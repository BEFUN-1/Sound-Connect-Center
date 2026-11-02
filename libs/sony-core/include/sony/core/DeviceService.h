#pragma once

#include "IDeviceService.h"
#include "sony/transport/IDeviceDiscovery.h"
#include <mutex>
#include <chrono>
#include <functional>
#include <set>

namespace sony::core {

class DeviceService : public IDeviceService {
public:
    enum class RecoveryPolicy { Legacy, RequireSystemConnection };
    using Clock = std::chrono::steady_clock;
    using Now = std::function<Clock::time_point()>;
    explicit DeviceService(
        std::shared_ptr<transport::ITransport> transport,
        std::shared_ptr<transport::IDeviceDiscovery> discovery = nullptr,
        Now now = [] { return Clock::now(); },
        InitialRefresh initialRefresh = InitialRefresh::All,
        bool maintenanceEnabled = true,
        RecoveryPolicy recoveryPolicy = RecoveryPolicy::Legacy);
    ~DeviceService() override;

    void tick() override;
    void startAutoConnect(std::string address) override;
    void restoreConnectionPreferences(const ConnectionPreferences& preferences) override;
    ConnectionPreferences connectionPreferences() const override;
    ConnectionIntent connectionIntent() const override;
    std::optional<bool> systemConnected() const override;
    void cancelPendingIo() noexcept override;
    void close() noexcept override;
    void serviceBattery() override;
    bool isLocalPeer(std::string_view address) override;
    void validateSelectedTarget(std::string_view expectedAddress = {}) override;
    std::string connectionState() const override;
    std::string selectedAddress() const override;
    std::string lastError() const override;
    std::vector<DiscoveredDevice> discoverDevices() override;
    void connect(const transport::DeviceAddress& address, std::string_view name = "") override;
    void disconnect() noexcept override;
    void powerOff() override;
    [[nodiscard]] bool isConnected() const noexcept override;

    [[nodiscard]] SonyDevice* activeDevice() noexcept override;
    [[nodiscard]] protocol::DeviceStateSnapshot snapshot() const override;

private:
    std::shared_ptr<transport::ITransport> _transport;
    std::shared_ptr<transport::IDeviceDiscovery> _discovery;
    std::unique_ptr<SonyDevice> _device;
    mutable std::recursive_mutex _mutex;
    Now _now;
    InitialRefresh _initialRefresh;
    bool _maintenanceEnabled;
    RecoveryPolicy _recoveryPolicy;
    ConnectionIntent _intent{ConnectionIntent::Active};
    std::set<std::string> _pausedAddresses;
    std::set<std::string> _resumeOnSystemConnectionAddresses;
    std::optional<bool> _systemConnected;
    Clock::time_point _systemObservationAfter{};
    std::atomic<Clock::duration::rep> _batteryDeadlineTicks{0};
    bool _batteryInProgress{false};
    std::atomic<bool> _cancelled{false};
    bool _automatic{false};
    bool _waitingForPowerOn{false};
    bool _wasConnected{false};
    unsigned _retrySeconds{1};
    Clock::time_point _nextAttempt{}, _nextSettings{}, _nextBattery{};
    std::string _target, _targetName, _selected, _connectionState{"disconnected"}, _lastError;
    void _connect(const transport::DeviceAddress& address, std::string_view name);
    void _validateTarget(const transport::DeviceAddress& address);
    void _pause(ConnectionIntent intent) noexcept;
    bool _observeSystemConnection();

};

} // namespace sony::core
