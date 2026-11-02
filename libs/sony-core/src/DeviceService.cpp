#include "sony/core/DeviceService.h"
#include "sony/protocol/DeviceProfileRegistry.h"
#include "sony/transport/Logger.h"
#include "sony/transport/SonyDeviceFilter.h"
#include <algorithm>
#include <cctype>

namespace sony::core {
namespace {
constexpr auto SettingsStepInterval = std::chrono::seconds(5);
constexpr auto ShutdownQuietPeriod = std::chrono::seconds(5);
constexpr auto PowerOnPollInterval = std::chrono::seconds(1);

std::string normalizeAddress(std::string address) {
    for (auto& character : address) {
        if (character == '-') {
            character = ':';
        } else {
            character = static_cast<char>(
                std::toupper(static_cast<unsigned char>(character)));
        }
    }
    return address;
}

bool isDeviceConnected(const std::unique_ptr<SonyDevice>& device) noexcept {
    return device && device->isConnected();
}
}

DeviceService::DeviceService(
    std::shared_ptr<transport::ITransport> transport,
    std::shared_ptr<transport::IDeviceDiscovery> discovery, Now now, InitialRefresh initialRefresh,
    bool maintenanceEnabled, RecoveryPolicy recoveryPolicy)
    : _transport(std::move(transport)), _discovery(std::move(discovery)), _now(std::move(now)),
      _initialRefresh(initialRefresh), _maintenanceEnabled(maintenanceEnabled),
      _recoveryPolicy(recoveryPolicy) {}

DeviceService::~DeviceService() {
    close();
}

std::vector<DiscoveredDevice> DeviceService::discoverDevices() {
    if (!_discovery) {
        return {};
    }
    auto rawDevices = _discovery->discover();
    std::vector<DiscoveredDevice> result;
    result.reserve(rawDevices.size());
    for (const auto& dev : rawDevices) {
        auto profile = protocol::DeviceProfileRegistry::getProfileForDevice(dev.name);
        result.push_back(DiscoveredDevice{
            .address = dev.address.str(),
            .name = dev.name,
            .version = profile.protocol,
            .paired = dev.paired, .connected = dev.connected
        });
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.connected.value_or(false) != b.connected.value_or(false)) return a.connected.value_or(false);
        return a.address < b.address;
    });
    return result;
}

void DeviceService::connect(const transport::DeviceAddress& address, std::string_view name) {
    std::lock_guard lock(_mutex);
    _cancelled = false;
    if (!transport::addressOui(address.str()))
        throw std::invalid_argument("Expected a Bluetooth address in AA:BB:CC:DD:EE:FF format");
    _validateTarget(address);
    const auto addressString = normalizeAddress(address.str());
    if (_target != addressString) {
        _targetName.clear();
    }
    _target = addressString; _automatic = true; _waitingForPowerOn = false; _retrySeconds = 1;
    _intent = ConnectionIntent::Active;
    _pausedAddresses.erase(addressString);
    _resumeOnSystemConnectionAddresses.erase(addressString);
    if (!name.empty()) {
        _targetName = std::string(name);
    }
    try { _connect(transport::DeviceAddress(addressString), name); }
    catch (const std::exception& ex) {
        _wasConnected = false;
        if (_device) _device->disconnect();
        _lastError = ex.what(); _connectionState = "retrying";
        _nextAttempt = _now() + std::chrono::seconds(1); throw;
    }
}

void DeviceService::_validateTarget(const transport::DeviceAddress& address) {
    // Test and simulated transports may not expose discovery. Real platform
    // transports do, and their discovery result is the trust boundary for
    // explicit address connections.
    if (!_discovery) return;

    const auto target = normalizeAddress(address.str());
    const auto candidates = discoverDevices();
    const auto found = std::find_if(candidates.begin(), candidates.end(),
        [&](const auto& candidate) {
            return normalizeAddress(candidate.address) == target;
        });
    if (found == candidates.end()) {
        throw SonyException(
            SonyErrorCode::TransportFailure,
            "Selected Bluetooth device is not present in the paired discovery results");
    }
    if (found->paired.has_value() && !*found->paired) {
        throw SonyException(
            SonyErrorCode::TransportFailure,
            "Selected Bluetooth device is not paired");
    }
    _systemConnected = found->connected;
}

