#include "MacOSBluetoothConnector.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

@class AsyncDeviceDelegate;

struct MacOSBluetoothConnectionState {
    std::string address;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> connected{false};
    std::atomic<bool> channelClosed{false};
    std::atomic<bool> receiveCancelled{false};
    std::atomic<SonyProtocolVersion> protocolVersion{SonyProtocolVersion::V1};

    std::mutex openMutex;
    std::condition_variable openChanged;
    bool openCallReturned{false};
    bool openCallbackReceived{false};
    bool openResultReady{false};
    IOReturn immediateOpenStatus{kIOReturnSuccess};
    IOReturn callbackOpenStatus{kIOReturnSuccess};
    IOReturn openStatus{kIOReturnSuccess};
    std::string openError;

    std::mutex deviceMutex;
    std::condition_variable deviceConnectionChanged;
    bool deviceConnectionComplete{false};
    IOReturn deviceConnectionStatus{kIOReturnSuccess};
    bool openingDevice{false};
    __strong IOBluetoothDevice* device = nil;
    __strong AsyncDeviceDelegate* deviceDelegate = nil;

    std::mutex channelMutex;
    __strong IOBluetoothRFCOMMChannel* channel = nil;

    std::mutex receiveMutex;
    std::condition_variable receiveChanged;
    std::deque<std::vector<unsigned char>> receivedBytes;
    size_t queuedReceiveBytes{0};

    std::mutex writeSerialMutex;
    std::mutex activeWriteMutex;
    std::weak_ptr<struct MacOSBluetoothWriteWait> activeWrite;
    std::mutex writeLifecycleMutex;
    std::condition_variable writeLifecycleChanged;
    bool writeInFlight{false};

    std::mutex threadMutex;
    std::condition_variable threadChanged;
    bool threadFinished{false};
};

struct MacOSBluetoothWriteWait {
    std::mutex mutex;
    std::condition_variable changed;
    bool completed{false};
    IOReturn status{kIOReturnSuccess};
};

@interface AsyncCommDelegate : NSObject <IOBluetoothRFCOMMChannelDelegate> {
@public
    std::weak_ptr<MacOSBluetoothConnectionState> connectionState;
}
@end

@interface AsyncDeviceDelegate : NSObject <IOBluetoothDeviceAsyncCallbacks> {
@public
    std::weak_ptr<MacOSBluetoothConnectionState> connectionState;
}
@end

