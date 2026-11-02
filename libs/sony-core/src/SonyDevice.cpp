#include "sony/core/SonyDevice.h"
#include "sony/protocol/DeviceProfileRegistry.h"
#include "sony/protocol/ProtocolV1.h"
#include "sony/protocol/ProtocolV2.h"
#include "sony/protocol/MultipointCodec.h"
#include "sony/transport/Logger.h"
#include <atomic>

namespace sony::core {
namespace {
std::atomic<uint64_t> nextSessionId{0};
}

SonyDevice::SonyDevice(
    std::shared_ptr<transport::ITransport> transport,
    SonyProtocolVersion version)
    : _transport(std::move(transport)), _version(version) {
    auto defaultProf = protocol::DeviceProfileRegistry::getProfile(protocol::SonyModel::Unknown);
    _profile = defaultProf.value_or(protocol::DeviceProfile{});
    _capabilities = _profile.capabilities;
    _name = std::string(to_string(_profile.model));
}

SonyDevice::~SonyDevice() {
    disconnect();
}

void SonyDevice::connect(const transport::DeviceAddress& address, std::string_view deviceName,
                         InitialRefresh refresh) {
    if (!_transport) {
        throw SonyException(SonyErrorCode::TransportFailure, "No transport configured");
    }

    if (isConnected()) {
        disconnect();
    }

    // A Bluetooth name is only a profile hint. The platform connector can
    // determine the actual generation from the SDP/RFCOMM service UUID after
    // connect(), and that negotiated result must win because opcode 0x22 is
    // battery on V2 but POWER OFF on V1.
    _profile = protocol::DeviceProfileRegistry::getProfileForDevice(deviceName);
    _capabilities = _profile.capabilities;
    _capabilities.powerOff = _profile.model != protocol::SonyModel::Unknown;
    _version = _profile.protocol;
    _name = deviceName.empty() ? std::string(to_string(_profile.model))
                               : std::string(deviceName);

    // Tear the previous session down *before* opening the new connection.
    // ~SonyProtocolSession disconnects the transport, and assigning over the
    // unique_ptr in _setupSession() destroys the old session only after the new
    // one exists — which would drop the link that was just established. This is
    // what makes reconnecting, or switching to a second device, work at all.
    _protocol.reset();
    _session.reset();

    // A failed switch must not expose the previous device's confirmed values.
    {
        std::lock_guard lock(_stateMutex);
        _state = {};
        _state.sessionId = ++nextSessionId;
        _refreshStep = 0;
    }

    _transport->connect(address);

    if (const auto negotiated = _transport->protocolVersion(); negotiated.has_value()) {
        if (*negotiated != _profile.protocol) {
            Logger::warn(LogCategory::Device,
                         "Bluetooth service UUID and device-name profile disagree; using negotiated protocol");
            // A mismatched name cannot safely supply feature capabilities for
            // the negotiated generation. Keep the display name, but start
            // with a conservative empty capability set until probing exists.
            _profile.protocol = *negotiated;
            _profile.capabilities = {};
            _capabilities = {};
        }
        _version = *negotiated;
    }

    {
        std::lock_guard lock(_stateMutex);
        const auto& c = _capabilities;
        for (const auto& [name, supported] : std::initializer_list<std::pair<std::string, bool>>{
            {"battery",c.battery},{"noiseControl",c.noiseCancelling || c.ambientSound},
            {"equalizer",c.equalizer},{"dsee",c.dsee},{"codec",c.codecInfo},{"firmware",c.firmwareInfo},
            {"speakToChat",c.speakToChat},{"adaptiveVolume",c.adaptiveVolume},{"autoPowerOff",c.autoPowerOff},
            {"multipoint",c.multipoint && _version == SonyProtocolVersion::V2}})
            _state.features[name].availability = supported ? "unknown" : "unsupported";
    }

    _setupSession();

    if (_protocol) {
        try {
            _protocol->initDevice();
        } catch (const SonyException& ex) {
            Logger::warn(LogCategory::Device, "Device init handshake failed: " + std::string(ex.what()));
            disconnect();
            throw;
        }
    }

    if (refresh == InitialRefresh::All) refreshAll();

    _dispatcher.dispatch(protocol::ConnectionChanged{
        .connected = true,
        .deviceAddress = address.str()
    });
}

void SonyDevice::disconnect() noexcept {
    if (_session) {
        _session->disconnect();
    }
    if (_transport) {
        _transport->disconnect();
    }
    {
        std::lock_guard lock(_stateMutex);
        for (auto& [name, status] : _state.features)
            if (status.availability == "valid") status.availability = "stale";
        ++_state.revision;
    }
    _dispatcher.dispatch(protocol::ConnectionChanged{
        .connected = false,
        .deviceAddress = ""
    });
}

bool SonyDevice::isConnected() const noexcept {
    return _session && _session->isConnected();
}

SonyProtocolVersion SonyDevice::protocolVersion() const noexcept {
    return _version;
}

const std::string& SonyDevice::name() const noexcept {
    return _name;
}

const protocol::DeviceProfile& SonyDevice::profile() const noexcept {
    return _profile;
}

const protocol::DeviceCapabilities& SonyDevice::capabilities() const noexcept {
    return _capabilities;
}

protocol::DeviceState SonyDevice::state() const {
    std::lock_guard lock(_stateMutex);
    return _state;
}

protocol::DeviceStateSnapshot SonyDevice::snapshot() const {
    std::lock_guard lock(_stateMutex);
    return std::make_shared<const protocol::DeviceState>(_state);
}

protocol::DeviceEventDispatcher& SonyDevice::events() noexcept {
    return _dispatcher;
}

void SonyDevice::_setupSession() {
    _session = std::make_unique<protocol::SonyProtocolSession>(_transport.get(),
        protocol::SonyProtocolSession::ReplyPolicy::FreshOnly);
    if (_version == SonyProtocolVersion::V1) {
        _protocol = std::make_unique<protocol::ProtocolV1>(*_session);
    } else {
        _protocol = std::make_unique<protocol::ProtocolV2>(
            *_session, _capabilities.tenBandEqualizer, _capabilities.wearSensor);
    }

    _session->onNotification([this](const protocol::SonyFrame& frame) {
        _onNotification(frame);
    });

    _session->start();
}

void SonyDevice::_onNotification(const protocol::SonyFrame& frame) {
    protocol::DeviceStateSnapshot updated;
    std::string feature;
    {
        std::lock_guard lock(_stateMutex);
        if (frame.type == protocol::DataType::DataMdrNo2) {
            if (frame.payload.size() >= 2 && frame.payload[0] == 0x39 && (frame.payload[1] == 0 || frame.payload[1] == 2)) {
                _state.multipoint = protocol::multipoint::devices(frame.payload, _state.multipoint);
            } else if (frame.payload.size() == 4 && frame.payload[0] == 0x39 && frame.payload[1] == 1 && frame.payload[2] <= 1 && frame.payload[3] == 0) {
                _state.multipoint.sourceKeeping = frame.payload[2] == 0;
            } else return;
            feature = "multipoint";
        } else {
        if (frame.type != protocol::DataType::DataMdr) return;
        const auto& p = frame.payload;
        if (_version == SonyProtocolVersion::V1 && p.size() >= 4 && (p[0] == 0x11 || p[0] == 0x13)) {
            if (p[1] == 0) {
                _state.battery.main = p[2];
                _state.battery.charging = p[3] == 1;
            } else if (p[1] == 1 && p.size() >= 6) {
                _state.battery.left = p[2]; _state.battery.right = p[4];
                _state.battery.main = (std::min)(int(p[2]), int(p[4]));
                _state.battery.charging = p[3] == 1 || p[5] == 1;
            } else if (p[1] == 2) _state.battery.caseBattery = p[2];
            else return;
            feature = "battery";
        } else if (_version == SonyProtocolVersion::V1 && p.size() >= 8 &&
                   (p[0] == 0x67 || p[0] == 0x69) && p[1] == 2) {
            auto& nc = _state.noiseControl;
            nc.mode = p[2] == 0 ? protocol::NoiseControlMode::Off :
                p[4] == 0 ? protocol::NoiseControlMode::Ambient : protocol::NoiseControlMode::NoiseCancelling;
            nc.ambientLevel = nc.mode == protocol::NoiseControlMode::Ambient ? p[7] : 0;
            nc.focusOnVoice = p[6] == 1;
            feature = "noiseControl";
        } else {
        if (!_dispatcher.parseNotificationPayload(frame.payload, _state, false)) return;
        const auto opcode = frame.payload[0];
        feature = (opcode == 0x23 || opcode == 0x25) ? "battery" :
            (opcode == 0x67 || opcode == 0x69) ? "noiseControl" : "equalizer";
        }
        }
        _markSuccess(feature);
        updated = std::make_shared<const protocol::DeviceState>(_state);
    }
    if (feature == "battery") _dispatcher.dispatch(protocol::BatteryChanged{updated->battery});
    if (feature == "noiseControl") _dispatcher.dispatch(protocol::NoiseControlChanged{updated->noiseControl});
    if (feature == "equalizer") _dispatcher.dispatch(protocol::EqualizerChanged{updated->equalizer});
    _dispatcher.dispatch(protocol::DeviceStateChanged{updated});
}

void SonyDevice::refreshAll() {
    refreshBattery();
    refreshNoiseControl();
    refreshEqualizer();
    refreshDsee();
    if (_capabilities.autoPowerOff) {
        try { _readAutoPowerOff(); }
        catch (const SonyException& ex) { _markError("autoPowerOff", ex); }
    }
}

void SonyDevice::refreshBattery() {
    if (!_protocol) return;
    if (!_capabilities.battery) return;
    try {
        auto bat = _protocol->getBattery();
        if (!bat.main && !bat.left && !bat.right && !bat.caseBattery)
            throw SonyException(SonyErrorCode::InvalidResponse, "No battery reading received");
        {
            std::lock_guard lock(_stateMutex);
            _state.battery = bat;
        _markSuccess("battery");
        }
        _dispatcher.dispatch(protocol::BatteryChanged{bat});
        _dispatcher.dispatch(protocol::DeviceStateChanged{snapshot()});
    } catch (const SonyException& ex) {
        _markError("battery", ex);
        Logger::debug(LogCategory::Device, "refreshBattery error: " + std::string(ex.what()));
    }
}

void SonyDevice::refreshNoiseControl() {
    if (!_protocol) return;
    if (!_capabilities.noiseCancelling && !_capabilities.ambientSound) return;
    try {
        auto nc = _protocol->getNoiseControl();
        {
            std::lock_guard lock(_stateMutex);
            _state.noiseControl = nc;
        _markSuccess("noiseControl");
        }
        _dispatcher.dispatch(protocol::NoiseControlChanged{nc});
        _dispatcher.dispatch(protocol::DeviceStateChanged{snapshot()});
    } catch (const SonyException& ex) {
        _markError("noiseControl", ex);
        Logger::debug(LogCategory::Device, "refreshNoiseControl error: " + std::string(ex.what()));
    }
}

void SonyDevice::refreshEqualizer() {
    if (!_protocol) return;
    if (!_capabilities.equalizer) return;
    try {
        auto eq = _protocol->getEqualizer();
        {
            std::lock_guard lock(_stateMutex);
            _state.equalizer = eq;
        _markSuccess("equalizer");
        }
        _dispatcher.dispatch(protocol::EqualizerChanged{eq});
        _dispatcher.dispatch(protocol::DeviceStateChanged{snapshot()});
    } catch (const SonyException& ex) {
        _markError("equalizer", ex);
        Logger::debug(LogCategory::Device, "refreshEqualizer error: " + std::string(ex.what()));
    }
}

void SonyDevice::refreshDsee() {
    if (!_protocol) return;
    if (!_capabilities.dsee) return;
    try {
        bool dsee = _protocol->getDsee();
        {
            std::lock_guard lock(_stateMutex);
            _state.dsee = dsee;
        _markSuccess("dsee");
        }
        _dispatcher.dispatch(protocol::DeviceStateChanged{snapshot()});
    } catch (const SonyException& ex) {
        _markError("dsee", ex);
        Logger::debug(LogCategory::Device, "refreshDsee error: " + std::string(ex.what()));
    }
}

void SonyDevice::setNoiseControl(const protocol::NoiseControlState& nc) {
    if (!_protocol) return;
    _confirmSetting("noiseControl", [&] {
        _protocol->setNoiseControl(nc);
        const auto confirmed = _protocol->getNoiseControl();
        if (confirmed.mode != nc.mode || (nc.mode == protocol::NoiseControlMode::Ambient &&
            (confirmed.ambientLevel != nc.ambientLevel || confirmed.focusOnVoice != nc.focusOnVoice)))
            throw SonyException(SonyErrorCode::InvalidResponse, "Headset did not confirm the noise-control setting");
        std::lock_guard lock(_stateMutex);
        _state.noiseControl = confirmed;
        _markSuccess("noiseControl");
    });
    _dispatcher.dispatch(protocol::NoiseControlChanged{snapshot()->noiseControl});
    _dispatcher.dispatch(protocol::DeviceStateChanged{snapshot()});
}

void SonyDevice::setAnc(bool enabled) {
    auto nc = snapshot()->noiseControl;
    nc.mode = enabled ? protocol::NoiseControlMode::NoiseCancelling : protocol::NoiseControlMode::Off;
    setNoiseControl(nc);
}

void SonyDevice::setAmbient(int level, bool focusOnVoice) {
    auto nc = snapshot()->noiseControl;
    nc.mode = protocol::NoiseControlMode::Ambient;
    nc.ambientLevel = std::clamp(level, 0, 255);
    nc.focusOnVoice = focusOnVoice;
    setNoiseControl(nc);
}

void SonyDevice::setEqualizerPreset(int preset) {
    if (preset < 0 || preset > 255 || !_protocol) return;
    _confirmSetting("equalizer", [&] {
        _protocol->setEqualizerPreset(preset);
        const auto confirmed = _protocol->getEqualizer();
        if (confirmed.preset != preset)
            throw SonyException(SonyErrorCode::InvalidResponse, "Headset did not confirm the equalizer preset");
        std::lock_guard lock(_stateMutex);
        _state.equalizer = confirmed;
        _markSuccess("equalizer");
    });
    _dispatcher.dispatch(protocol::EqualizerChanged{snapshot()->equalizer});
    _dispatcher.dispatch(protocol::DeviceStateChanged{snapshot()});
}

void SonyDevice::setEqualizerCustom(int clearBass, const std::vector<int>& bands) {
    if (!_protocol) return;
    _confirmSetting("equalizer", [&] {
        _protocol->setEqualizerCustom(clearBass, bands);
        const auto confirmed = _protocol->getEqualizer();
        if (confirmed.preset != 0xa0 || confirmed.bands != bands ||
            (_capabilities.clearBass && confirmed.clearBass != clearBass))
            throw SonyException(SonyErrorCode::InvalidResponse, "Headset did not confirm the custom equalizer");
        std::lock_guard lock(_stateMutex);
        _state.equalizer = confirmed;
        _markSuccess("equalizer");
    });
    _dispatcher.dispatch(protocol::EqualizerChanged{snapshot()->equalizer});
    _dispatcher.dispatch(protocol::DeviceStateChanged{snapshot()});
}

void SonyDevice::setDsee(bool enabled) {
    if (!_protocol) return;
    _confirmSetting("dsee", [&] {
        _protocol->setDsee(enabled);
        const auto confirmed = _protocol->getDsee();
        if (confirmed != enabled)
            throw SonyException(SonyErrorCode::InvalidResponse, "Headset did not confirm the dsee setting");
        std::lock_guard lock(_stateMutex);
        _state.dsee = confirmed;
        _markSuccess("dsee");
    });
    _dispatcher.dispatch(protocol::DeviceStateChanged{snapshot()});
}

void SonyDevice::_readAutoPowerOff() {
    const auto value = _protocol->getAutoPowerOff();
    const auto options = _protocol->getAutoPowerOffOptions();
    {
        std::lock_guard lock(_stateMutex);
        _state.autoPowerOff = value;
        _state.autoPowerOffOptions = options;
        _markSuccess("autoPowerOff");
    }
}

void SonyDevice::setAutoPowerOff(int index) {
    if (!_protocol) return;
    try {
        // Read the independent remembered duration before switching to the
        // wearing mode, and reject modes absent from the headset capability.
        _readAutoPowerOff();
        const auto options = snapshot()->autoPowerOffOptions;
        if (std::find(options.begin(), options.end(), index) == options.end())
            throw SonyException(SonyErrorCode::Unsupported, "Headset does not support this auto-power-off setting");
        _protocol->setAutoPowerOff(index);
        _readAutoPowerOff();
        if (snapshot()->autoPowerOff != index)
            throw SonyException(SonyErrorCode::InvalidResponse, "Headset did not confirm the auto-power-off setting");
    } catch (const SonyException& ex) {
        _markError("autoPowerOff", ex);
        throw;
    }
    _dispatcher.dispatch(protocol::DeviceStateChanged{snapshot()});
}

void SonyDevice::setSpeakToChat(bool enabled) {
    if (!_protocol) return;
    _confirmSetting("speakToChat", [&] {
        _protocol->setSpeakToChat(enabled);
        const auto confirmed = _protocol->getSpeakToChat();
        if (confirmed != enabled)
            throw SonyException(SonyErrorCode::InvalidResponse, "Headset did not confirm the speakToChat setting");
        std::lock_guard lock(_stateMutex);
        _state.speakToChat = confirmed;
        _markSuccess("speakToChat");
    });
    _dispatcher.dispatch(protocol::DeviceStateChanged{snapshot()});
}

void SonyDevice::setAdaptiveVolume(bool enabled) {
    if (!_protocol) return;
    _confirmSetting("adaptiveVolume", [&] {
        _protocol->setAdaptiveVolume(enabled);
        const auto confirmed = _protocol->getAdaptiveVolume();
        if (confirmed != enabled)
            throw SonyException(SonyErrorCode::InvalidResponse, "Headset did not confirm the adaptiveVolume setting");
        std::lock_guard lock(_stateMutex);
        _state.adaptiveVolume = confirmed;
        _markSuccess("adaptiveVolume");
    });
    _dispatcher.dispatch(protocol::DeviceStateChanged{snapshot()});
}

void SonyDevice::powerOff() {
    if (!_protocol) return;
    _protocol->powerOff();
    // The headset normally closes the link immediately after accepting this
    // command, so do not issue another readback request here.
    disconnect();
}

void SonyDevice::refreshMultipoint() {
    if (!_protocol || !_capabilities.multipoint || _version != SonyProtocolVersion::V2) return;
    try {
        auto value = _protocol->getMultipoint();
        {
            std::lock_guard lock(_stateMutex);
            _state.multipoint = std::move(value);
            if (_state.multipoint.supported) _markSuccess("multipoint");
            else _state.features["multipoint"] = {"unsupported", 0, "Headset does not advertise multipoint management"};
        }
        _dispatcher.dispatch(protocol::DeviceStateChanged{snapshot()});
    } catch (const SonyException& ex) {
        _markError("multipoint", ex);
        std::lock_guard lock(_stateMutex);
        _state.multipoint.error = ex.what();
    }
}
void SonyDevice::switchMultipointSource(std::string_view address) {
    if (!_protocol || !isConnected()) throw SonyException(SonyErrorCode::Disconnected);
    _protocol->switchMultipointSource(address);
    refreshMultipoint();
}
void SonyDevice::setMultipointSourceKeeping(bool enabled) {
    if (!_protocol || !isConnected()) throw SonyException(SonyErrorCode::Disconnected);
    _protocol->setMultipointSourceKeeping(enabled);
    refreshMultipoint();
}
void SonyDevice::multipointConnect(std::string_view address) {
    if (!_protocol || !isConnected()) throw SonyException(SonyErrorCode::Disconnected);
    _protocol->multipointConnect(address);
    refreshMultipoint();
}
void SonyDevice::multipointDisconnect(std::string_view address) {
    if (!_protocol || !isConnected()) throw SonyException(SonyErrorCode::Disconnected);
    _protocol->multipointDisconnect(address);
    refreshMultipoint();
}
void SonyDevice::multipointUnpair(std::string_view address) {
    if (!_protocol || !isConnected()) throw SonyException(SonyErrorCode::Disconnected);
    _protocol->multipointUnpair(address);
    refreshMultipoint();
}

void SonyDevice::performMultipoint(std::string_view action, std::string_view address,
                                  bool enabled, bool closesControlSession) {
    if (!_protocol || !isConnected()) throw SonyException(SonyErrorCode::Disconnected);
    _confirmSetting("multipoint", [&] {
        auto state = _protocol->getMultipoint();
        {
            std::lock_guard lock(_stateMutex);
            _state.multipoint = state;
            if (state.supported) _markSuccess("multipoint");
        }
        if (!state.supported)
            throw SonyException(SonyErrorCode::Unsupported, "Headset does not advertise multipoint management");
        if (action == "list") return;
        if (action == "source" || action == "default") {
            const auto target = protocol::multipoint::address(address);
            if (!state.sourceSwitchSupported ||
                (action == "default" && !state.sourceKeepSupported))
                throw SonyException(SonyErrorCode::Unsupported, "Playback source switching is not supported");
            if (std::none_of(state.devices.begin(), state.devices.end(),
                    [&](const auto& peer) { return peer.address == target && peer.connected; }))
                throw std::invalid_argument("Playback source must be connected to the headphones");
            const bool keeping = action == "default";
            if (state.sourceKeeping != keeping) {
                _protocol->useMultipointPreflight(state);
                _protocol->setMultipointSourceKeeping(keeping);
                state.sourceKeeping = keeping;
            }
            _protocol->useMultipointPreflight(state);
            _protocol->switchMultipointSource(target);
        } else {
            _protocol->useMultipointPreflight(state);
            if (action == "keep") _protocol->setMultipointSourceKeeping(enabled);
            else if (action == "connect") _protocol->multipointConnect(address);
            else if (action == "disconnect") _protocol->multipointDisconnect(address);
            else if (action == "unpair") _protocol->multipointUnpair(address);
            else throw std::invalid_argument("Unknown multipoint action");
        }
        if (!closesControlSession) {
            const auto confirmed = _protocol->getMultipoint();
            std::lock_guard lock(_stateMutex);
            _state.multipoint = confirmed;
            _markSuccess("multipoint");
        }
    });
    _dispatcher.dispatch(protocol::DeviceStateChanged{snapshot()});
}

void SonyDevice::_confirmSetting(const std::string& feature, const std::function<void()>& operation) {
    try { operation(); }
    catch (const SonyException& ex) {
        _markError(feature, ex);
        _dispatcher.dispatch(protocol::DeviceStateChanged{snapshot()});
        throw;
    }
}
void SonyDevice::_markSuccess(const std::string& feature) {
    std::lock_guard lock(_stateMutex);
    auto& status = _state.features[feature];
    status = {"valid", (std::max)(protocol::stateTimestamp(), status.lastSuccessMs + 1), {}};
    ++_state.revision;
}
void SonyDevice::_markError(const std::string& feature, const SonyException& ex) {
    std::lock_guard lock(_stateMutex);
    auto& status = _state.features[feature];
    status.availability = ex.code() == SonyErrorCode::Unsupported ? "unsupported" : status.lastSuccessMs ? "stale" : "unknown";
    status.error = ex.what();
    ++_state.revision;
}
void SonyDevice::refreshSettingsStep() {
    if (!_protocol || !isConnected()) return;
    // Nine steps, each scheduled separately. Optional features are never probed
    // on profiles that don't advertise them.
    const auto step = _refreshStep++ % 9;
    if (step == 0) { refreshNoiseControl(); return; }
    if (step == 1) { refreshEqualizer(); return; }
    if (step == 2) { refreshDsee(); return; }
    std::string feature;
    try {
        if (step == 3 && _capabilities.codecInfo) {
            feature = "codec"; auto value = _protocol->getCodec();
            if (value.empty()) throw SonyException(SonyErrorCode::InvalidResponse, "Codec not reported");
            std::lock_guard lock(_stateMutex); _state.codec = value; _markSuccess(feature);
        } else if (step == 4 && _capabilities.firmwareInfo && snapshot()->firmware.empty()) {
            feature = "firmware"; auto value = _protocol->getFirmwareVersion();
            if (value.empty()) throw SonyException(SonyErrorCode::InvalidResponse, "Firmware not reported");
            std::lock_guard lock(_stateMutex); _state.firmware = value; _markSuccess(feature);
        } else if (step == 5 && _capabilities.speakToChat) {
            feature = "speakToChat"; auto value = _protocol->getSpeakToChat();
            std::lock_guard lock(_stateMutex); _state.speakToChat = value; _markSuccess(feature);
        } else if (step == 6 && _capabilities.adaptiveVolume) {
            feature = "adaptiveVolume"; auto value = _protocol->getAdaptiveVolume();
            std::lock_guard lock(_stateMutex); _state.adaptiveVolume = value; _markSuccess(feature);
        } else if (step == 7 && _capabilities.autoPowerOff) {
            feature = "autoPowerOff"; _readAutoPowerOff();
        }
    } catch (const SonyException& ex) { if (!feature.empty()) _markError(feature, ex); }
    _dispatcher.dispatch(protocol::DeviceStateChanged{snapshot()});
}
} // namespace sony::core