void DeviceService::_connect(const transport::DeviceAddress& address, std::string_view name) {
    if (_cancelled) throw SonyException(SonyErrorCode::Disconnected, "Connection cancelled");
    _wasConnected = false;
    _selected = normalizeAddress(address.str()); _connectionState = "connecting";
    const auto effectiveName = name.empty() ? std::string_view(_targetName) : name;
    if (!_device) {
        // The version passed here is provisional; SonyDevice::connect() resolves
        // it from the device profile. Start from V1 so that a failure to resolve
        // can never leave a legacy device on the V2 command set.
        _device = std::make_unique<SonyDevice>(_transport, SonyProtocolVersion::V1);
        _device->events().onBatteryChanged([this](const auto&) {
            _batteryDeadlineTicks.store((_now() + std::chrono::seconds(30)).time_since_epoch().count());
        });
    }
    _device->connect(address, effectiveName, _initialRefresh);
    if (!_device->isConnected()) throw SonyException(SonyErrorCode::Disconnected, "Bluetooth link closed during initialization");
    _wasConnected = true; _waitingForPowerOn = false;
    _connectionState = "connected"; _lastError.clear(); _retrySeconds = 1;
    _nextSettings = _now() + std::chrono::seconds(5);
    _nextBattery = _now() + std::chrono::seconds(30);
    _batteryDeadlineTicks = 0;
    _systemObservationAfter = _now() + std::chrono::seconds(5);
    _intent = ConnectionIntent::Active;
    _device->beforeRequest([this] { serviceBattery(); });
    if (!effectiveName.empty()) {
        _targetName = std::string(effectiveName);
    }
    Logger::info(LogCategory::Device, "Connected to " + std::string(effectiveName));
}

void DeviceService::disconnect() noexcept {
    std::lock_guard lock(_mutex);
    _pause(ConnectionIntent::PausedByUser);
    // Cancellation stops the old request, not passive observation while paused.
    _cancelled = false;
}

void DeviceService::_pause(ConnectionIntent intent) noexcept {
    _intent = intent;
    if (!_target.empty()) _pausedAddresses.insert(_target);
    if (!_target.empty() && (intent == ConnectionIntent::PausedAfterSystemDisconnect ||
                            _systemConnected == false))
        _resumeOnSystemConnectionAddresses.insert(_target);
    else
        _resumeOnSystemConnectionAddresses.erase(_target);
    _automatic = false;
    _waitingForPowerOn = false;
    _wasConnected = false;
    _connectionState = intent == ConnectionIntent::PausedByUser
        ? "manually_disconnected" : "paused_after_system_disconnect";
    if (_device) _device->disconnect();
}

void DeviceService::cancelPendingIo() noexcept {
    _cancelled = true;
    if (_transport) _transport->cancelPendingIo();
}

void DeviceService::close() noexcept {
    std::lock_guard lock(_mutex);
    _automatic = false;
    _wasConnected = false;
    if (_device) {
        _device->disconnect();
    }
}

void DeviceService::powerOff() {
    std::lock_guard lock(_mutex);
    validateSelectedTarget();
    _device->powerOff();
    _automatic = true;
    _waitingForPowerOn = true;
    _intent = ConnectionIntent::WaitingForPowerOn;
    _pausedAddresses.erase(_target);
    _resumeOnSystemConnectionAddresses.erase(_target);
    _wasConnected = false;
    _retrySeconds = 1;
    _nextAttempt = _now() + ShutdownQuietPeriod;
    _connectionState = "waiting_for_power_on";
    _lastError.clear();
}

bool DeviceService::isConnected() const noexcept {
    std::lock_guard lock(_mutex);
    return _device && _device->isConnected();
}

SonyDevice* DeviceService::activeDevice() noexcept {
    std::lock_guard lock(_mutex);
    return _device.get();
}

protocol::DeviceStateSnapshot DeviceService::snapshot() const {
    std::lock_guard lock(_mutex);
    if (_device) {
        return _device->snapshot();
    }
    return std::make_shared<const protocol::DeviceState>();
}