namespace {
using ConnectionState = MacOSBluetoothConnectionState;

constexpr auto ConnectTimeout = std::chrono::seconds(12);
constexpr auto ConnectCleanupTimeout = std::chrono::seconds(1);
constexpr auto DisconnectTimeout = std::chrono::seconds(2);
constexpr auto WriteTimeout = std::chrono::seconds(2);
constexpr size_t MaxQueuedReceiveBytes = 1024 * 1024;

void markOpenFailure(const std::shared_ptr<ConnectionState>& state, IOReturn status,
                     std::string message) {
    {
        std::lock_guard lock(state->openMutex);
        if (!state->openResultReady) {
            state->openResultReady = true;
            state->openStatus = status;
            state->openError = std::move(message);
        }
    }
    state->connected = false;
    state->channelClosed = true;
    state->openChanged.notify_all();
    state->receiveChanged.notify_all();
}

void completeOpenIfReady(const std::shared_ptr<ConnectionState>& state) {
    bool completed = false;
    IOReturn status = kIOReturnSuccess;
    {
        std::lock_guard lock(state->openMutex);
        if (!state->openResultReady && state->openCallReturned) {
            if (state->immediateOpenStatus != kIOReturnSuccess) {
                state->openResultReady = true;
                state->openStatus = state->immediateOpenStatus;
                state->openError = "Could not open the RFCOMM channel (status " +
                    std::to_string(state->immediateOpenStatus) + ")";
            } else if (state->openCallbackReceived) {
                state->openResultReady = true;
                state->openStatus = state->callbackOpenStatus;
                if (state->callbackOpenStatus != kIOReturnSuccess) {
                    state->openError = "RFCOMM channel open failed (status " +
                        std::to_string(state->callbackOpenStatus) + ")";
                }
            }
        }
        completed = state->openResultReady;
        status = state->openStatus;
    }
    if (!completed) return;

    if (status == kIOReturnSuccess && !state->cancelled) {
        state->connected = true;
    } else {
        state->connected = false;
        state->channelClosed = true;
    }
    state->openChanged.notify_all();
    state->receiveChanged.notify_all();
}

IOBluetoothRFCOMMChannel* copyChannel(const std::shared_ptr<ConnectionState>& state) {
    if (!state) return nil;
    std::lock_guard lock(state->channelMutex);
    return state->channel;
}

void clearChannel(const std::shared_ptr<ConnectionState>& state,
                   IOBluetoothRFCOMMChannel* expected = nil) {
    if (!state) return;
    std::lock_guard lock(state->channelMutex);
    if (!expected || state->channel == expected) state->channel = nil;
}

void closeChannel(const std::shared_ptr<ConnectionState>& state,
                  IOBluetoothRFCOMMChannel* channel = nil) {
    if (!state) return;
    if (!channel) channel = copyChannel(state);
    if (channel) {
        [channel setDelegate:nil];
        if (channel.isOpen) [channel closeChannel];
        clearChannel(state, channel);
    }
}

void markThreadFinished(const std::shared_ptr<ConnectionState>& state) {
    {
        std::lock_guard lock(state->threadMutex);
        state->threadFinished = true;
    }
    state->threadChanged.notify_all();
}

struct ThreadFinishedGuard {
    std::shared_ptr<ConnectionState> state;
    ~ThreadFinishedGuard() { markThreadFinished(state); }
};

struct WriteFinishedGuard {
    std::shared_ptr<ConnectionState> state;
    ~WriteFinishedGuard() {
        {
            std::lock_guard lock(state->writeLifecycleMutex);
            state->writeInFlight = false;
        }
        state->writeLifecycleChanged.notify_all();
    }
};
}

@implementation AsyncCommDelegate

- (void)rfcommChannelClosed:(IOBluetoothRFCOMMChannel *)rfcommChannel {
#ifdef SHC_DEBUG_PROTOCOL
    fprintf(stderr, "[connect] rfcommChannelClosed\n");
#endif
    auto state = connectionState.lock();
    if (!state) return;
    state->connected = false;
    state->channelClosed = true;
    clearChannel(state, rfcommChannel);
    markOpenFailure(state, kIOReturnNotOpen, "RFCOMM channel closed while connecting");
    state->receiveChanged.notify_all();
}

-(void)rfcommChannelOpenComplete:(IOBluetoothRFCOMMChannel *)rfcommChannel status:(IOReturn)error {
#ifdef SHC_DEBUG_PROTOCOL
    fprintf(stderr, "[connect] rfcommChannelOpenComplete status=0x%x\n", error);
#endif
    auto state = connectionState.lock();
    if (!state) return;
    {
        std::lock_guard lock(state->openMutex);
        state->openCallbackReceived = true;
        state->callbackOpenStatus = error;
    }
    if (error != kIOReturnSuccess || state->cancelled) {
        state->connected = false;
        state->channelClosed = true;
        if (error != kIOReturnSuccess)
            clearChannel(state, rfcommChannel);
    }
    completeOpenIfReady(state);
    state->receiveChanged.notify_all();
}

-(void)rfcommChannelData:(IOBluetoothRFCOMMChannel *)rfcommChannel
                    data:(void *)dataPointer
                  length:(size_t)dataLength {
    auto state = connectionState.lock();
    if (!state || state->cancelled || state->receiveCancelled) return;
    std::lock_guard lock(state->receiveMutex);
    if (!dataLength) return;
    if (dataLength > MaxQueuedReceiveBytes - state->queuedReceiveBytes) {
        state->connected = false;
        state->channelClosed = true;
        state->receiveChanged.notify_all();
        return;
    }
    const auto* buffer = static_cast<unsigned char*>(dataPointer);
    state->receivedBytes.emplace_back(buffer, buffer + dataLength);
    state->queuedReceiveBytes += dataLength;
    state->receiveChanged.notify_one();
}

