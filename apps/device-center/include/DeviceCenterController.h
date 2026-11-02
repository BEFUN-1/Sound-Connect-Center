#pragma once

#include <QObject>
#include <QThread>
#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <memory>
#include <string>

#include "sony/core/IDeviceService.h"
#include "sony/core/IpcClient.h"

namespace sony::devicecenter {

class DeviceBackend;

class DeviceCenterController : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY stateChanged)
    Q_PROPERTY(int connectionIntent READ connectionIntent NOTIFY stateChanged)
    Q_PROPERTY(bool systemConnected READ systemConnected NOTIFY stateChanged)
    Q_PROPERTY(QString statusDescription READ statusDescription NOTIFY stateChanged)
    Q_PROPERTY(QString connectionLabel READ connectionLabel NOTIFY stateChanged)
    Q_PROPERTY(QString codec READ codec NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap featureStatus READ featureStatus NOTIFY stateChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY stateChanged)
    Q_PROPERTY(QString deviceAddress READ deviceAddress NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY stateChanged)
    Q_PROPERTY(int batteryLevel READ batteryLevel NOTIFY stateChanged)
    Q_PROPERTY(bool isCharging READ isCharging NOTIFY stateChanged)
    Q_PROPERTY(QString noiseControlMode READ noiseControlMode NOTIFY stateChanged)
    Q_PROPERTY(int ambientLevel READ ambientLevel NOTIFY stateChanged)
    Q_PROPERTY(bool focusOnVoice READ focusOnVoice NOTIFY stateChanged)
    Q_PROPERTY(int equalizerPreset READ equalizerPreset NOTIFY stateChanged)
    Q_PROPERTY(QString equalizerPresetName READ equalizerPresetName NOTIFY stateChanged)
    Q_PROPERTY(QVariantList equalizerPresets READ equalizerPresets NOTIFY equalizerPresetsChanged)
    Q_PROPERTY(int activeCustomEqualizerPresetId READ activeCustomEqualizerPresetId NOTIFY stateChanged)
    Q_PROPERTY(int clearBass READ clearBass NOTIFY stateChanged)
    Q_PROPERTY(QVariantList equalizerBands READ equalizerBands NOTIFY stateChanged)
    Q_PROPERTY(bool dsee READ dsee NOTIFY stateChanged)
    Q_PROPERTY(bool speakToChat READ speakToChat NOTIFY stateChanged)
    Q_PROPERTY(bool adaptiveVolume READ adaptiveVolume NOTIFY stateChanged)
    Q_PROPERTY(int autoPowerOff READ autoPowerOff NOTIFY stateChanged)
    Q_PROPERTY(QVariantList autoPowerOffOptions READ autoPowerOffOptions NOTIFY stateChanged)
    Q_PROPERTY(QString heroImagePath READ heroImagePath NOTIFY stateChanged)

    Q_PROPERTY(bool hasAnc READ hasAnc NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool hasAmbient READ hasAmbient NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool hasEqualizer READ hasEqualizer NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool hasClearBass READ hasClearBass NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool tenBandEqualizer READ tenBandEqualizer NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool hasDsee READ hasDsee NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool hasSpeakToChat READ hasSpeakToChat NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool hasAdaptiveVolume READ hasAdaptiveVolume NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool hasPowerOff READ hasPowerOff NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool hasWearSensor READ hasWearSensor NOTIFY capabilitiesChanged)

    Q_PROPERTY(QVariantList pairedDevices READ pairedDevices NOTIFY pairedDevicesChanged)
    Q_PROPERTY(QVariantList discoveredDevices READ discoveredDevices NOTIFY discoveredDevicesChanged)

    Q_PROPERTY(QVariantList multipointDevices READ multipointDevices NOTIFY multipointChanged)
    Q_PROPERTY(bool multipointSupported READ multipointSupported NOTIFY multipointChanged)
    Q_PROPERTY(bool sourceSwitchSupported READ sourceSwitchSupported NOTIFY multipointChanged)
    Q_PROPERTY(bool sourceKeepSupported READ sourceKeepSupported NOTIFY multipointChanged)
    Q_PROPERTY(bool sourceKeeping READ sourceKeeping NOTIFY multipointChanged)
    Q_PROPERTY(int playbackIndex READ playbackIndex NOTIFY multipointChanged)
    Q_PROPERTY(int maxConnected READ maxConnected NOTIFY multipointChanged)
    Q_PROPERTY(QString multipointError READ multipointError NOTIFY multipointChanged)

    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY autostartChanged)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QString currentLanguage READ currentLanguage WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QVariantList availableLanguages READ availableLanguages CONSTANT)
    Q_PROPERTY(bool darkTheme READ darkTheme WRITE setDarkTheme NOTIFY themeChanged)

