#pragma once
#include <QObject>
#include <QTimer>
#include <QThread>
#include <atomic>
#include <deque>
#include <future>
#include <mutex>
#include "sony/core/IDeviceService.h"
#include "sony/core/IpcClient.h"
#include "sony/core/JsonProtocol.h"
namespace sony::devicecenter {
// Qt coordinates the queue while a single executor serializes service I/O.
// Reader callbacks publish immutable state without waiting for that executor.
class DeviceBackend : public QObject {
    Q_OBJECT
public:
    explicit DeviceBackend(std::shared_ptr<core::IDeviceService> service = {});
    ~DeviceBackend() override;
    void start();
    void command(QByteArray request, quint64 generation);
    void discover();
    void shutdown();
    void cancelPendingIo() noexcept;
signals:
    void snapshotReady(QByteArray data, quint64 generation);
    void devicesReady(QByteArray data);
    void error(QString message, quint64 generation);
    void completed(quint64 generation);
private:
    using Json = core::JsonProtocol::Json;
    Json request(const std::string& method, const Json& params = Json::object());
    void poll();
    void publish(quint64 generation);
    void rememberSelection(const Json& snapshot);
    void subscribe();
    void pump();
    void launch(std::function<void()> operation);
    void deliver(Json data, quint64 generation);
    void rememberPreferences();
    struct Command { QByteArray bytes; quint64 generation; };
    std::deque<Command> _commands;
    std::future<void> _operation;
    QThread _executorThread;
    QObject* _executor{nullptr};
    std::mutex _serviceMutex, _publicationMutex;
    QByteArray _lastPublished;
    quint64 _publishedGeneration{0};
    uint64_t _publishedSession{0}, _publishedRevision{0}, _subscribedSession{0};
    quint64 _subscribedGeneration{0};
    bool _subscribedConnected{false};
    std::atomic<quint64> _notificationEpoch{0};
    std::chrono::steady_clock::time_point _nextPoll{};
    bool _discoverPending{false};
    std::shared_ptr<core::IDeviceService> _service;
    core::IpcClient _ipc;
    bool _usingIpc{false};
    std::atomic<bool> _stopped{false}, _incompatible{false};
    QTimer* _timer{nullptr};
    std::atomic<quint64> _generation{0};
    quint64 _requestId{0};
    core::SonyDevice* _subscribed{nullptr};
    protocol::DeviceEventDispatcher::SubscriptionId _subscription{0};
};
}