@end

@implementation AsyncDeviceDelegate
- (void)connectionComplete:(IOBluetoothDevice *)device status:(IOReturn)status {
    auto state = connectionState.lock();
    if (!state) return;
    AsyncDeviceDelegate* keepAlive = self;
    {
        std::lock_guard lock(state->deviceMutex);
        state->deviceConnectionComplete = true;
        state->deviceConnectionStatus = status;
        state->openingDevice = false;
        state->deviceDelegate = nil;
        state->device = nil;
    }
    state->deviceConnectionChanged.notify_all();
}
- (void)remoteNameRequestComplete:(IOBluetoothDevice *)device status:(IOReturn)status {}
- (void)sdpQueryComplete:(IOBluetoothDevice *)device status:(IOReturn)status {}
@end

#ifdef SHC_DEBUG_PROTOCOL
static void _debugHexDump(const char* label, const char* buf, size_t length) {
    fprintf(stderr, "[%s] ", label);
    for (size_t i = 0; i < length; i++) fprintf(stderr, "%02x ", (unsigned char)buf[i]);
    fprintf(stderr, "\n");
}
#endif

MacOSBluetoothConnector::MacOSBluetoothConnector() = default;

MacOSBluetoothConnector::~MacOSBluetoothConnector() {
    disconnect();
}

std::shared_ptr<MacOSBluetoothConnectionState> MacOSBluetoothConnector::currentState() const noexcept {
    std::lock_guard lock(stateMutex);
    return state;
}

bool MacOSBluetoothConnector::waitForThread(
    const std::shared_ptr<MacOSBluetoothConnectionState>& connection,
    std::chrono::milliseconds timeout) {
    if (!connection) return true;
    std::unique_lock lock(connection->threadMutex);
    return connection->threadChanged.wait_for(lock, timeout, [&] { return connection->threadFinished; });
}

bool MacOSBluetoothConnector::waitForWrite(
    const std::shared_ptr<MacOSBluetoothConnectionState>& connection,
    std::chrono::milliseconds timeout) {
    if (!connection) return true;
    std::unique_lock lock(connection->writeLifecycleMutex);
    return connection->writeLifecycleChanged.wait_for(lock, timeout, [&] { return !connection->writeInFlight; });
}

void MacOSBluetoothConnector::cancel(const std::shared_ptr<MacOSBluetoothConnectionState>& connection) noexcept {
    if (!connection) return;
    connection->cancelled = true;
    connection->connected = false;
    connection->channelClosed = true;
    connection->receiveCancelled = true;
    markOpenFailure(connection, kIOReturnAborted, "Bluetooth operation cancelled");
    connection->openChanged.notify_all();
    connection->receiveChanged.notify_all();

    IOBluetoothDevice* pendingDevice = nil;
    {
        std::lock_guard lock(connection->deviceMutex);
        if (connection->openingDevice) {
            pendingDevice = connection->device;
            connection->openingDevice = false;
        }
    }
    if (pendingDevice && ![pendingDevice isConnected]) [pendingDevice closeConnection];

    std::shared_ptr<MacOSBluetoothWriteWait> write;
    {
        std::lock_guard lock(connection->activeWriteMutex);
        write = connection->activeWrite.lock();
    }
    if (write) write->changed.notify_all();
    closeChannel(connection);
}