public:
    explicit DeviceCenterController(QObject* parent = nullptr, std::shared_ptr<core::IDeviceService> service = {});
    ~DeviceCenterController() override;

    bool busy() const { return _busy; }
    QString lastError() const { return _lastError; }
    QString connectionState() const { return _connectionState; }
    int connectionIntent() const { return _connectionIntent; }
    bool systemConnected() const { return _systemConnected; }
    QString statusDescription() const;
    QString connectionLabel() const;
    QString codec() const { return _codec; }
    QVariantMap featureStatus() const { return _features; }
    Q_INVOKABLE void clearError() { _lastError.clear(); emit stateChanged(); }
    [[nodiscard]] QString deviceName() const;
    [[nodiscard]] QString deviceAddress() const;
    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] int batteryLevel() const;
    [[nodiscard]] bool isCharging() const;
    [[nodiscard]] QString noiseControlMode() const;
    [[nodiscard]] int ambientLevel() const;
    [[nodiscard]] bool focusOnVoice() const;
    [[nodiscard]] int equalizerPreset() const;
    [[nodiscard]] QString equalizerPresetName() const;
    [[nodiscard]] QVariantList equalizerPresets() const;
    [[nodiscard]] int activeCustomEqualizerPresetId() const;
    [[nodiscard]] int clearBass() const;
    [[nodiscard]] QVariantList equalizerBands() const;
    [[nodiscard]] bool dsee() const;
    [[nodiscard]] bool speakToChat() const;
    [[nodiscard]] bool adaptiveVolume() const;
    [[nodiscard]] int autoPowerOff() const;
    [[nodiscard]] QVariantList autoPowerOffOptions() const { return _autoPowerOffOptions; }
    [[nodiscard]] QString heroImagePath() const;

    [[nodiscard]] bool hasAnc() const;
    [[nodiscard]] bool hasAmbient() const;
    [[nodiscard]] bool hasEqualizer() const;
    [[nodiscard]] bool hasClearBass() const;
    [[nodiscard]] bool tenBandEqualizer() const;
    [[nodiscard]] bool hasDsee() const;
    [[nodiscard]] bool hasSpeakToChat() const;
    [[nodiscard]] bool hasAdaptiveVolume() const;
    [[nodiscard]] bool hasPowerOff() const;
    [[nodiscard]] bool hasWearSensor() const;

    [[nodiscard]] QVariantList pairedDevices() const;
    [[nodiscard]] QVariantList discoveredDevices() const;

    [[nodiscard]] QVariantList multipointDevices() const;
    [[nodiscard]] bool multipointSupported() const;
    [[nodiscard]] bool sourceSwitchSupported() const;
    [[nodiscard]] bool sourceKeepSupported() const;
    [[nodiscard]] bool sourceKeeping() const;
    [[nodiscard]] int playbackIndex() const;
    [[nodiscard]] int maxConnected() const;
    [[nodiscard]] QString multipointError() const;

    [[nodiscard]] bool autostart() const;
    [[nodiscard]] QString appVersion() const;
    [[nodiscard]] QString currentLanguage() const;
    [[nodiscard]] QVariantList availableLanguages() const;
    [[nodiscard]] bool darkTheme() const;

    Q_INVOKABLE void setAnc(bool enabled);
    Q_INVOKABLE void setAmbient(int level, bool focusOnVoice = false);
    Q_INVOKABLE void setNoiseControlOff();
    Q_INVOKABLE void setEqualizerPreset(int preset);
    Q_INVOKABLE void setEqualizerCustom(int clearBass, const QVariantList& bands);
    Q_INVOKABLE void saveEqualizerPreset(const QString& name);
    Q_INVOKABLE void renameEqualizerPreset(int id, const QString& name);
    Q_INVOKABLE void deleteEqualizerPreset(int id);
    Q_INVOKABLE void applyEqualizerPreset(int id);
    Q_INVOKABLE void setDsee(bool enabled);
    Q_INVOKABLE void setSpeakToChat(bool enabled);
    Q_INVOKABLE void setAdaptiveVolume(bool enabled);
    Q_INVOKABLE void setAutoPowerOff(int index);
    Q_INVOKABLE void powerOff();

    Q_INVOKABLE void setAutostart(bool enable);
    Q_INVOKABLE void setLanguage(const QString& langCode);
    Q_INVOKABLE void setDarkTheme(bool enabled);
    Q_INVOKABLE QString t(const QString& key) const;
    Q_INVOKABLE void openUrl(const QString& url);

    Q_INVOKABLE void connectDevice(const QString& address, const QString& name = "");
    Q_INVOKABLE void disconnectDevice();
    Q_INVOKABLE void refreshDiscoveredDevices();
    Q_INVOKABLE void refreshMultipoint();
    Q_INVOKABLE void switchMultipointSource(const QString& address);
    Q_INVOKABLE void setMultipointDefault(const QString& address);
    Q_INVOKABLE void setMultipointSourceKeeping(bool enabled);
    Q_INVOKABLE void multipointConnect(const QString& address);
    Q_INVOKABLE void multipointDisconnect(const QString& address);
    Q_INVOKABLE void multipointUnpair(const QString& address);

