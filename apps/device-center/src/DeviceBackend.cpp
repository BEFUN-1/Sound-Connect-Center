#include "DeviceBackend.h"
#include "sony/core/DeviceService.h"
#include "sony/transport/PlatformTransport.h"
#include <QSettings>

namespace sony::devicecenter {
DeviceBackend::DeviceBackend(std::shared_ptr<core::IDeviceService> service) : _service(std::move(service)) {}
DeviceBackend::~DeviceBackend() { shutdown(); }

void DeviceBackend::cancelPendingIo() noexcept {
    std::lock_guard lock(_serviceMutex);
    if (_service) _service->cancelPendingIo();
}

void DeviceBackend::shutdown() {
    if (_stopped.exchange(true)) return;
    if (_timer) _timer->stop();
    _commands.clear();
    cancelPendingIo();
    if (_operation.valid()) _operation.get();
    if (_subscribed) {
        _subscribed->events().removeListener(_subscription);
        _subscribed = nullptr;
    }
    if (_service) {
        rememberPreferences();
        _service->close();
    }
    _executorThread.quit();
    _executorThread.wait();
    _executor = nullptr;
}

void DeviceBackend::start() {
    try {
        _executor = new QObject;
        _executor->moveToThread(&_executorThread);
        connect(&_executorThread, &QThread::finished, _executor, &QObject::deleteLater);
        _executorThread.start();
        {
            std::lock_guard lock(_serviceMutex);
            if (!_service) {
#if !defined(Q_OS_WIN)
                _usingIpc = _ipc.isDaemonRunning();
#endif
                if (!_usingIpc) {
                    auto policy = core::DeviceService::RecoveryPolicy::Legacy;
#if defined(Q_OS_WIN)
                    policy = core::DeviceService::RecoveryPolicy::RequireSystemConnection;
#endif
                    _service = std::make_shared<core::DeviceService>(
                        transport::createPlatformTransport(), transport::createPlatformDiscovery(),
                        [] { return core::DeviceService::Clock::now(); }, core::InitialRefresh::All, true, policy);
                }
            }
        }
        if (!_usingIpc && _service && !_service->isConnected()) {
            QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
            core::ConnectionPreferences preferences;
            preferences.selectedAddress = settings.value("connection/selectedAddress").toString().toStdString();
            for (const auto& address : settings.value("connection/pausedAddresses").toStringList())
                preferences.pausedAddresses.push_back(address.toStdString());
            for (const auto& address : settings.value("connection/resumeOnSystemConnectionAddresses").toStringList())
                preferences.resumeOnSystemConnectionAddresses.push_back(address.toStdString());
            // Older releases persisted every pause without its cause. Migrate
            // those records once to observation; new manual pauses keep a latch.
            if (!settings.contains("connection/resumeOnSystemConnectionAddresses"))
                preferences.resumeOnSystemConnectionAddresses = preferences.pausedAddresses;
            preferences.waitingForPowerOnAddress =
                settings.value("connection/waitingForPowerOnAddress").toString().toStdString();
            try { _service->restoreConnectionPreferences(preferences); }
            catch (const std::invalid_argument&) { settings.remove("connection/selectedAddress"); }
        }
        launch([this] {
            try {
                publish(0);
                emit devicesReady(QByteArray::fromStdString(request("devices").dump()));
                poll();
            } catch (const std::exception& ex) { emit error(QString::fromUtf8(ex.what()), _generation.load()); }
            emit completed(0);
        });
        _timer = new QTimer(this);
        _timer->setInterval(1000);
        connect(_timer, &QTimer::timeout, this, &DeviceBackend::pump);
        _timer->start();
    } catch (const std::exception& ex) {
        emit error(QString::fromUtf8(ex.what()), _generation.load());
        emit completed(0);
    }
}

DeviceBackend::Json DeviceBackend::request(const std::string& method, const Json& params) {
    if (_incompatible) throw std::runtime_error("Update sonyd and Sony Device Center to matching versions");
    Json req{{"version",core::JsonProtocol::Version},{"id",++_requestId},{"method",method},{"params",params}};
    Json response;
    if (_usingIpc) {
        try { response = Json::parse(_ipc.request(req.dump())); }
        catch (const Json::exception&) { _incompatible = true; throw std::runtime_error("Daemon does not support structured IPC; update sonyd"); }
    } else response = core::JsonProtocol::execute(req, *_service);
    if (response.value("version",0) != core::JsonProtocol::Version || response.value("id",Json{}) != req["id"])
        throw std::runtime_error("Incompatible or mismatched daemon response; update sonyd");
    if (!response.value("ok",false)) {
        const auto e = response.value("error",Json::object());
        if (e.value("code",std::string{}) == "VersionMismatch") _incompatible = true;
        throw std::runtime_error(e.value("message",std::string("Device command failed")));
    }
    return response.at("data");
}

void DeviceBackend::rememberPreferences() {
    if (!_service || _usingIpc) return;
    const auto preferences = _service->connectionPreferences();
    if (preferences.selectedAddress.empty()) return;
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
    bool changed = false;
    const auto writeChanged = [&](const char* key, const QVariant& value) {
        if (settings.value(key) != value) { settings.setValue(key, value); changed = true; }
    };
    writeChanged("connection/selectedAddress", QString::fromStdString(preferences.selectedAddress));
    QStringList paused;
    for (const auto& address : preferences.pausedAddresses) paused.push_back(QString::fromStdString(address));
    writeChanged("connection/pausedAddresses", paused);
    QStringList resumeOnSystemConnection;
    for (const auto& address : preferences.resumeOnSystemConnectionAddresses)
        resumeOnSystemConnection.push_back(QString::fromStdString(address));
    writeChanged("connection/resumeOnSystemConnectionAddresses", resumeOnSystemConnection);
    writeChanged("connection/waitingForPowerOnAddress", QString::fromStdString(preferences.waitingForPowerOnAddress));
    if (changed) settings.sync();
}

void DeviceBackend::rememberSelection(const Json& snapshot) {
    if (_service) { rememberPreferences(); return; }
    const auto address = QString::fromStdString(snapshot.value("address", std::string{}));
    if (address.isEmpty()) return;
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
    if (settings.value("connection/selectedAddress").toString() != address)
        settings.setValue("connection/selectedAddress", address);
}

void DeviceBackend::deliver(Json data, quint64 generation) {
    std::lock_guard lock(_publicationMutex);
    if (_stopped || generation != _generation.load()) return;
    const auto session = data.value("sessionId", uint64_t{});
    const auto revision = data.value("revision", uint64_t{});
    if (generation == _publishedGeneration &&
        (session < _publishedSession || (session == _publishedSession && revision < _publishedRevision))) return;
    const auto bytes = QByteArray::fromStdString(data.dump());
    if (bytes == _lastPublished && generation == _publishedGeneration) return;
    _publishedGeneration = generation;
    _publishedSession = session;
    _publishedRevision = revision;
    _lastPublished = bytes;
    emit snapshotReady(bytes, generation);
}

void DeviceBackend::publish(quint64 generation) {
    const auto data = request("snapshot");
    rememberSelection(data);
    deliver(data, generation);
}

void DeviceBackend::subscribe() {
    if (!_service) return;
    auto* device = _service->activeDevice();
    const auto session = device ? device->snapshot()->sessionId : 0;
    const auto connected = _service->isConnected();
    if (device == _subscribed && session == _subscribedSession &&
        _subscribedGeneration == _generation.load() && connected == _subscribedConnected) return;
    if (_subscribed) _subscribed->events().removeListener(_subscription);
    _subscribed = device;
    _subscribedSession = session;
    _subscribedConnected = connected;
    if (!device) return;
    const auto envelope = request("snapshot");
    const auto generation = _generation.load();
    const auto epoch = ++_notificationEpoch;
    _subscribedGeneration = generation;
    _subscription = device->events().onStateChanged([this, envelope, generation, session, epoch](const auto& event) {
        if (event.state->sessionId != session || epoch != _notificationEpoch.load()) return;
        auto data = envelope;
        data.update(core::JsonProtocol::stateSnapshot(event.state, envelope.value("connected", false)));
        deliver(std::move(data), generation);
    });
}

void DeviceBackend::poll() {
    if (_stopped || _incompatible) return;
    const auto generation = _generation.load();
    try {
        if (_service) {
            _service->tick();
            if (generation == _generation.load()) subscribe();
        }
        publish(generation);
    } catch (const std::exception& ex) {
        emit error(QString::fromUtf8(ex.what()), _generation.load());
        if (_usingIpc)
            deliver({{"connected",false},{"connectionState","disconnected"}}, _generation.load());
    }
}

void DeviceBackend::command(QByteArray bytes, quint64 generation) {
    if (_stopped) { emit completed(generation); return; }
    const auto cmd = Json::parse(bytes.toStdString());
    const auto method = cmd.value("method", std::string{});
    if (method == "connect" || method == "disconnect") {
        ++_notificationEpoch;
        _commands.clear();
        _generation = generation;
        cancelPendingIo();
    }
    // Only unsent slider input is replaceable; discrete operations retain order.
    if ((method == "ambient" || method == "eqCustom") && !_commands.empty()) {
        auto& last = _commands.back();
        if (last.generation == generation &&
            Json::parse(last.bytes.toStdString()).value("method", std::string{}) == method) {
            last.bytes = std::move(bytes);
            emit completed(generation);
            return;
        }
    }
    if (_commands.size() >= 32) {
        emit error(QStringLiteral("Command queue is full (32); please retry after pending operations finish"), generation);
        emit completed(generation);
        return;
    }
    _commands.push_back({std::move(bytes), generation});
    pump();
}

void DeviceBackend::discover() { _discoverPending = true; pump(); }

void DeviceBackend::launch(std::function<void()> operation) {
    auto completion = std::make_shared<std::promise<void>>();
    _operation = completion->get_future();
    // Qt owns the executor thread's TLS, and I/O jobs reuse it until shutdown.
    QMetaObject::invokeMethod(_executor, [this, completion, operation = std::move(operation)] {
        try { operation(); }
        catch (const std::exception& ex) { emit error(QString::fromUtf8(ex.what()), _generation.load()); }
        completion->set_value();
        QMetaObject::invokeMethod(this, &DeviceBackend::pump, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void DeviceBackend::pump() {
    if (_stopped) return;
    if (_operation.valid()) {
        if (_operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        _operation.get();
    }
    if (!_commands.empty()) {
        const auto command = std::move(_commands.front());
        _commands.pop_front();
        launch([this, command] {
            try {
                const auto cmd = Json::parse(command.bytes.toStdString());
                const auto method = cmd.at("method").get<std::string>();
                if (_service && method != "connect" && method != "disconnect") _service->serviceBattery();
                const auto data = request(method, cmd.value("params",Json::object()));
                rememberSelection(data);
                if (command.generation == _generation.load()) subscribe();
                deliver(data, command.generation);
            } catch (const std::exception& ex) {
                emit error(QString::fromUtf8(ex.what()), command.generation);
                try {
                    const auto data = request("snapshot");
                    rememberSelection(data);
                    deliver(data, command.generation);
                } catch (...) {}
            }
            emit completed(command.generation);
        });
    } else if (_discoverPending) {
        _discoverPending = false;
        launch([this] {
            try { emit devicesReady(QByteArray::fromStdString(request("devices").dump())); }
            catch (const std::exception& ex) { emit error(QString::fromUtf8(ex.what()), _generation.load()); }
        });
    } else if (std::chrono::steady_clock::now() >= _nextPoll) {
        _nextPoll = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        launch([this] { poll(); });
    }
}
}