void MacOSBluetoothConnector::connect(const std::string& addrStr) {
    disconnect();
    auto previous = currentState();
    if (previous && (!waitForThread(previous, std::chrono::milliseconds(0)) ||
                     !waitForWrite(previous, std::chrono::milliseconds(0))))
        throw RecoverableException("Previous Bluetooth operation is still stopping", true);

    auto connection = std::make_shared<MacOSBluetoothConnectionState>();
    connection->address = addrStr;
    {
        std::lock_guard lock(stateMutex);
        state = connection;
    }

    connectThread = std::thread([connection] { connectToMac(connection); });
    std::unique_lock lock(connection->openMutex);
    const bool completed = connection->openChanged.wait_for(lock, ConnectTimeout, [&] {
        return connection->openResultReady || connection->cancelled.load();
    });
    const auto status = connection->openStatus;
    const auto error = connection->openError;
    lock.unlock();

    if (!completed) {
        cancel(connection);
        if (connectThread.joinable()) {
            if (waitForThread(connection, ConnectCleanupTimeout)) connectThread.join();
            else connectThread.detach();
        }
        throw RecoverableException("Timed out waiting for the headset RFCOMM channel", false);
    }
    if (status != kIOReturnSuccess || connection->cancelled || !connection->connected) {
        cancel(connection);
        if (connectThread.joinable()) {
            if (waitForThread(connection, ConnectCleanupTimeout)) connectThread.join();
            else connectThread.detach();
        }
        throw RecoverableException(error.empty() ? "Could not open the headset RFCOMM channel" : error, false);
    }
}

void MacOSBluetoothConnector::connectToMac(
    const std::shared_ptr<MacOSBluetoothConnectionState>& connection) noexcept {
    ThreadFinishedGuard finished{connection};
    try {
        @autoreleasepool {
            NSString* address = [NSString stringWithUTF8String:connection->address.c_str()];
            IOBluetoothDevice* device = address ? [IOBluetoothDevice deviceWithAddressString:address] : nil;
            if (!device) throw std::runtime_error("Could not find the paired Bluetooth device");
            if (connection->cancelled) return;

            if (![device isConnected]) {
                AsyncDeviceDelegate* deviceDelegate = [[AsyncDeviceDelegate alloc] init];
                deviceDelegate->connectionState = connection;
                {
                    std::lock_guard lock(connection->deviceMutex);
                    connection->device = device;
                    connection->deviceDelegate = deviceDelegate;
                    connection->openingDevice = true;
                    connection->deviceConnectionComplete = false;
                }
                IOReturn connectionStatus = [device openConnection:deviceDelegate];
                if (connectionStatus != kIOReturnSuccess)
                    throw std::runtime_error("Could not open the Bluetooth device connection (status " +
                        std::to_string(connectionStatus) + ")");
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
                while (!connection->cancelled) {
                    {
                        std::lock_guard lock(connection->deviceMutex);
                        if (connection->deviceConnectionComplete) {
                            if (connection->deviceConnectionStatus != kIOReturnSuccess &&
                                ![device isConnected])
                                throw std::runtime_error("Bluetooth baseband connection failed (status " +
                                    std::to_string(connection->deviceConnectionStatus) + ")");
                            break;
                        }
                    }
                    if ([device isConnected]) {
                        std::lock_guard lock(connection->deviceMutex);
                        connection->openingDevice = false;
                        break;
                    }
                    if (std::chrono::steady_clock::now() >= deadline)
                        throw std::runtime_error("Timed out opening the Bluetooth baseband connection");
                    @autoreleasepool {
                        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
                    }
                }
            }
            if (connection->cancelled) return;

            IOBluetoothSDPUUID* serviceUUIDV1 =
                [IOBluetoothSDPUUID uuidWithBytes:(void*)SERVICE_UUID_IN_BYTES length:16];
            IOBluetoothSDPServiceRecord* serviceRecord = [device getServiceRecordForUUID:serviceUUIDV1];
            SonyProtocolVersion protocolVersion = SonyProtocolVersion::V1;
#ifdef SHC_DEBUG_PROTOCOL
            fprintf(stderr, "[connect] v1 getServiceRecordForUUID -> %s\n", serviceRecord == nil ? "nil" : "found");
#endif
            if (serviceRecord == nil) {
                IOBluetoothSDPUUID* serviceUUIDV2 =
                    [IOBluetoothSDPUUID uuidWithBytes:(void*)SERVICE_UUID_V2_IN_BYTES length:16];
                serviceRecord = [device getServiceRecordForUUID:serviceUUIDV2];
                protocolVersion = SonyProtocolVersion::V2;
#ifdef SHC_DEBUG_PROTOCOL
                fprintf(stderr, "[connect] v2 getServiceRecordForUUID -> %s\n", serviceRecord == nil ? "nil" : "found");
#endif
            }
            if (serviceRecord == nil)
                throw std::runtime_error("Couldn't find the Sony service record on this device");

            UInt8 channelID = 0;
            IOReturn channelStatus = [serviceRecord getRFCOMMChannelID:&channelID];
            if (channelStatus != kIOReturnSuccess)
                throw std::runtime_error("Sony service record has no RFCOMM channel");
            if (connection->cancelled) return;

            IOBluetoothRFCOMMChannel* channel = [[IOBluetoothRFCOMMChannel alloc] init];
            if (!channel) throw std::runtime_error("Could not allocate an RFCOMM channel");
            AsyncCommDelegate* delegate = [[AsyncCommDelegate alloc] init];
            delegate->connectionState = connection;
            connection->protocolVersion = protocolVersion;

            IOReturn openStatus = [device openRFCOMMChannelAsync:&channel
                                                    withChannelID:channelID
                                                        delegate:delegate];
            {
                std::lock_guard lock(connection->channelMutex);
                connection->channel = channel;
            }
            {
                std::lock_guard lock(connection->openMutex);
                connection->openCallReturned = true;
                connection->immediateOpenStatus = openStatus;
            }
            completeOpenIfReady(connection);
#ifdef SHC_DEBUG_PROTOCOL
            fprintf(stderr, "[connect] openRFCOMMChannelAsync -> 0x%x\n", openStatus);
#endif
            if (openStatus != kIOReturnSuccess) return;
            if (connection->cancelled) {
                closeChannel(connection, channel);
                return;
            }

            while (!connection->cancelled && !connection->channelClosed) {
                @autoreleasepool {
                    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
                }
            }
            closeChannel(connection, channel);
        }
    } catch (const std::exception& ex) {
        markOpenFailure(connection, kIOReturnError, ex.what());
        closeChannel(connection);
    } catch (...) {
        markOpenFailure(connection, kIOReturnError, "Unexpected Bluetooth connection failure");
        closeChannel(connection);
    }
}

