#include "DesktopSession.h"
#include "sony/core/JsonProtocol.h"
#include "sony/protocol/EqualizerPresets.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

namespace sony::core {
using Json = JsonProtocol::Json;
namespace {
std::string normalized(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::toupper(c); });
    return value;
}

bool retryableStateCommand(IpcCommandType type) {
    switch (type) {
    case IpcCommandType::Anc:
    case IpcCommandType::Ambient:
    case IpcCommandType::EqPreset:
    case IpcCommandType::EqCustom:
    case IpcCommandType::Dsee:
    case IpcCommandType::AutoPowerOff:
        return true;
    default:
        return false;
    }
}

Json snapshotFromState(const protocol::DeviceStateSnapshot& state, const std::string& address,
                       const std::string& name, const std::string& protocolName,
                       const protocol::DeviceCapabilities& c) {
    Json features = Json::object();
    for (const auto& [name, status] : state->features) {
        features[name] = {{"availability", status.availability},
            {"lastSuccessMs", status.lastSuccessMs}, {"error", status.error}};
    }
    Json devices = Json::array();
    for (const auto& d : state->multipoint.devices) devices.push_back({
        {"address", d.address}, {"name", d.name}, {"connectionIndex", d.connectionIndex},
        {"deviceClass", d.deviceClass}, {"connected", d.connected}, {"playing", d.playing}});
    const auto optional = [](const auto& value) { return value ? Json(*value) : Json(nullptr); };
    return {
        {"connected", true}, {"connectionState", "connected"},
        {"address", address}, {"name", name},
        {"lastError", ""}, {"protocol", protocolName},
        {"capabilities", {{"anc", c.noiseCancelling}, {"ambient", c.ambientSound}, {"focusOnVoice", c.focusOnVoice},
            {"equalizer", c.equalizer}, {"clearBass", c.clearBass}, {"tenBandEqualizer", c.tenBandEqualizer},
            {"dsee", c.dsee}, {"battery", c.battery}, {"speakToChat", c.speakToChat},
            {"adaptiveVolume", c.adaptiveVolume}, {"autoPowerOff", c.autoPowerOff}, {"powerOff", c.powerOff}, {"multipoint", c.multipoint}}},
        {"features", features},
        {"multipoint", {{"supported", state->multipoint.supported}, {"sourceSwitchSupported", state->multipoint.sourceSwitchSupported},
            {"sourceKeepSupported", state->multipoint.sourceKeepSupported}, {"inquiredType", state->multipoint.inquiredType},
            {"maxConnected", state->multipoint.maxConnected}, {"sourceKeeping", state->multipoint.sourceKeeping},
            {"playbackIndex", state->multipoint.playbackIndex}, {"devices", devices}, {"error", state->multipoint.error}}},
        {"battery", {{"main", optional(state->battery.main)}, {"left", optional(state->battery.left)},
            {"right", optional(state->battery.right)}, {"case", optional(state->battery.caseBattery)}, {"charging", state->battery.charging}}},
        {"noiseControl", {{"mode", state->noiseControl.mode == protocol::NoiseControlMode::NoiseCancelling ? "cancelling" :
            state->noiseControl.mode == protocol::NoiseControlMode::Ambient ? "ambient" : "off"},
            {"ambientLevel", state->noiseControl.ambientLevel}, {"focusOnVoice", state->noiseControl.focusOnVoice}}},
        {"equalizer", {{"preset", state->equalizer.preset}, {"presetName", protocol::equalizerPresetName(state->equalizer.preset)},
            {"clearBass", state->equalizer.clearBass}, {"bands", state->equalizer.bands}}},
        {"dsee", state->dsee}, {"speakToChat", state->speakToChat}, {"adaptiveVolume", state->adaptiveVolume},
        {"autoPowerOff", state->autoPowerOff}, {"codec", state->codec.empty() ? "Unknown" : state->codec},
        {"firmware", state->firmware.empty() ? "Unknown" : state->firmware}
    };
}
}
DesktopSession::DesktopSession(IDeviceService& service, std::function<void(std::string, std::string)> changed)
    : _service(service), _changed(std::move(changed)) {}
