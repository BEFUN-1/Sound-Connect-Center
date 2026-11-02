#include "DeviceCenterController.h"
#include "DeviceBackend.h"
#include <QJsonDocument>
#include <QJsonArray>
#include "I18nManager.h"
#include "sony/core/DeviceService.h"
#include "sony/core/IpcProtocol.h"
#include "sony/protocol/DeviceProfileRegistry.h"
#include "sony/protocol/EqualizerPresets.h"
#include "sony/transport/PlatformTransport.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>
#include <sstream>

namespace sony::devicecenter {

DeviceCenterController::DeviceCenterController(QObject* parent, std::shared_ptr<core::IDeviceService> service)
    : QObject(parent) {
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
    _currentLanguage = settings.value("language", "zh_CN").toString();
    if (_currentLanguage.isEmpty())
        _currentLanguage = "zh_CN";
    _darkTheme = settings.value("darkTheme", true).toBool();
    const auto storedPresets = settings.value("equalizer/customPresets").toList();
    for (const auto& value : storedPresets) {
        const auto preset = value.toMap();
        const auto name = preset.value("name").toString().trimmed();
        const auto bands = preset.value("bands").toList();
        const auto id = preset.value("id").isValid() ? preset.value("id").toInt() : -1;
        if (id > 0 && !name.isEmpty() && (bands.size() == 5 || bands.size() == 10))
            _equalizerPresets.push_back(QVariantMap{
                {"id", id}, {"name", name},
                {"clearBass", preset.value("clearBass").toInt()},
                {"bands", bands}});
    }
    _backend = new DeviceBackend(std::move(service));
    _backend->moveToThread(&_worker);
    connect(&_worker, &QThread::started, _backend, &DeviceBackend::start);
    connect(&_worker, &QThread::finished, _backend, &QObject::deleteLater);
    connect(_backend, &DeviceBackend::snapshotReady, this, [this](const QByteArray& data, quint64 generation) {
        if (generation == _generation) _applySnapshot(data);
    });
    connect(_backend, &DeviceBackend::devicesReady, this, [this](const QByteArray& data) {
        _discoveredDevices = QJsonDocument::fromJson(data).array().toVariantList();
        QVariantList paired;
        for (const auto& value : _discoveredDevices) {
            const auto item = value.toMap();
            // Older transports do not expose the paired flag. Keep those
            // entries visible in the switcher while newer transports can
            // distinguish the discovery list from the paired subset.
            const auto pairedValue = item.value("paired");
            if (!item.contains("paired") || !pairedValue.isValid() || pairedValue.isNull() || pairedValue.toBool())
                paired.push_back(value);
        }
        _pairedDevices = std::move(paired);
        emit pairedDevicesChanged();
        emit discoveredDevicesChanged();
    });
    connect(_backend, &DeviceBackend::error, this, [this](const QString& error, quint64 generation) {
        if (generation == _generation) { _lastError = error; emit stateChanged(); }
    });
    connect(_backend, &DeviceBackend::completed, this, [this](quint64 generation) {
        if (generation == _generation) {
            if (_pendingCommands > 0) --_pendingCommands;
            _busy = _pendingCommands > 0;
            emit stateChanged();
        }
    });
    _worker.start();
}
DeviceCenterController::~DeviceCenterController() {
    _backend->cancelPendingIo();
    QMetaObject::invokeMethod(_backend, &DeviceBackend::shutdown, Qt::BlockingQueuedConnection);
    _worker.quit(); _worker.wait();
}
void DeviceCenterController::_send(const QString& method, const QJsonObject& params) {
    if (method == "connect" || method == "disconnect") {
        ++_generation;
        _pendingCommands = 0;
        _expectedAddress = (method == "connect" ? params.value("address").toString() : _deviceAddress).toUpper();
        _backend->cancelPendingIo();
    }
    ++_pendingCommands;
    _busy = true; _lastError.clear(); const auto generation = _generation;
    emit stateChanged();
    auto targetedParams = params;
    if (method != "connect" && method != "disconnect" && method != "devices")
        targetedParams.insert("targetAddress", _expectedAddress.isEmpty() ? _deviceAddress : _expectedAddress);
    const auto bytes = QJsonDocument(QJsonObject{{"method",method},{"params",targetedParams}}).toJson(QJsonDocument::Compact);
    QMetaObject::invokeMethod(_backend, [backend = _backend, bytes, generation] { backend->command(bytes, generation); }, Qt::QueuedConnection);
}
void DeviceCenterController::_applySnapshot(const QByteArray& data) {
    const auto s = QJsonDocument::fromJson(data).object();
    if (!_expectedAddress.isEmpty() && s.value("address").toString().toUpper() != _expectedAddress) return;
    const auto session = s.value("sessionId").toVariant().toULongLong();
    const auto revision = s.value("revision").toVariant().toULongLong();
    if (session < _sessionId || (session == _sessionId && revision < _revision)) return;
    if (data == _lastSnapshot) return;
    _sessionId = session;
    _revision = revision;
    _lastSnapshot = data;
    _connected = s.value("connected").toBool();
    _connectionState = s.value("connectionState").toString("disconnected");
    _connectionIntent = s.value("connectionIntent").toInt();
    _systemConnected = s.value("systemConnected").toBool();
    if (s.contains("name")) _deviceName = s.value("name").toString();
    if (s.contains("address")) _deviceAddress = s.value("address").toString().toUpper();
    if (!s.contains("features")) {
        _batteryLevel = -1; _noiseControlMode = "unknown";
        _autoPowerOffOptions.clear();
        _multipointSupported = false; _sourceSwitchSupported = false;
        _sourceKeepSupported = false; _sourceKeeping = false;
        _playbackIndex = 0; _maxConnected = 0;
        _multipointDevices.clear(); _multipointError.clear();
        emit stateChanged(); emit multipointChanged(); return;
    }
    _features = s.value("features").toObject().toVariantMap();
    _capabilities = s.value("capabilities").toObject();
    auto valid = [&s](const char* feature) {
        const auto status = s.value("features").toObject().value(feature).toObject();
        const auto availability = status.value("availability").toString();
        return availability == "valid" ||
            (availability == "stale" && status.value("lastSuccessMs").toDouble() > 0);
    };
    const auto battery = s.value("battery").toObject();
    _batteryLevel = _connected && valid("battery") ? battery.value("main").toInt(-1) : -1;
    _isCharging = _connected && battery.value("charging").toBool();
    const auto nc = s.value("noiseControl").toObject();
    _noiseControlMode = valid("noiseControl") ? nc.value("mode").toString() : "unknown";
    _ambientLevel = nc.value("ambientLevel").toInt(); _focusOnVoice = nc.value("focusOnVoice").toBool();
    const auto eq = s.value("equalizer").toObject();
    _equalizerPreset = valid("equalizer") ? eq.value("preset").toInt() : -1;
    _equalizerPresetName = valid("equalizer") ? eq.value("presetName").toString() : "Unknown";
    _clearBass = eq.value("clearBass").toInt(); _equalizerBands = eq.value("bands").toArray().toVariantList();
    _matchActiveCustomPreset();
    _dsee = s.value("dsee").toBool(); _speakToChat = s.value("speakToChat").toBool();
    _adaptiveVolume = s.value("adaptiveVolume").toBool(); _autoPowerOff = s.value("autoPowerOff").toInt();
    _autoPowerOffOptions = s.value("autoPowerOffOptions").toArray().toVariantList();
    _codec = _connected && valid("codec") ? s.value("codec").toString("Unknown") : "Unknown";
    const auto multipoint = s.value("multipoint").toObject();
    _multipointSupported = _connected && multipoint.value("supported").toBool();
    _sourceSwitchSupported = _connected && multipoint.value("sourceSwitchSupported").toBool();
    _sourceKeepSupported = _connected && multipoint.value("sourceKeepSupported").toBool();
    _sourceKeeping = _connected && multipoint.value("sourceKeeping").toBool();
    _playbackIndex = _connected ? multipoint.value("playbackIndex").toInt() : 0;
    _maxConnected = _connected ? multipoint.value("maxConnected").toInt() : 0;
    _multipointError = _connected ? multipoint.value("error").toString() : QString();
    _multipointDevices = _connected ? multipoint.value("devices").toArray().toVariantList() : QVariantList();
    emit stateChanged(); emit capabilitiesChanged(); emit multipointChanged();
}

QString DeviceCenterController::deviceName() const { return _deviceName; }
QString DeviceCenterController::connectionLabel() const {
    if (_connected) return t("connected");
    if (_connectionIntent == static_cast<int>(core::ConnectionIntent::PausedByUser) ||
        _connectionIntent == static_cast<int>(core::ConnectionIntent::PausedAfterSystemDisconnect))
        return t(_systemConnected ? "system_connected_control_paused" : "control_paused_short");
    if (_connectionIntent == static_cast<int>(core::ConnectionIntent::WaitingForPowerOn))
        return t("waiting_for_power_on_short");
    if (_systemConnected) return t("control_recovering");
    if (_connectionState == "waiting_for_system_connection") return t("waiting_for_system_connection");
    return t("disconnected");
}
QString DeviceCenterController::statusDescription() const {
    if (_connectionIntent == static_cast<int>(core::ConnectionIntent::PausedByUser) ||
        _connectionIntent == static_cast<int>(core::ConnectionIntent::PausedAfterSystemDisconnect))
        return t(_systemConnected ? "control_paused_system_connected" : "control_paused");
    if (_connectionIntent == static_cast<int>(core::ConnectionIntent::WaitingForPowerOn))
        return t("waiting_for_power_on");
    if (!_connected) return connectionLabel();
    QString description = t("connected");
    for (const auto& value : _features) {
        if (value.toMap().value("availability").toString() == "stale") {
            description += " · " + t("status_update_delayed");
            break;
        }
    }
    const auto battery = _features.value("battery").toMap();
    const auto updated = battery.value("lastSuccessMs").toLongLong();
    if (updated > 0)
        description += " · " + t("battery_last_updated").arg(
            QDateTime::fromMSecsSinceEpoch(updated).toString("HH:mm:ss"));
    else description += " · " + t("tray_battery_unknown");
    return description;
}
QString DeviceCenterController::deviceAddress() const { return _deviceAddress; }
bool DeviceCenterController::isConnected() const { return _connected; }
int DeviceCenterController::batteryLevel() const { return _batteryLevel; }
bool DeviceCenterController::isCharging() const { return _isCharging; }
QString DeviceCenterController::noiseControlMode() const { return _noiseControlMode; }
int DeviceCenterController::ambientLevel() const { return _ambientLevel; }
bool DeviceCenterController::focusOnVoice() const { return _focusOnVoice; }
int DeviceCenterController::equalizerPreset() const { return _equalizerPreset; }
QString DeviceCenterController::equalizerPresetName() const {
    if (_activeCustomEqualizerPresetId != -1) {
        for (const auto& value : _equalizerPresets) {
            const auto preset = value.toMap();
            if (preset.value("id").toInt() == _activeCustomEqualizerPresetId)
                return preset.value("name").toString();
        }
    }
    return _equalizerPresetName;
}
QVariantList DeviceCenterController::equalizerPresets() const { return _equalizerPresets; }
int DeviceCenterController::activeCustomEqualizerPresetId() const { return _activeCustomEqualizerPresetId; }
int DeviceCenterController::clearBass() const { return _clearBass; }
QVariantList DeviceCenterController::equalizerBands() const { return _equalizerBands; }
bool DeviceCenterController::dsee() const { return _dsee; }
bool DeviceCenterController::speakToChat() const { return _speakToChat; }
bool DeviceCenterController::adaptiveVolume() const { return _adaptiveVolume; }
int DeviceCenterController::autoPowerOff() const { return _autoPowerOff; }

QString DeviceCenterController::heroImagePath() const {
    QString lower = _deviceName.toLower();
    if (lower.contains("wf") && lower.contains("xm6")) {
        return "resources/devices/wf-1000xm6.png";
    } else if (lower.contains("wf") && lower.contains("xm5")) {
        return "resources/devices/wf-1000xm5.png";
    } else if (lower.contains("wf") && lower.contains("xm4")) {
        return "resources/devices/wf-1000xm4.png";
    } else if (lower.contains("wf") && lower.contains("xm3")) {
        return "resources/devices/wf-1000xm3.png";
    } else if (lower.contains("xm6")) {
        return "resources/devices/wh-1000xm6.png";
    } else if (lower.contains("xm5")) {
        return "resources/devices/wh-1000xm5.png";
    } else if (lower.contains("xm4")) {
        return "resources/devices/wh-1000xm4.png";
    } else if (lower.contains("xm3")) {
        return "resources/devices/wh-1000xm3.png";
    } else if (lower.contains("ch720")) {
        return "resources/devices/wh-ch720n.png";
    } else if (lower.contains("linkbuds")) {
        return "resources/devices/linkbuds-s.png";
    }
    return "resources/devices/wh-1000xm5.png";
}

bool DeviceCenterController::hasAnc() const { return _capabilities.value("anc").toBool(); }

bool DeviceCenterController::hasAmbient() const { return _capabilities.value("ambient").toBool(); }

bool DeviceCenterController::hasEqualizer() const { return _capabilities.value("equalizer").toBool(); }

bool DeviceCenterController::hasClearBass() const { return _capabilities.value("clearBass").toBool(); }

bool DeviceCenterController::tenBandEqualizer() const { return _capabilities.value("tenBandEqualizer").toBool(); }

bool DeviceCenterController::hasDsee() const { return _capabilities.value("dsee").toBool(); }

bool DeviceCenterController::hasSpeakToChat() const { return _capabilities.value("speakToChat").toBool(); }

bool DeviceCenterController::hasAdaptiveVolume() const { return _capabilities.value("adaptiveVolume").toBool(); }
bool DeviceCenterController::hasPowerOff() const { return _capabilities.value("powerOff").toBool(); }
bool DeviceCenterController::hasWearSensor() const { return _capabilities.value("wearSensor").toBool(); }

QVariantList DeviceCenterController::pairedDevices() const { return _pairedDevices; }
QVariantList DeviceCenterController::discoveredDevices() const { return _discoveredDevices; }

QVariantList DeviceCenterController::multipointDevices() const { return _multipointDevices; }
bool DeviceCenterController::multipointSupported() const { return _multipointSupported; }
bool DeviceCenterController::sourceSwitchSupported() const { return _sourceSwitchSupported; }
bool DeviceCenterController::sourceKeepSupported() const { return _sourceKeepSupported; }
bool DeviceCenterController::sourceKeeping() const { return _sourceKeeping; }
int DeviceCenterController::playbackIndex() const { return _playbackIndex; }
int DeviceCenterController::maxConnected() const { return _maxConnected; }
QString DeviceCenterController::multipointError() const { return _multipointError; }

void DeviceCenterController::setAnc(bool enabled) { _send("anc", {{"enabled",enabled}}); }
void DeviceCenterController::setAmbient(int level, bool voice) { _send("ambient", {{"level",level},{"focusOnVoice",voice}}); }
void DeviceCenterController::setNoiseControlOff() { setAnc(false); }
void DeviceCenterController::setEqualizerPreset(int preset) {
    if (_activeCustomEqualizerPresetId != -1) {
        _activeCustomEqualizerPresetId = -1;
        emit stateChanged();
    }
    _send("eqPreset", {{"preset",preset}});
}
void DeviceCenterController::setEqualizerCustom(int bass, const QVariantList& bands) {
    if (_activeCustomEqualizerPresetId != -1) {
        _activeCustomEqualizerPresetId = -1;
        emit stateChanged();
    }
    _send("eqCustom", {{"clearBass",bass},{"bands",QJsonArray::fromVariantList(bands)}});
}
void DeviceCenterController::saveEqualizerPreset(const QString& name) {
    const auto trimmed = name.trimmed();
    if (trimmed.isEmpty() || !_connected || !hasEqualizer())
        return;

    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
    int nextId = settings.value("equalizer/nextCustomPresetId", 1000).toInt();
    if (nextId < 1000)
        nextId = 1000;
    while (std::any_of(_equalizerPresets.cbegin(), _equalizerPresets.cend(),
                       [nextId](const auto& value) { return value.toMap().value("id").toInt() == nextId; }))
        ++nextId;
    _equalizerPresets.push_back(QVariantMap{
        {"id", nextId},
        {"name", trimmed},
        {"clearBass", _clearBass},
        {"bands", _equalizerBands}});
    settings.setValue("equalizer/nextCustomPresetId", nextId + 1);
    _saveEqualizerPresets();
    emit equalizerPresetsChanged();
}
void DeviceCenterController::renameEqualizerPreset(int id, const QString& name) {
    const auto trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return;
    for (auto& value : _equalizerPresets) {
        auto preset = value.toMap();
        if (preset.value("id").toInt() == id) {
            preset["name"] = trimmed;
            value = preset;
            _saveEqualizerPresets();
            emit equalizerPresetsChanged();
            emit stateChanged();
            return;
        }
    }
}
void DeviceCenterController::deleteEqualizerPreset(int id) {
    for (qsizetype i = 0; i < _equalizerPresets.size(); ++i) {
        if (_equalizerPresets.at(i).toMap().value("id").toInt() != id)
            continue;
        _equalizerPresets.removeAt(i);
        if (_activeCustomEqualizerPresetId == id)
            _activeCustomEqualizerPresetId = -1;
        _saveEqualizerPresets();
        emit equalizerPresetsChanged();
        emit stateChanged();
        return;
    }
}
void DeviceCenterController::applyEqualizerPreset(int id) {
    for (const auto& value : _equalizerPresets) {
        const auto preset = value.toMap();
        if (preset.value("id").toInt() != id)
            continue;
        if (!_connected || !hasEqualizer())
            return;
        _activeCustomEqualizerPresetId = id;
        emit stateChanged();
        _send("eqCustom", {{"clearBass", preset.value("clearBass").toInt()},
                           {"bands", QJsonArray::fromVariantList(preset.value("bands").toList())}});
        return;
    }
}
void DeviceCenterController::setDsee(bool enabled) { _send("dsee", {{"enabled",enabled}}); }
void DeviceCenterController::setSpeakToChat(bool enabled) { _send("speakToChat", {{"enabled",enabled}}); }
void DeviceCenterController::setAdaptiveVolume(bool enabled) { _send("adaptiveVolume", {{"enabled",enabled}}); }
void DeviceCenterController::setAutoPowerOff(int index) { _send("autoPowerOff", {{"index",index}}); }
void DeviceCenterController::powerOff() { _send("powerOff"); }
void DeviceCenterController::connectDevice(const QString& address, const QString& name) { _send("connect", {{"address",address},{"name",name}}); }
void DeviceCenterController::disconnectDevice() { _send("disconnect"); }
void DeviceCenterController::refreshDiscoveredDevices() {
    QMetaObject::invokeMethod(_backend, &DeviceBackend::discover, Qt::QueuedConnection);
}
void DeviceCenterController::refreshMultipoint() { _send("multipoint", {{"action", "list"}}); }
void DeviceCenterController::switchMultipointSource(const QString& address) {
    _send("multipoint", {{"action", "source"}, {"address", address}});
}
void DeviceCenterController::setMultipointDefault(const QString& address) {
    _send("multipoint", {{"action", "default"}, {"address", address}});
}
void DeviceCenterController::setMultipointSourceKeeping(bool enabled) {
    _send("multipoint", {{"action", "keep"}, {"enabled", enabled}});
}
void DeviceCenterController::multipointConnect(const QString& address) {
    _send("multipoint", {{"action", "connect"}, {"address", address}});
}
void DeviceCenterController::multipointDisconnect(const QString& address) {
    _send("multipoint", {{"action", "disconnect"}, {"address", address}});
}
void DeviceCenterController::multipointUnpair(const QString& address) {
    _send("multipoint", {{"action", "unpair"}, {"address", address}});
}

bool DeviceCenterController::autostart() const {
#if defined(Q_OS_LINUX)
    QString autostartDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart";
    QString desktopFile = autostartDir + "/sony-device-center.desktop";
    return QFileInfo::exists(desktopFile);
#elif defined(Q_OS_WIN)
    QSettings bootSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    return bootSettings.contains("SonyDeviceCenter");
#else
    return false;
#endif
}

void DeviceCenterController::setAutostart(bool enable) {
#if defined(Q_OS_LINUX)
    QString autostartDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart";
    QString desktopFile = autostartDir + "/sony-device-center.desktop";
    if (enable) {
        QDir().mkpath(autostartDir);
        QFile file(desktopFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "[Desktop Entry]\n";
            out << "Type=Application\n";
            out << "Name=Sony Device Center\n";
            out << "Comment=Unofficial open-source companion for Sony WH/WF/LinkBuds audio devices\n";
            out << "Exec=\"" << QDir::toNativeSeparators(QCoreApplication::applicationFilePath())
                << "\" --tray\n";
            out << "Icon=sony-device-center\n";
            out << "Terminal=false\n";
            out << "Categories=Audio;AudioVideo;Settings;\n";
            out << "X-GNOME-Autostart-enabled=true\n";
        }
    } else {
        QFile::remove(desktopFile);
    }
#elif defined(Q_OS_WIN)
    QSettings bootSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (enable) {
        QString appPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        bootSettings.setValue("SonyDeviceCenter", "\"" + appPath + "\" --tray");
    } else {
        bootSettings.remove("SonyDeviceCenter");
    }
#endif
    emit autostartChanged();
}

QString DeviceCenterController::appVersion() const {
    return QCoreApplication::applicationVersion();
}

QString DeviceCenterController::currentLanguage() const {
    return _currentLanguage;
}

QVariantList DeviceCenterController::availableLanguages() const {
    return I18nManager::instance().availableLanguages();
}

void DeviceCenterController::setLanguage(const QString& langCode) {
    if (_currentLanguage != langCode) {
        _currentLanguage = langCode;
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
        settings.setValue("language", langCode);
        emit languageChanged();
          emit stateChanged();
    }
}

bool DeviceCenterController::darkTheme() const {
    return _darkTheme;
}

void DeviceCenterController::setDarkTheme(bool enabled) {
    if (_darkTheme == enabled)
        return;
    _darkTheme = enabled;
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
    settings.setValue("darkTheme", enabled);
    emit themeChanged();
}

QString DeviceCenterController::t(const QString& key) const {
    return I18nManager::instance().translate(key, _currentLanguage);
}

void DeviceCenterController::openUrl(const QString& url) {
    QDesktopServices::openUrl(QUrl(url));
}

void DeviceCenterController::_saveEqualizerPresets() {
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
    settings.setValue("equalizer/customPresets", _equalizerPresets);
}

void DeviceCenterController::_matchActiveCustomPreset() {
    _activeCustomEqualizerPresetId = -1;
    if (_equalizerPreset != 0xa0)
        return;
    for (const auto& value : _equalizerPresets) {
        const auto preset = value.toMap();
        if (preset.value("clearBass").toInt() != _clearBass)
            continue;
        const auto bands = preset.value("bands").toList();
        if (bands == _equalizerBands) {
            _activeCustomEqualizerPresetId = preset.value("id").isValid() ? preset.value("id").toInt() : -1;
            _equalizerPresetName = preset.value("name").toString();
            return;
        }
    }
}

} // namespace sony::devicecenter