int MacOSBluetoothConnector::send(char* buf, size_t length) {
#ifdef SHC_DEBUG_PROTOCOL
    _debugHexDump("send", buf, length);
#endif
    auto connection = currentState();
    if (!connection || !connection->connected || connection->cancelled)
        throw RecoverableException("Bluetooth channel is not connected", true);
    if (!length) return 0;

    std::lock_guard serialLock(connection->writeSerialMutex);
    {
        std::lock_guard lock(connection->writeLifecycleMutex);
        if (connection->writeInFlight)
            throw RecoverableException("Previous RFCOMM write is still stopping", true);
    }
    auto write = std::make_shared<MacOSBluetoothWriteWait>();
    {
        std::lock_guard lock(connection->activeWriteMutex);
        connection->activeWrite = write;
    }
    std::vector<char> data(buf, buf + length);
    {
        std::lock_guard lock(connection->writeLifecycleMutex);
        connection->writeInFlight = true;
    }
    std::thread writer;
    try {
        writer = std::thread([connection, write, data = std::move(data)]() mutable {
            WriteFinishedGuard finished{connection};
            IOReturn status = kIOReturnNotOpen;
            @autoreleasepool {
                IOBluetoothRFCOMMChannel* channel = copyChannel(connection);
                if (channel && !connection->cancelled)
                    status = [channel writeSync:data.data() length:data.size()];
            }
            {
                std::lock_guard lock(write->mutex);
                write->status = status;
                write->completed = true;
            }
            write->changed.notify_all();
        });
    } catch (...) {
        {
            std::lock_guard lock(connection->writeLifecycleMutex);
            connection->writeInFlight = false;
        }
        connection->writeLifecycleChanged.notify_all();
        throw;
    }

    std::unique_lock lock(write->mutex);
    const bool completed = write->changed.wait_for(lock, WriteTimeout, [&] {
        return write->completed || connection->cancelled.load();
    });
    const auto status = write->status;
    const bool workerFinished = write->completed;
    lock.unlock();
    if (!completed || connection->cancelled) {
        cancel(connection);
        if (writer.joinable()) {
            if (workerFinished) writer.join();
            else writer.detach();
        }
        throw RecoverableException("RFCOMM write timed out or was cancelled", true);
    }
    if (writer.joinable()) writer.join();
    {
        std::lock_guard activeLock(connection->activeWriteMutex);
        connection->activeWrite.reset();
    }
    if (status != kIOReturnSuccess) {
        cancel(connection);
        throw RecoverableException("RFCOMM write failed (status " + std::to_string(status) + ")", true);
    }
    return static_cast<int>(length);
}