signals:
    void stateChanged();
    void capabilitiesChanged();
    void pairedDevicesChanged();
    void discoveredDevicesChanged();
    void multipointChanged();
    void autostartChanged();
    void languageChanged();
    void themeChanged();
    void equalizerPresetsChanged();

private:
    void _applySnapshot(const QByteArray& data);
    void _send(const QString& method, const QJsonObject& params = {});
    void _saveEqualizerPresets();
    void _matchActiveCustomPreset();
    QThread _worker;
    DeviceBackend* _backend{nullptr};
    quint64 _generation{0};
    int _pendingCommands{1};
    quint64 _sessionId{0}, _revision{0};
    QByteArray _lastSnapshot;
    QString _expectedAddress;
    int _connectionIntent{0};
    bool _systemConnected{false};
    bool _busy{true};
    QString _lastError, _connectionState{"searching"}, _codec{"Unknown"};
    QVariantMap _features;
    QJsonObject _capabilities;

    // Cached UI state
    QString _deviceName{""};
    QString _deviceAddress{""};
    bool _connected{false};
    int _batteryLevel{-1};
    bool _isCharging{false};
    QString _noiseControlMode{"unknown"};
    int _ambientLevel{10};
    bool _focusOnVoice{false};
    int _equalizerPreset{0x00};
    QString _equalizerPresetName{"Off"};
    QVariantList _equalizerPresets;
    int _activeCustomEqualizerPresetId{-1};
    int _clearBass{0};
    QVariantList _equalizerBands{0, 0, 0, 0, 0};
    bool _dsee{false};
    bool _speakToChat{false};
    bool _adaptiveVolume{false};
    int _autoPowerOff{0};
    QVariantList _autoPowerOffOptions;
    QVariantList _pairedDevices;
    QVariantList _discoveredDevices;
    QVariantList _multipointDevices;
    bool _multipointSupported{false};
    bool _sourceSwitchSupported{false};
    bool _sourceKeepSupported{false};
    bool _sourceKeeping{false};
    int _playbackIndex{0};
    int _maxConnected{0};
    QString _multipointError;
    QString _currentLanguage{"zh_CN"};
    bool _darkTheme{true};

};

} // namespace sony::devicecenter