DesktopSession::~DesktopSession() {
    // Join the reader while captured notification state is still alive.
    _service.disconnect();
    unsubscribe();
}
void DesktopSession::unsubscribe() {
    if (_subscribed) _subscribed->events().removeListener(_subscription);
    _subscribed = nullptr;
}
void DesktopSession::subscribe(const std::string& address) {
    auto* device = _service.activeDevice();
    if (!device) return;
    if (_subscribed == device) return;
    unsubscribe();
    _subscribed = device;
    if (!_changed) return;
    const auto name = device->name();
    const auto protocolName = std::string(protocol::to_string(device->protocolVersion()));
    const auto capabilities = device->capabilities();
    _subscription = _subscribed->events().onStateChanged(
        [this, address, name, protocolName, capabilities](const auto& event) {
            _changed(address, snapshotFromState(event.state, address, name, protocolName, capabilities)
                .dump(-1, ' ', false, Json::error_handler_t::replace));
        });
}
IpcResponse DesktopSession::execute(const std::string& command, const std::string& requestedAddress) {
    try {
        auto cmd = IpcProtocol::parseCommand(command);
        if (cmd.type == IpcCommandType::Devices) return IpcProtocol::execute(cmd, _service);
        const bool refresh = command.rfind("refresh ", 0) == 0;
        if (command != "snapshot" && !refresh && cmd.type == IpcCommandType::Unknown)
            throw std::invalid_argument("Unknown desktop command");
        if (command != "snapshot") {
            const auto target = normalized(requestedAddress);
            if (!_service.isConnected() || (!target.empty() && target != normalized(_service.selectedAddress()))) {
                // Clear the old generation before connecting another headset.
                _service.disconnect();
                unsubscribe();
                const auto devices = _service.discoverDevices();
                const auto found = std::find_if(devices.begin(), devices.end(), [&](const auto& d) {
                    return target.empty() || normalized(d.address) == target;
                });
                if (found == devices.end()) throw std::runtime_error("Selected headphones are unavailable");
                _service.connect(transport::DeviceAddress(found->address), found->name);
                subscribe(found->address);
                _lastConnected = true;
                _lastAddress = found->address;
                _lastConnectionState = _service.connectionState();
            }
        }
        auto* device = _service.activeDevice();
        if (refresh) {
            if (!device || !_service.isConnected()) throw std::runtime_error("Headphones disconnected");
            if (command == "refresh battery") device->refreshBattery();
            else if (command == "refresh noise") device->refreshNoiseControl();
            else if (command == "refresh eq") device->refreshEqualizer();
            else if (command == "refresh dsee") device->refreshDsee();
            else if (command == "refresh multipoint") device->refreshMultipoint();
            else throw std::invalid_argument("Unknown refresh feature");
            if (!_service.isConnected())
                throw std::runtime_error("Bluetooth link closed while refreshing device state");
        } else if (command != "snapshot") {
            // Only the mode's dependent state is needed before a write.
            if (cmd.type == IpcCommandType::Anc || cmd.type == IpcCommandType::Ambient) {
                const auto features = device->snapshot()->features;
                const auto nc = features.find("noiseControl");
                if (nc == features.end() || nc->second.availability != "valid") {
                    device->refreshNoiseControl();
                    if (!_service.isConnected()) {
                        tick();
                        if (!_service.isConnected())
                            throw std::runtime_error("Bluetooth link is unavailable after reconnecting for the requested operation");
                        device = _service.activeDevice();
                    }
                }
            }
            if (cmd.type == IpcCommandType::Dashboard || cmd.type == IpcCommandType::Info) device->refreshAll();
            if (cmd.type == IpcCommandType::EqGet) device->refreshEqualizer();
            auto response = IpcProtocol::execute(cmd, _service);
            if (!response.success && !_service.isConnected() && retryableStateCommand(cmd.type)) {
                tick();
                if (_service.isConnected()) response = IpcProtocol::execute(cmd, _service);
            }
            if (!response.success) return response;
        }
        return {true, {}, JsonProtocol::snapshot(_service).dump(-1, ' ', false, Json::error_handler_t::replace)};
    } catch (const std::exception& ex) {
        return {false, ex.what(), {}};
    }
}
void DesktopSession::tick() {
    const bool connectedBeforeTick = _service.isConnected();
    _service.tick();
    const auto connected = _service.isConnected();
    const auto address = _service.selectedAddress();
    const auto state = _service.connectionState();
    if (connected) subscribe(address);
    if (connected != _lastConnected || address != _lastAddress || state != _lastConnectionState ||
        (!connectedBeforeTick && connected)) {
        _lastConnected = connected;
        _lastAddress = address;
        _lastConnectionState = state;
        if (_changed) {
            auto snapshot = JsonProtocol::snapshot(_service).dump(-1, ' ', false, Json::error_handler_t::replace);
            _changed(address, std::move(snapshot));
        }
    }
}
int runDesktopSession(IDeviceService& service) {
    std::mutex outputMutex;
    auto write = [&](const Json& message) {
        std::lock_guard lock(outputMutex);
        std::cout << message.dump(-1, ' ', false, Json::error_handler_t::replace) << std::endl;
    };
    DesktopSession session(service, [&](std::string address, std::string snapshot) {
        write(Json{{"event", "changed"}, {"address", address}, {"data", snapshot}});
    });
    struct InputQueue {
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<std::string> lines;
        bool closed{false};
    } input;
    std::thread reader([&] {
        std::string line;
        while (std::getline(std::cin, line)) {
            std::unique_lock lock(input.mutex);
            input.ready.wait(lock, [&] { return input.lines.size() < 16; });
            input.lines.push_back(std::move(line));
            lock.unlock();
            input.ready.notify_one();
            line.clear();
        }
        {
            std::lock_guard lock(input.mutex);
            input.closed = true;
        }
        input.ready.notify_one();
    });
    bool inputClosed = false;
    while (true) {
        std::string line;
        {
            std::unique_lock lock(input.mutex);
            input.ready.wait_for(lock, std::chrono::milliseconds(500), [&] { return !input.lines.empty() || input.closed; });
            if (!input.lines.empty()) {
                line = std::move(input.lines.front());
                input.lines.pop_front();
                input.ready.notify_one();
            } else if (input.closed) inputClosed = true;
        }
        if (inputClosed) break;
        if (line.empty()) { session.tick(); continue; }
        Json id = nullptr;
        try {
            const auto request = Json::parse(line, [](int depth, Json::parse_event_t, Json&) {
                if (depth > 16) throw std::invalid_argument("Request nesting exceeds limit");
                return true;
            });
            id = request.at("id");
            if (!id.is_number_integer()) throw std::invalid_argument("Request id must be an integer");
            const auto result = session.execute(request.at("command").get<std::string>(), request.value("address", std::string{}));
            write(Json{{"id", id}, {"success", result.success}, {"message", result.message}, {"data", result.data}});
        } catch (const std::exception& ex) {
            write(Json{{"id", id}, {"success", false}, {"message", ex.what()}, {"data", ""}});
        }
    }
    reader.join();
    return std::cin.eof() ? 0 : 1;
}
}
