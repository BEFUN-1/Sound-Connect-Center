#include "sony/protocol/ProtocolV2.h"
#include "ProtocolHelpers.h"
#include "sony/protocol/MultipointCodec.h"
#include "sony/protocol/NoiseControlCodec.h"
#include <algorithm>
#include <array>
#include <thread>

namespace sony::protocol {

using detail::clampEqValue;
using detail::clampEqValueRaw;
using detail::codecName;

namespace {

constexpr std::array MultipointBusyRetryDelays{
    std::chrono::milliseconds(500),
    std::chrono::milliseconds(1000),
    std::chrono::milliseconds(2000),
};

// Auto-power-off codes: 0=Off, 1=5min, 2=30min, 3=1h, 4=3h, 5=when-taken-off
const std::pair<uint8_t, uint8_t> APO_CODES[] = {
    { 0x11, 0x00 }, { 0x00, 0x00 }, { 0x01, 0x01 }, { 0x02, 0x02 }, { 0x03, 0x03 }, { 0x10, 0x00 }
};

int apoIndexFromCode(uint8_t c0, uint8_t c1) {
    for (int i = 0; i < 6; i++) {
        if (APO_CODES[i].first == c0 && APO_CODES[i].second == c1) {
            return i;
        }
    }
    return 0;
}

} // namespace

ProtocolV2::ProtocolV2(SonyProtocolSession& session, bool tenBandEqualizer)
    : _session(session), _tenBandEqualizer(tenBandEqualizer) {}

void ProtocolV2::initDevice() {
    // V2 handshake init: 0x00 0x00 -> RET 0x01
    const auto response = _session.sendAndAwaitResponse(
        SonyFrame{ .type = DataType::DataMdr, .payload = {0x00, 0x00} },
        0x01,
        -1,
        std::chrono::milliseconds(1000)
    );
    if (response.payload.size() == 8 && response.payload[7] <= 1)
        _table2Supported = response.payload[7] == 0;
    if (response.payload.size() == 8 && response.payload[6] == 0) {
        try {
            const auto functions = _session.sendAndAwaitResponse(
                SonyFrame{.type=DataType::DataMdr, .payload={0x06,0}}, 0x07, 0,
                std::chrono::milliseconds(1200), [](const SonyFrame&) { return true; });
            const auto supported = multipoint::functions(functions.payload);
            if (supported.count(0x6D)) _noiseType = 0x19;
        } catch (const SonyException&) {
            // Capability discovery is optional; a completed init response
            // still proves that the control channel is usable.
        }
    }
}

BatteryState ProtocolV2::getBattery() {
    // V2 battery request uses opcode 0x22
    BatteryState state;

    // 1. Single battery (over-ear / main): GET 22 00 -> RET 23 00 <level> <charging>
    try {
        auto resp = _session.sendAndAwaitResponse(
            SonyFrame{ .type = DataType::DataMdr, .payload = {0x22, 0x00} },
            0x23,
            0x00,
            std::chrono::milliseconds(1000)
        );
        if (resp.payload.size() >= 4) {
            state.main = static_cast<int>(resp.payload[2]);
            state.charging = (resp.payload[3] == 1);
            return state;
        }
    } catch (const SonyException&) {}

    // 2. Dual L/R battery for TWS earbuds: GET 22 09 -> RET 23 09 <Llvl> <Lchg> <Rlvl> <Rchg>
    try {
        auto resp = _session.sendAndAwaitResponse(
            SonyFrame{ .type = DataType::DataMdr, .payload = {0x22, 0x09} },
            0x23,
            0x09,
            std::chrono::milliseconds(1000)
        );
        if (resp.payload.size() >= 6) {
            state.left = static_cast<int>(resp.payload[2]);
            state.right = static_cast<int>(resp.payload[4]);
            state.main = std::min(*state.left, *state.right);
            state.charging = (resp.payload[3] == 1 || resp.payload[5] == 1);
        }
    } catch (const SonyException&) {}

    // 3. Case battery for TWS earbuds: GET 22 0a -> RET 23 0a <level> <chg>
    try {
        auto resp = _session.sendAndAwaitResponse(
            SonyFrame{ .type = DataType::DataMdr, .payload = {0x22, 0x0a} },
            0x23,
            0x0a,
            std::chrono::milliseconds(1000)
        );
        if (resp.payload.size() >= 4) {
            state.caseBattery = static_cast<int>(resp.payload[2]);
        }
    } catch (const SonyException&) {}

    return state;
}

NoiseControlState ProtocolV2::getNoiseControl() {
    // Function 0x6D selects XM6's type 0x19 with two adaptation fields.
    auto resp = _session.sendAndAwaitResponse(
        SonyFrame{ .type = DataType::DataMdr, .payload = {0x66, _noiseType} },
        0x67,
        _noiseType,
        std::chrono::milliseconds(1000)
    );
    return decodeV2NoiseControl(resp.payload);
}

void ProtocolV2::setNoiseControl(const NoiseControlState& state) {
    uint8_t effect = (state.mode == NoiseControlMode::Off) ? 0 : 1;
    uint8_t settingType = (state.mode == NoiseControlMode::Ambient) ? 1 : 0;
    uint8_t voice = state.focusOnVoice ? 1 : 0;
    uint8_t level = static_cast<uint8_t>(std::clamp(state.ambientLevel, 0, 255));

    std::vector<uint8_t> payload = {
        0x68,
        _noiseType,
        0x01,
        effect,
        settingType,
        voice,
        level
    };
    if (_noiseType == 0x19) {
        payload.push_back(state.adaptiveNoiseCancelling ? 1 : 0);
        payload.push_back(static_cast<uint8_t>(std::clamp(state.adaptiveSensitivity, 0, 2)));
    }

    _session.send(SonyFrame{ .type = DataType::DataMdr, .payload = std::move(payload) });
}

EqualizerState ProtocolV2::getEqualizer() {
    // Legacy: GET 56 00 -> RET 57 00 <preset> 06 <bass+10> <b1..b5 +10>
    // 10-band (e.g. WH-1000XM6): GET 56 04 -> RET 57 04 <preset> 0a <b1..b10>,
    // no separate Clear Bass slot. Reverse-engineered from a Sound Connect
    // btsnoop capture -- see issue #10.
    const uint8_t inquired = _tenBandEqualizer ? 0x04 : 0x00;
    auto resp = _session.sendAndAwaitResponse(
        SonyFrame{ .type = DataType::DataMdr, .payload = {0x56, inquired} },
        0x57,
        -1,
        std::chrono::milliseconds(1000)
    );

    if (resp.payload.size() < 4 || resp.payload[1] != inquired)
        throw SonyException(SonyErrorCode::InvalidResponse, "Incomplete equalizer response");
    EqualizerState state;
    state.preset = static_cast<int>(resp.payload[2]);
    const size_t count = resp.payload[3];
    if (resp.payload.size() < 4 + count)
        throw SonyException(SonyErrorCode::InvalidResponse, "Truncated equalizer response");
    if (_tenBandEqualizer) {
        // <preset> 0a <b1..b10>, raw units, no bias.
        state.clearBass = 0;
        state.bands.assign(count, 0);
        for (size_t i = 0; i < count; ++i) {
            state.bands[i] = static_cast<int>(resp.payload[4 + i]);
        }
    } else if (count >= 1) {
        // <preset> 06 <bass+10> <b1..b5 +10>
        state.clearBass = static_cast<int>(resp.payload[4]) - 10;
        state.bands.assign(count - 1, 0);
        for (size_t i = 0; i + 1 < count; ++i) {
            state.bands[i] = static_cast<int>(resp.payload[5 + i]) - 10;
        }
    }
    return state;
}

void ProtocolV2::setEqualizerPreset(int preset) {
    // SET preset: 58 <inquired> <preset> 00 -- same shape on both layouts,
    // only the inquired-type byte differs.
    std::vector<uint8_t> payload = {
        0x58,
        static_cast<uint8_t>(_tenBandEqualizer ? 0x04 : 0x00),
        static_cast<uint8_t>(preset),
        0x00
    };
    _session.send(SonyFrame{ .type = DataType::DataMdr, .payload = std::move(payload) });
}

void ProtocolV2::setEqualizerCustom(int clearBass, const std::vector<int>& bands) {
    if (_tenBandEqualizer) {
        // SET custom: 58 04 A0 <bands.size()> <b1..bN>, raw units, no bias,
        // no separate Clear Bass slot (clearBass is ignored).
        std::vector<uint8_t> payload = {
            0x58,
            0x04,
            0xa0,
            static_cast<uint8_t>(bands.size())
        };
        for (int b : bands) {
            payload.push_back(clampEqValueRaw(b));
        }
        _session.send(SonyFrame{ .type = DataType::DataMdr, .payload = std::move(payload) });
        return;
    }
    // SET custom: 58 00 A0 <1+bands.size()> <clearBass+10> <b1..bN +10>
    std::vector<uint8_t> payload = {
        0x58,
        0x00,
        0xa0,
        static_cast<uint8_t>(1 + bands.size()),
        clampEqValue(clearBass)
    };
    for (int b : bands) {
        payload.push_back(clampEqValue(b));
    }
    _session.send(SonyFrame{ .type = DataType::DataMdr, .payload = std::move(payload) });
}

bool ProtocolV2::getDsee() {
    // GET: e6 01 -> RET: e7 01 <enabled 0/1>
    auto resp = _session.sendAndAwaitResponse(
        SonyFrame{ .type = DataType::DataMdr, .payload = {0xe6, 0x01} },
        0xe7,
        0x01,
        std::chrono::milliseconds(1000)
    );
    if (resp.payload.size() >= 3) {
        return resp.payload[2] != 0;
    }
    throw SonyException(SonyErrorCode::InvalidResponse, "Incomplete Dsee response");
}

void ProtocolV2::setDsee(bool enabled) {
    // SET: e8 01 <enabled 0/1>
    std::vector<uint8_t> payload = {
        0xe8,
        0x01,
        static_cast<uint8_t>(enabled ? 0x01 : 0x00)
    };
    _session.send(SonyFrame{ .type = DataType::DataMdr, .payload = std::move(payload) });
}

std::string ProtocolV2::getFirmwareVersion() {
    // GET: 04 02 -> RET: 05 02 <ascii version...>
    auto resp = _session.sendAndAwaitResponse(
        SonyFrame{ .type = DataType::DataMdr, .payload = {0x04, 0x02} },
        0x05,
        -1,
        std::chrono::milliseconds(1000)
    );
    if (resp.payload.size() > 3) {
        return std::string(resp.payload.begin() + 3, resp.payload.end());
    }
    return "";
}

std::string ProtocolV2::getCodec() {
    // GET: 12 02 -> RET: 13 02 <codec>
    auto resp = _session.sendAndAwaitResponse(
        SonyFrame{ .type = DataType::DataMdr, .payload = {0x12, 0x02} },
        0x13,
        -1,
        std::chrono::milliseconds(1000)
    );
    if (resp.payload.size() >= 3) {
        return codecName(resp.payload[2]);
    }
    return "";
}

int ProtocolV2::getAutoPowerOff() {
    // GET: 26 05 -> RET: 27 05 <c0> <c1>
    auto resp = _session.sendAndAwaitResponse(
        SonyFrame{ .type = DataType::DataMdr, .payload = {0x26, 0x05} },
        0x27,
        -1,
        std::chrono::milliseconds(1000)
    );
    if (resp.payload.size() >= 4) {
        return apoIndexFromCode(resp.payload[2], resp.payload[3]);
    }
    throw SonyException(SonyErrorCode::InvalidResponse, "Incomplete AutoPowerOff response");
}

void ProtocolV2::setAutoPowerOff(int index) {
    if (index < 0 || index >= 6) {
        return;
    }
    // SET: 28 05 <c0> <c1>
    std::vector<uint8_t> payload = {
        0x28,
        0x05,
        APO_CODES[index].first,
        APO_CODES[index].second
    };
    _session.send(SonyFrame{ .type = DataType::DataMdr, .payload = std::move(payload) });
}

bool ProtocolV2::getSpeakToChat() {
    // GET: f6 0c -> RET: f7 0c <inverted_enabled>
    auto resp = _session.sendAndAwaitResponse(
        SonyFrame{ .type = DataType::DataMdr, .payload = {0xf6, 0x0c} },
        0xf7,
        0x0c,
        std::chrono::milliseconds(1000)
    );
    if (resp.payload.size() >= 3) {
        return resp.payload[2] == 0;
    }
    throw SonyException(SonyErrorCode::InvalidResponse, "Incomplete SpeakToChat response");
}

void ProtocolV2::setSpeakToChat(bool enabled) {
    // SET: f8 0c <inverted_enabled> 01
    std::vector<uint8_t> payload = {
        0xf8,
        0x0c,
        static_cast<uint8_t>(enabled ? 0x00 : 0x01),
        0x01
    };
    _session.send(SonyFrame{ .type = DataType::DataMdr, .payload = std::move(payload) });
}

bool ProtocolV2::getAdaptiveVolume() {
    // GET: f6 0a -> RET: f7 0a <inverted_enabled>
    auto resp = _session.sendAndAwaitResponse(
        SonyFrame{ .type = DataType::DataMdr, .payload = {0xf6, 0x0a} },
        0xf7,
        0x0a,
        std::chrono::milliseconds(1000)
    );
    if (resp.payload.size() >= 3) {
        return resp.payload[2] == 0;
    }
    throw SonyException(SonyErrorCode::InvalidResponse, "Incomplete AdaptiveVolume response");
}

void ProtocolV2::setAdaptiveVolume(bool enabled) {
    // SET: f8 0a <inverted_enabled>
    std::vector<uint8_t> payload = {
        0xf8,
        0x0a,
        static_cast<uint8_t>(enabled ? 0x00 : 0x01)
    };
    _session.send(SonyFrame{ .type = DataType::DataMdr, .payload = std::move(payload) });
}

void ProtocolV2::powerOff() {
    // POWER_SET_STATUS / POWER_OFF / USER_POWER_OFF from the Sound Connect
    // V2 table-1 command model: 0x24 0x03 0x01.
    _session.send(SonyFrame{ .type = DataType::DataMdr, .payload = {0x24, 0x03, 0x01} });
}

SonyFrame ProtocolV2::sendNo2AndAwait(const std::vector<uint8_t>& request, uint8_t opcode,
                                     int subtype, std::chrono::milliseconds timeout) {
    return _session.sendAndAwaitResponse(SonyFrame{.type = DataType::DataMdrNo2, .payload = request},
        opcode, subtype, timeout, [](const SonyFrame&) { return true; });
}
void ProtocolV2::sendNo2(const std::vector<uint8_t>& request) {
    _session.send(SonyFrame{.type = DataType::DataMdrNo2, .payload = request});
}
void ProtocolV2::discoverMultipoint() {
    if (_multipointDiscovered) return;
    if (_table2Supported == false) { _multipointDiscovered = true; return; }
    const auto response = sendNo2AndAwait({0x06, 0x00}, 0x07, 0);
    const auto supported = multipoint::functions(response.payload);
    _multipoint.inquiredType = (supported.count(0x32) || supported.count(0x33)) ? 2 : supported.count(0x30) ? 0 : -1;
    _multipoint.supported = _multipoint.inquiredType >= 0;
    _multipoint.sourceSwitchSupported = _multipoint.supported && supported.count(0x31);
    _multipoint.sourceKeepSupported = _multipoint.sourceSwitchSupported;
    if (_multipoint.supported) {
        const auto type = static_cast<uint8_t>(_multipoint.inquiredType);
        const auto cap = sendNo2AndAwait({0x30, type}, 0x31, type);
        if (cap.payload.size() != 5 || cap.payload[3] == 0) multipoint::invalid();
        _multipoint.maxConnected = cap.payload[3];
    }
    _multipointDiscovered = true;
}
MultipointState ProtocolV2::getMultipoint() {
    std::lock_guard lock(_mutex);
    discoverMultipoint();
    if (!_multipoint.supported) return _multipoint;
    const auto type = static_cast<uint8_t>(_multipoint.inquiredType);
    const auto list = sendNo2AndAwait({0x36, type}, 0x37, type);
    _multipoint = multipoint::devices(list.payload, _multipoint);
    if (_multipoint.sourceKeepSupported) {
        const auto keep = sendNo2AndAwait({0x36, 1}, 0x37, 1);
        if (keep.payload.size() != 3 || keep.payload[2] > 1) multipoint::invalid();
        _multipoint.sourceKeeping = keep.payload[2] == 0;
    }
    return _multipoint;
}
void ProtocolV2::switchMultipointSource(std::string_view input) {
    const auto target = multipoint::address(input);
    const auto state = getMultipoint();
    if (!state.sourceSwitchSupported) throw SonyException(SonyErrorCode::Unsupported, "Headset does not support playback source switching");
    if (std::none_of(state.devices.begin(), state.devices.end(), [&](const auto& d) { return d.address == target && d.connected; }))
        throw std::invalid_argument("Playback source must be connected to the headphones");
    std::lock_guard lock(_mutex);
    std::vector<uint8_t> request{0x3C, 1};
    request.insert(request.end(), target.begin(), target.end());
    const auto result = _session.sendAndAwaitResponse(SonyFrame{.type = DataType::DataMdrNo2, .payload = request},
        0x3D, 1, std::chrono::seconds(10), [target](const auto& frame) {
            return frame.payload.size() == 20 && std::string(frame.payload.begin() + 3, frame.payload.end()) == target;
        });
    const auto code = result.payload[2];
    if (code != 0) {
        const char* reason = code == 2 ? "A call is active" : code == 3 ? "A2DP is not connected" :
            code == 4 ? "Voice assistant has priority" : "Headset rejected playback source switch";
        throw SonyException(SonyErrorCode::ProtocolViolation, reason);
    }
}
void ProtocolV2::setMultipointSourceKeeping(bool enabled) {
    const auto state = getMultipoint();
    if (!state.sourceKeepSupported) throw SonyException(SonyErrorCode::Unsupported, "Headset does not support fixed playback source");
    std::lock_guard lock(_mutex);
    const uint8_t value = enabled ? 0 : 1;
    const auto result = _session.sendAndAwaitResponse(SonyFrame{.type = DataType::DataMdrNo2, .payload = {0x38, 1, value}},
        0x39, 1, std::chrono::seconds(5), [value](const auto& frame) {
            return frame.payload.size() == 4 && frame.payload[2] == value;
        });
    if (result.payload[3] != 0) throw SonyException(SonyErrorCode::ProtocolViolation, "Headset rejected fixed playback source setting");
    _multipoint.sourceKeeping = enabled;
}
void ProtocolV2::multipointAction(std::string_view input, uint8_t action) {
    const auto target = multipoint::address(input);
    const auto state = getMultipoint();
    if (!state.supported) throw SonyException(SonyErrorCode::Unsupported, "Headset does not support multipoint device management");
    if (std::none_of(state.devices.begin(), state.devices.end(), [&](const auto& d) { return d.address == target; }))
        throw std::invalid_argument("Device is not in the headphones' pairing history");
    std::lock_guard lock(_mutex);
    const auto type = static_cast<uint8_t>(state.inquiredType);
    std::vector<uint8_t> request{0x3C, type, action};
    request.insert(request.end(), target.begin(), target.end());
    const uint8_t success = action << 4;
    for (size_t attempt = 0;; ++attempt) {
        const auto result = _session.sendAndAwaitResponse(SonyFrame{.type = DataType::DataMdrNo2, .payload = request},
            0x3D, type, std::chrono::seconds(12), [target, action](const auto& frame) {
                return frame.payload.size() == 21 && frame.payload[2] == action &&
                    std::string(frame.payload.begin() + 4, frame.payload.end()) == target;
            });
        const auto resultCode = result.payload[3];
        if (resultCode == success) return;
        if (resultCode == success + 2 && (action == 0 || action == 1)) return;
        if (resultCode != success + 3 || attempt == MultipointBusyRetryDelays.size())
            throw SonyException(SonyErrorCode::ProtocolViolation,
                resultCode == success + 3
                    ? "Headset remained busy after bounded retries; wait for its current operation to finish and retry"
                    : "Headset rejected device management operation");

        // Busy is an explicit rejection, so retrying this same desired-state
        // operation is safe after the headset has had time to finish its work.
        std::this_thread::sleep_for(MultipointBusyRetryDelays[attempt]);
    }
}
void ProtocolV2::multipointConnect(std::string_view address) { multipointAction(address, 1); }
void ProtocolV2::multipointDisconnect(std::string_view address) { multipointAction(address, 0); }
void ProtocolV2::multipointUnpair(std::string_view address) { multipointAction(address, 2); }

} // namespace sony::protocol