int MacOSBluetoothConnector::recv(char* buf, size_t length) {
    auto connection = currentState();
    if (!connection) throw RecoverableException("Bluetooth channel is not connected", true);

    std::unique_lock lock(connection->receiveMutex);
    const bool received = connection->receiveChanged.wait_for(lock, std::chrono::milliseconds(1500), [&] {
        return !connection->receivedBytes.empty() || connection->receiveCancelled.load() ||
            connection->cancelled.load() || !connection->connected.load();
    });
    if (connection->receiveCancelled || connection->cancelled || !connection->connected)
        throw RecoverableException("Bluetooth receive cancelled", true);
    if (!received) throw RecoverableException("recv timed out", false);

    auto bytes = std::move(connection->receivedBytes.front());
    connection->receivedBytes.pop_front();
    const size_t copied = std::min(length, bytes.size());
    connection->queuedReceiveBytes -= copied;
    std::memcpy(buf, bytes.data(), copied);
    if (bytes.size() > copied) {
        bytes.erase(bytes.begin(), bytes.begin() + copied);
        connection->receivedBytes.push_front(std::move(bytes));
    }
#ifdef SHC_DEBUG_PROTOCOL
    _debugHexDump("recv", buf, copied);
#endif
    return static_cast<int>(copied);
}

void MacOSBluetoothConnector::cancelPendingIo() noexcept {
    cancel(currentState());
}

SonyProtocolVersion MacOSBluetoothConnector::getProtocolVersion() noexcept {
    auto connection = currentState();
    return connection ? connection->protocolVersion.load() : SonyProtocolVersion::V1;
}

std::vector<BluetoothDevice> MacOSBluetoothConnector::getConnectedDevices() {
    std::vector<BluetoothDevice> devices;
    for (IOBluetoothDevice* device in [IOBluetoothDevice pairedDevices]) {
        if (![device addressString]) continue;
        BluetoothDevice item;
        item.mac = [[device addressString] UTF8String];
        item.name = [device name] ? [[device name] UTF8String] : "Unknown Device";
        item.paired = true;
        item.connected = [device isConnected] == YES;
        devices.push_back(std::move(item));
    }
    return devices;
}

void MacOSBluetoothConnector::disconnect() noexcept {
    auto connection = currentState();
    cancel(connection);
    const auto deadline = std::chrono::steady_clock::now() + DisconnectTimeout;
    if (connectThread.joinable() && connectThread.get_id() != std::this_thread::get_id()) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (waitForThread(connection, remaining)) connectThread.join();
        else connectThread.detach();
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    waitForWrite(connection, remaining);
}

void MacOSBluetoothConnector::closeConnection() {
    auto connection = currentState();
    if (!connection) return;
    connection->connected = false;
    connection->channelClosed = true;
    connection->receiveCancelled = true;
    connection->receiveChanged.notify_all();
    closeChannel(connection);
}

bool MacOSBluetoothConnector::isConnected() noexcept {
    auto connection = currentState();
    if (!connection || !connection->connected || connection->cancelled) return false;
    IOBluetoothRFCOMMChannel* channel = copyChannel(connection);
    const bool open = channel && channel.isOpen;
    if (!open) connection->connected = false;
    return open;
}
