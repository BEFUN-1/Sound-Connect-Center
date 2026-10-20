#pragma once
#include <stdio.h>
#include "../IBluetoothConnector.h"
#include "IOBluetooth/IOBluetooth.h"
#include "Constants.h"
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

struct MacOSBluetoothConnectionState;

class MacOSBluetoothConnector final : public IBluetoothConnector
{
public:
    MacOSBluetoothConnector();
    ~MacOSBluetoothConnector();
    virtual void connect(const std::string& addrStr) noexcept(false);
    virtual int send(char* buf, size_t length) noexcept(false);
    virtual int recv(char* buf, size_t length) noexcept(false);
    virtual void cancelPendingIo() noexcept;
    virtual void disconnect() noexcept;
    virtual bool isConnected() noexcept;
    virtual void closeConnection();
    virtual SonyProtocolVersion getProtocolVersion() noexcept;

    virtual std::vector<BluetoothDevice> getConnectedDevices() noexcept(false);

private:
    static void connectToMac(const std::shared_ptr<MacOSBluetoothConnectionState>& state) noexcept;
    static void cancel(const std::shared_ptr<MacOSBluetoothConnectionState>& state) noexcept;
    static bool waitForThread(const std::shared_ptr<MacOSBluetoothConnectionState>& state,
                              std::chrono::milliseconds timeout);
    static bool waitForWrite(const std::shared_ptr<MacOSBluetoothConnectionState>& state,
                             std::chrono::milliseconds timeout);
    [[nodiscard]] std::shared_ptr<MacOSBluetoothConnectionState> currentState() const noexcept;

    mutable std::mutex stateMutex;
    std::shared_ptr<MacOSBluetoothConnectionState> state;
    std::thread connectThread;
};