void DeviceService::startAutoConnect(std::string address) {
    std::lock_guard lock(_mutex);
    if (!address.empty() && !transport::addressOui(address))
        throw std::invalid_argument("Expected a Bluetooth address in AA:BB:CC:DD:EE:FF format");
    address = normalizeAddress(std::move(address));
    if (_target != address) {
        if (_device) _device->disconnect();
        _selected.clear();
        _wasConnected = false;
        _targetName.clear();
    }
    _target = std::move(address);
    _selected = _target;
    _automatic = !_target.empty(); _waitingForPowerOn = false; _retrySeconds = 1;
    _intent = ConnectionIntent::Active;
    if (_pausedAddresses.contains(_target)) {
        _automatic = false;
        _intent = ConnectionIntent::PausedByUser;
        _connectionState = "manually_disconnected";
        return;
    }
    _lastError.clear();
    if (!_automatic) {
        _connectionState = "disconnected";
        return;
    }
    _nextAttempt = _now(); _connectionState = "searching";
}
void DeviceService::restoreConnectionPreferences(const ConnectionPreferences& preferences) {
    std::lock_guard lock(_mutex);
    _pausedAddresses.clear();
    for (const auto& address : preferences.pausedAddresses)
        if (transport::addressOui(address)) _pausedAddresses.insert(normalizeAddress(address));
    _resumeOnSystemConnectionAddresses.clear();
    for (const auto& address : preferences.resumeOnSystemConnectionAddresses) {
        const auto normalized = normalizeAddress(address);
        if (_pausedAddresses.contains(normalized))
            _resumeOnSystemConnectionAddresses.insert(normalized);
    }
    startAutoConnect(preferences.selectedAddress);
    if (!_target.empty() && !_pausedAddresses.contains(_target) &&
        normalizeAddress(preferences.waitingForPowerOnAddress) == _target) {
        _waitingForPowerOn = true;
        _intent = ConnectionIntent::WaitingForPowerOn;
        _connectionState = "waiting_for_power_on";
        _nextAttempt = _now() + ShutdownQuietPeriod;
    }
}
ConnectionPreferences DeviceService::connectionPreferences() const {
    std::lock_guard lock(_mutex);
    return {_target, {_pausedAddresses.begin(), _pausedAddresses.end()},
        _waitingForPowerOn ? _target : std::string{},
        {_resumeOnSystemConnectionAddresses.begin(), _resumeOnSystemConnectionAddresses.end()}};
}
ConnectionIntent DeviceService::connectionIntent() const {
    std::lock_guard lock(_mutex);
    return _intent;
}
std::optional<bool> DeviceService::systemConnected() const {
    std::lock_guard lock(_mutex);
    return _systemConnected;
}
bool DeviceService::_observeSystemConnection() {
    _systemConnected.reset();
    try {
        for (const auto& candidate : discoverDevices()) {
            if (normalizeAddress(candidate.address) == _target) {
                _systemConnected = candidate.connected;
                break;
            }
        }
    } catch (const std::exception& ex) {
        _lastError = ex.what();
    }
    return _systemConnected.value_or(false);
}
bool DeviceService::isLocalPeer(std::string_view address) {
    if (_recoveryPolicy != RecoveryPolicy::RequireSystemConnection || !_discovery) return false;
    for (const auto& local : _discovery->localAddresses())
        if (normalizeAddress(local) == normalizeAddress(std::string(address))) return true;
    return false;
}

