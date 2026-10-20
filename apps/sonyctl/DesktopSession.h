#pragma once

#include "sony/core/IDeviceService.h"
#include "sony/core/IpcProtocol.h"
#include <functional>

namespace sony::core {

// Owned by one executor. Notifications only signal that the cached snapshot
// changed; they must never call back into DeviceService from the reader thread.
class DesktopSession {
public:
    explicit DesktopSession(IDeviceService& service, std::function<void(std::string, std::string)> changed = {});
    ~DesktopSession();
    IpcResponse execute(const std::string& command, const std::string& address);
    void tick();
private:
    void unsubscribe();
    void subscribe(const std::string& address);
    IDeviceService& _service;
    std::function<void(std::string, std::string)> _changed;
    SonyDevice* _subscribed{nullptr};
    protocol::DeviceEventDispatcher::SubscriptionId _subscription{0};
    std::string _lastConnectionState;
    bool _lastConnected{false};
    std::string _lastAddress;
};

int runDesktopSession(IDeviceService& service);
}
