#pragma once

#include "SonyDevice.h"
#include "sony/transport/ITransport.h"

#include <memory>
#include <string>
#include <vector>

namespace sony::core {

enum class ConnectionIntent { Active, PausedByUser, PausedAfterSystemDisconnect, WaitingForPowerOn };
struct ConnectionPreferences {
    std::string selectedAddress;
    std::vector<std::string> pausedAddresses;
    std::string waitingForPowerOnAddress;
    std::vector<std::string> resumeOnSystemConnectionAddresses;
};

struct DiscoveredDevice {
    std::string address;
    std::string name;
    SonyProtocolVersion version{SonyProtocolVersion::V1};
    std::optional<bool> paired;
    std::optional<bool> connected;
};

class IDeviceService {
public:
    virtual ~IDeviceService() = default;

    virtual void tick() {}
    virtual void startAutoConnect(std::string address) {}
    virtual void restoreConnectionPreferences(const ConnectionPreferences& preferences) {
        startAutoConnect(preferences.selectedAddress);
    }
    virtual ConnectionPreferences connectionPreferences() const { return {selectedAddress(), {}, {}}; }
    virtual ConnectionIntent connectionIntent() const { return ConnectionIntent::Active; }
    virtual std::optional<bool> systemConnected() const { return std::nullopt; }
    // May be called from another thread; it must not wait for service locks.
    virtual void cancelPendingIo() noexcept {}
    virtual void close() noexcept { disconnect(); }
    virtual void serviceBattery() {}
    virtual bool isLocalPeer(std::string_view address) { return false; }
    virtual void validateSelectedTarget(std::string_view expectedAddress = {}) {
        if (!isConnected() || selectedAddress().empty() ||
            (!expectedAddress.empty() && expectedAddress != selectedAddress()))
            throw SonyException(SonyErrorCode::Disconnected, "Selected headphones are not connected");
    }
    virtual std::string connectionState() const { return isConnected() ? "connected" : "disconnected"; }
    virtual std::string selectedAddress() const { return {}; }
    virtual std::string lastError() const { return {}; }
    virtual std::vector<DiscoveredDevice> discoverDevices() = 0;
    virtual void connect(const transport::DeviceAddress& address, std::string_view name = "") = 0;
    virtual void disconnect() noexcept = 0;
    virtual void powerOff() = 0;
    [[nodiscard]] virtual bool isConnected() const noexcept = 0;

    [[nodiscard]] virtual SonyDevice* activeDevice() noexcept = 0;
    [[nodiscard]] virtual protocol::DeviceStateSnapshot snapshot() const = 0;
};

} // namespace sony::core