void DeviceService::serviceBattery() {
    std::lock_guard lock(_mutex);
    if (!_maintenanceEnabled || _batteryInProgress || !isDeviceConnected(_device)) return;
    if (const auto ticks = _batteryDeadlineTicks.exchange(0); ticks != 0)
        _nextBattery = Clock::time_point(Clock::duration(ticks));
    if (_now() >= _nextBattery) {
        _batteryInProgress = true;
        try { _device->refreshBattery(); }
        catch (...) { _batteryInProgress = false; throw; }
        _batteryInProgress = false;
        _nextBattery = _now() + std::chrono::seconds(30);
    }
}
void DeviceService::validateSelectedTarget(std::string_view expectedAddress) {
    std::lock_guard lock(_mutex);
    if (!isDeviceConnected(_device) || _selected.empty() || _target.empty() ||
        normalizeAddress(_selected) != normalizeAddress(_target) ||
        (!expectedAddress.empty() && normalizeAddress(std::string(expectedAddress)) != normalizeAddress(_selected)))
        throw SonyException(SonyErrorCode::Disconnected, "Selected headphones are not connected");
}
std::string DeviceService::connectionState() const {
    std::lock_guard lock(_mutex);
    return _connectionState == "connected" && !isDeviceConnected(_device)
        ? "retrying"
        : _connectionState;
}
std::string DeviceService::selectedAddress() const { std::lock_guard lock(_mutex); return _selected; }
std::string DeviceService::lastError() const { std::lock_guard lock(_mutex); return _lastError; }
void DeviceService::tick() {
    std::lock_guard lock(_mutex);
    if (_cancelled) return;
    if (_recoveryPolicy == RecoveryPolicy::RequireSystemConnection && !_target.empty()) {
        const bool previouslyConnected = _systemConnected.value_or(false);
        const bool systemUp = _observeSystemConnection();
        if (_intent == ConnectionIntent::PausedByUser ||
            _intent == ConnectionIntent::PausedAfterSystemDisconnect) {
            if (_systemConnected == false) _resumeOnSystemConnectionAddresses.insert(_target);
            if (!systemUp || !_resumeOnSystemConnectionAddresses.contains(_target)) return;
            // Only a positively observed system connection authorizes control
            // recovery. A manual pause on an unchanged link stays effective.
            _pausedAddresses.erase(_target);
            _resumeOnSystemConnectionAddresses.erase(_target);
            _intent = ConnectionIntent::Active;
            _automatic = true;
            _retrySeconds = 1;
            _nextAttempt = _now();
            _lastError.clear();
        }
        if (!_waitingForPowerOn) {
            const bool controlUp = isDeviceConnected(_device);
            const bool confirmedDisconnect = _systemConnected == false &&
                (previouslyConnected || (_wasConnected && _now() >= _systemObservationAfter));
            const bool unknownDisconnect = !_systemConnected.has_value() && _wasConnected && !controlUp;
            if (confirmedDisconnect || unknownDisconnect) {
                _lastError = "Bluetooth connection lost; software control paused";
                _pause(ConnectionIntent::PausedAfterSystemDisconnect);
                return;
            }
            // An unavailable startup link is not a disconnect event. Unknown
            // discovery also cannot invalidate a control session still alive.
            if (!controlUp && !systemUp) {
                _connectionState = "waiting_for_system_connection";
                return;
            }
        }
    }
    if (isDeviceConnected(_device)) {
        if (!_maintenanceEnabled) return;
        serviceBattery();
        if (_now() >= _nextSettings) {
            // One inquiry per tick keeps maintenance from monopolizing requests.
            _device->refreshSettingsStep();
            _nextSettings = _now() + SettingsStepInterval;
        }
        return;
    }
    if (!_automatic || _target.empty()) return;
    if (_wasConnected) {
        _wasConnected = false;
        _device->disconnect();
        _lastError = "Bluetooth connection lost; reconnecting";
        _nextAttempt = _now(); _connectionState = "retrying";
        Logger::warn(LogCategory::Device, _lastError);
    }
    if (_now() < _nextAttempt) return;
    try {
        auto candidates = discoverDevices();
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
            [](const auto& candidate) {
                return candidate.paired.has_value() && !*candidate.paired;
            }), candidates.end());
        if (_waitingForPowerOn) {
            const auto poweredOffAddress = normalizeAddress(_selected);
            candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                [&](const auto& candidate) {
                    return normalizeAddress(candidate.address) != poweredOffAddress ||
                        (_recoveryPolicy == RecoveryPolicy::RequireSystemConnection
                            ? !candidate.connected.value_or(false)
                            : (candidate.connected.has_value() && !*candidate.connected));
                }), candidates.end());
            if (candidates.empty()) {
                // Observe the OS link without opening RFCOMM while the headset is off.
                _connectionState = "waiting_for_power_on";
                _nextAttempt = _now() + PowerOnPollInterval;
                return;
            }
        }
        bool targetUnavailable = false;
        if (!_target.empty()) {
            const auto target = normalizeAddress(_target);
            auto found = std::find_if(candidates.begin(), candidates.end(),
                [&](const auto& candidate) {
                    return normalizeAddress(candidate.address) == target;
                });
            if (found == candidates.end()) {
                targetUnavailable = true;
                _lastError =
                    "Selected Bluetooth device is not present in the paired discovery results";
                candidates.clear();
            } else {
                candidates = {*found};
            }
        }
        _connectionState = "searching";
        for (const auto& candidate : candidates) {
            if (_recoveryPolicy == RecoveryPolicy::RequireSystemConnection &&
                !candidate.connected.value_or(false)) continue;
            try { _connect(transport::DeviceAddress(candidate.address), candidate.name); return; }
            catch (const std::exception& ex) { _lastError = ex.what(); if (_device) _device->disconnect(); }
        }
        if (candidates.empty() && !targetUnavailable)
            _lastError = "No paired Sony device found";
    } catch (const std::exception& ex) { _lastError = ex.what(); }
    _connectionState = "retrying";
    Logger::warn(LogCategory::Device, _lastError + "; retry in " + std::to_string(_retrySeconds) + "s");
    _nextAttempt = _now() + std::chrono::seconds(_retrySeconds);
    _retrySeconds = std::min(30u, _retrySeconds * 2);
}
} // namespace sony::core
