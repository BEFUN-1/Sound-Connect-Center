#include <QtTest>
#include "DeviceCenterController.h"
#include "sony/core/DeviceService.h"
#include "../support/ReplyTransport.h"
#include <chrono>
#include <QSettings>
#include <QTemporaryDir>
#include <condition_variable>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif
using namespace sony;
using namespace sony::devicecenter;
class GatedReplyTransport : public ReplyTransport {
public:
    std::atomic<bool> blockSettings{false}, entered{false}, cancelled{false};
    std::mutex gateMutex;
    std::vector<std::byte> deferred;
    void release() {
        std::lock_guard lock(gateMutex);
        blockSettings = false;
        if (!cancelled && !deferred.empty()) ReplyTransport::send(deferred);
        deferred.clear();
    }
    void cancelPendingIo() noexcept override {
        cancelled = true;
        simulateDisconnect();
        release();
    }
    void connect(const transport::DeviceAddress& address) override {
        cancelled = false;
        ReplyTransport::connect(address);
    }
    size_t send(std::span<const std::byte> data) override {
        std::vector<uint8_t> bytes;
        for (const auto b : data) bytes.push_back(static_cast<uint8_t>(b));
        const auto frame = protocol::FrameCodec::decode(bytes);
        if (blockSettings && frame.type == protocol::DataType::DataMdr &&
            !frame.payload.empty() && frame.payload[0] == 0x68) {
            std::lock_guard lock(gateMutex);
            deferred.assign(data.begin(), data.end());
            entered = true;
            return data.size();
        }
        return ReplyTransport::send(data);
    }
};
class WaitingMultipointTransport : public ReplyTransport {
public:
    std::atomic<unsigned> actions{0};
    bool reportBusy{false};
    void cancelPendingIo() noexcept override { simulateDisconnect(); }
    size_t send(std::span<const std::byte> data) override {
        std::vector<uint8_t> bytes;
        for (const auto byte : data) bytes.push_back(static_cast<uint8_t>(byte));
        const auto frame = protocol::FrameCodec::decode(bytes);
        if (frame.type != protocol::DataType::DataMdrNo2) return ReplyTransport::send(data);
        const auto count = FakeTransport::send(data);
        const auto& payload = frame.payload;
        std::vector<uint8_t> reply;
        const std::string peer = "AA:BB:CC:DD:EE:FF";
        if (payload[0] == 6) reply = {7, 0, 1, 0x32, 0};
        else if (payload[0] == 0x30) reply = {0x31, 2, 8, 2, 0};
        else if (payload[0] == 0x36) {
            reply = {0x37, 2, 1};
            reply.insert(reply.end(), peer.begin(), peer.end());
            reply.insert(reply.end(), {1, 0, 1, 0x0c, 2, 'P', 'C', 0});
        } else if (payload[0] == 0x3c) {
            ++actions;
            if (reportBusy) {
                reply = {0x3d, 2, payload[2], static_cast<uint8_t>((payload[2] << 4) + 3)};
                reply.insert(reply.end(), peer.begin(), peer.end());
            }
        }
        if (!reply.empty()) notify(std::move(reply), protocol::DataType::DataMdrNo2);
        return count;
    }
};
class SlowService : public core::IDeviceService {
public:
    void tick() override { std::this_thread::sleep_for(std::chrono::milliseconds(200)); }
    std::vector<core::DiscoveredDevice> discoverDevices() override { return {}; }
    void connect(const transport::DeviceAddress&, std::string_view) override {}
    void disconnect() noexcept override {}
    void powerOff() override {}
    bool isConnected() const noexcept override { return false; }
    core::SonyDevice* activeDevice() noexcept override { return nullptr; }
    protocol::DeviceStateSnapshot snapshot() const override { return std::make_shared<const protocol::DeviceState>(); }
};
class DeviceControllerTests : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDirectory;
private slots:
    void initTestCase() {
        QVERIFY(settingsDirectory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    }
    void init() {
        QSettings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter").clear();
    }
    void startupUsesOnlyTheRememberedHeadset() {
        auto transport = std::make_shared<ReplyTransport>();
        auto discovery = std::make_shared<transport::FakeDeviceDiscovery>();
        discovery->setDevices({
            {"WH-1000XM6", transport::DeviceAddress("11:22:33:44:55:66"), true, true},
            {"WH-1000XM5", transport::DeviceAddress("22:22:33:44:55:66"), true, true}});
        auto service = std::make_shared<core::DeviceService>(
            transport, discovery, [] { return core::DeviceService::Clock::now(); },
            core::InitialRefresh::None, false);
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
        settings.setValue("connection/selectedAddress", "22:22:33:44:55:66");
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(controller.isConnected(), 2000);
        QCOMPARE(controller.deviceAddress(), QString("22:22:33:44:55:66"));
        QVERIFY(transport->attempts == std::vector<std::string>{"22:22:33:44:55:66"});
    }
    void manualPauseSurvivesRestartAndExplicitConnectResumes() {
        auto transport = std::make_shared<ReplyTransport>();
        auto discovery = std::make_shared<transport::FakeDeviceDiscovery>();
        discovery->setDevices({
            {"WH-1000XM6", transport::DeviceAddress("11:22:33:44:55:66"), true, true}});
        auto makeService = [&] {
            return std::make_shared<core::DeviceService>(
                transport, discovery, [] { return core::DeviceService::Clock::now(); },
                core::InitialRefresh::None, false);
        };
        {
            DeviceCenterController controller(nullptr, makeService());
            QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
            controller.connectDevice("11:22:33:44:55:66", "WH-1000XM6");
            QTRY_VERIFY_WITH_TIMEOUT(controller.isConnected(), 2000);
            controller.disconnectDevice();
            QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
            QVERIFY(!controller.isConnected());
        }
        const auto attempts = transport->attempts.size();
        {
            DeviceCenterController controller(nullptr, makeService());
            QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
            QTest::qWait(1200);
            QVERIFY(!controller.isConnected());
            QCOMPARE(transport->attempts.size(), attempts);
            controller.connectDevice("11:22:33:44:55:66", "WH-1000XM6");
            QTRY_VERIFY_WITH_TIMEOUT(controller.isConnected(), 2000);
        }
    }
    void windowsStartupWaitsWithoutPausingAndExplainsExplicitPause() {
        auto transport = std::make_shared<ReplyTransport>();
        auto discovery = std::make_shared<transport::FakeDeviceDiscovery>();
        discovery->setDevices({
            {"WH-1000XM6", transport::DeviceAddress("11:22:33:44:55:66"), true, false}});
        auto service = std::make_shared<core::DeviceService>(
            transport, discovery, [] { return core::DeviceService::Clock::now(); },
            core::InitialRefresh::None, false, core::DeviceService::RecoveryPolicy::RequireSystemConnection);
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
        settings.setValue("connection/selectedAddress", "11:22:33:44:55:66");
        settings.sync();
        DeviceCenterController controller(nullptr, service);
        QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QString("waiting_for_system_connection"), 2000);
        QCOMPARE(controller.connectionLabel(), controller.t("waiting_for_system_connection"));
        QVERIFY(transport->attempts.empty());
        QVERIFY(service->connectionPreferences().pausedAddresses.empty());
        discovery->setDevices({
            {"WH-1000XM6", transport::DeviceAddress("11:22:33:44:55:66"), true, true}});
        QTRY_VERIFY_WITH_TIMEOUT(controller.isConnected(), 3000);
        controller.disconnectDevice();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(controller.systemConnected(), 2000);
        QVERIFY(!controller.isConnected());
        QCOMPARE(controller.connectionLabel(), controller.t("system_connected_control_paused"));
        QCOMPARE(controller.statusDescription(), controller.t("control_paused_system_connected"));
        const auto attempts = transport->attempts.size();
        QTest::qWait(1100);
        QCOMPARE(transport->attempts.size(), attempts);
    }
    void legacyPauseFollowsAnAlreadyConnectedWindowsHeadset() {
        auto transport = std::make_shared<ReplyTransport>();
        auto discovery = std::make_shared<transport::FakeDeviceDiscovery>();
        discovery->setDevices({
            {"WH-1000XM6", transport::DeviceAddress("11:22:33:44:55:66"), true, true}});
        auto service = std::make_shared<core::DeviceService>(
            transport, discovery, [] { return core::DeviceService::Clock::now(); },
            core::InitialRefresh::None, false, core::DeviceService::RecoveryPolicy::RequireSystemConnection);
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
        settings.setValue("connection/selectedAddress", "11:22:33:44:55:66");
        settings.setValue("connection/pausedAddresses", QStringList{"11:22:33:44:55:66"});
        settings.sync();
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(controller.isConnected(), 2000);
        QCOMPARE(transport->attempts.size(), size_t(1));
        controller.disconnectDevice();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QVERIFY(!controller.isConnected());
        settings.sync();
        QVERIFY(settings.contains("connection/resumeOnSystemConnectionAddresses"));
        QVERIFY(settings.value("connection/resumeOnSystemConnectionAddresses").toStringList().isEmpty());
    }
    void lowercaseAutomaticConnectionAcceptsManualDisconnectSnapshots() {
        auto transport = std::make_shared<ReplyTransport>();
        auto discovery = std::make_shared<transport::FakeDeviceDiscovery>();
        discovery->setDevices({
            {"WH-1000XM6", transport::DeviceAddress("aa:bb:cc:dd:ee:ff"), true, true}});
        auto service = std::make_shared<core::DeviceService>(
            transport, discovery, [] { return core::DeviceService::Clock::now(); },
            core::InitialRefresh::None, false, core::DeviceService::RecoveryPolicy::RequireSystemConnection);
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
        settings.setValue("connection/selectedAddress", "aa:bb:cc:dd:ee:ff");
        settings.sync();
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(controller.isConnected(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        controller.disconnectDevice();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QVERIFY(!service->isConnected());
        QVERIFY(!controller.isConnected());
        QCOMPARE(controller.connectionLabel(), controller.t("system_connected_control_paused"));
        const auto attempts = transport->attempts.size();
        QTest::qWait(1100);
        QCOMPARE(transport->attempts.size(), attempts);
        controller.connectDevice("aa:bb:cc:dd:ee:ff", "WH-1000XM6");
        QTRY_VERIFY_WITH_TIMEOUT(controller.isConnected(), 2000);
    }
    void lowercaseWindowsDiscoveryAddressAcceptsConnectedSnapshots() {
        auto transport = std::make_shared<ReplyTransport>();
        auto discovery = std::make_shared<transport::FakeDeviceDiscovery>();
        discovery->setDevices({
            {"WH-1000XM6", transport::DeviceAddress("aa:bb:cc:dd:ee:ff"), true, true}});
        auto service = std::make_shared<core::DeviceService>(
            transport, discovery, [] { return core::DeviceService::Clock::now(); },
            core::InitialRefresh::None, false, core::DeviceService::RecoveryPolicy::RequireSystemConnection);
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        controller.connectDevice("aa:bb:cc:dd:ee:ff", "WH-1000XM6");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QVERIFY(service->isConnected());
        QVERIFY(controller.isConnected());
        QCOMPARE(controller.deviceAddress(), QString("AA:BB:CC:DD:EE:FF"));
        controller.setAnc(true);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QCOMPARE(controller.noiseControlMode(), QString("cancelling"));
    }
    void firstRunWaitsForSelectionAndRemembersSuccessfulConnection() {
        auto transport = std::make_shared<ReplyTransport>();
        auto discovery = std::make_shared<transport::FakeDeviceDiscovery>();
        discovery->addDevice({"WH-1000XM6", transport::DeviceAddress("11:22:33:44:55:66"), true, true});
        auto service = std::make_shared<core::DeviceService>(
            transport, discovery, [] { return core::DeviceService::Clock::now(); },
            core::InitialRefresh::None, false);
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QVERIFY(!controller.isConnected());
        QVERIFY(transport->attempts.empty());
        controller.connectDevice("11:22:33:44:55:66", "WH-1000XM6");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QVERIFY(controller.isConnected());
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
        QCOMPARE(settings.value("connection/selectedAddress").toString(), QString("11:22:33:44:55:66"));
    }
    void malformedRememberedAddressStillAllowsManualSelection() {
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
        settings.setValue("connection/selectedAddress", "not an address");
        settings.sync();
        auto transport = std::make_shared<ReplyTransport>();
        auto discovery = std::make_shared<transport::FakeDeviceDiscovery>();
        discovery->addDevice({"WH-1000XM6", transport::DeviceAddress("11:22:33:44:55:66"), true, true});
        auto service = std::make_shared<core::DeviceService>(
            transport, discovery, [] { return core::DeviceService::Clock::now(); },
            core::InitialRefresh::None, false);
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QVERIFY(transport->attempts.empty());
        QCOMPARE(controller.discoveredDevices().size(), 1);
        controller.connectDevice("11:22:33:44:55:66", "WH-1000XM6");
        QTRY_VERIFY_WITH_TIMEOUT(controller.isConnected(), 2000);
    }
    void failedSelectionAndRecoveryKeepTheNewTarget() {
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
        settings.setValue("connection/selectedAddress", "22:22:33:44:55:66");
        settings.sync();
        auto transport = std::make_shared<ReplyTransport>();
        auto discovery = std::make_shared<transport::FakeDeviceDiscovery>();
        discovery->addDevice({"WH-1000XM6", transport::DeviceAddress("11:22:33:44:55:66"), true, true});
        discovery->addDevice({"WH-1000XM5", transport::DeviceAddress("22:22:33:44:55:66"), true, true});
        auto service = std::make_shared<core::DeviceService>(
            transport, discovery, [] { return core::DeviceService::Clock::now(); },
            core::InitialRefresh::None, false);
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QVERIFY(controller.isConnected());
        QCOMPARE(controller.deviceAddress(), QString("22:22:33:44:55:66"));
        transport->failAddress = "11:22:33:44:55:66";
        controller.connectDevice("11:22:33:44:55:66", "WH-1000XM6");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QVERIFY(!controller.isConnected());
        QCOMPARE(settings.value("connection/selectedAddress").toString(), QString("11:22:33:44:55:66"));
        transport->failAddress.clear();
        QTRY_VERIFY_WITH_TIMEOUT(controller.isConnected(), 4000);
        QCOMPARE(settings.value("connection/selectedAddress").toString(), QString("11:22:33:44:55:66"));
    }
    void startupDoesNotBlockGui() {
        auto service = std::make_shared<SlowService>();
        QElapsedTimer elapsed; elapsed.start();
        DeviceCenterController controller(nullptr, service);
        QVERIFY(elapsed.elapsed() < 100);
        QVERIFY(controller.busy());
        bool guiTick = false;
        QTimer::singleShot(10, [&] { guiTick = true; });
        QTRY_VERIFY_WITH_TIMEOUT(guiTick, 100);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QCOMPARE(controller.batteryLevel(), -1);
        QCOMPARE(controller.noiseControlMode(), QString("unknown"));
        QCOMPARE(controller.codec(), QString("Unknown"));
    }
    void equalizerCapabilities_data() {
        QTest::addColumn<QString>("model");
        QTest::addColumn<bool>("tenBand");
        QTest::newRow("WH-1000XM6") << QString("WH-1000XM6") << true;
        QTest::newRow("WH-1000XM5") << QString("WH-1000XM5") << false;
    }
    void equalizerCapabilities() {
        QFETCH(QString, model);
        QFETCH(bool, tenBand);
        auto transport = std::make_shared<ReplyTransport>();
        auto service = std::make_shared<core::DeviceService>(transport);
        service->connect(transport::DeviceAddress("11:22:33:44:55:66"), model.toStdString());
        DeviceCenterController controller(nullptr, service);
        QSignalSpy capabilitiesChanged(&controller, &DeviceCenterController::capabilitiesChanged);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QVERIFY(!capabilitiesChanged.isEmpty());
        QVERIFY(controller.hasEqualizer());
        QCOMPARE(controller.tenBandEqualizer(), tenBand);
        QCOMPARE(controller.property("tenBandEqualizer"), QVariant(tenBand));
        QCOMPARE(controller.hasClearBass(), !tenBand);
    }
    void failedActionPreservesConfirmedValue() {
        auto transport = std::make_shared<ReplyTransport>();
        auto service = std::make_shared<core::DeviceService>(transport);
        service->connect(transport::DeviceAddress("11:22:33:44:55:66"),"WH-1000XM5");
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),2000);
        QCOMPARE(controller.noiseControlMode(),QString("cancelling"));
        transport->simulateTimeoutOnSend();
        controller.setNoiseControlOff();
        QVERIFY(controller.busy());
        QCOMPARE(controller.noiseControlMode(),QString("cancelling"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),3000);
        QVERIFY(!controller.lastError().isEmpty());
        QCOMPARE(controller.noiseControlMode(),QString("cancelling"));
    }
    void trayDeviceCommandsCompleteWithoutReentrantFailure() {
        auto transport = std::make_shared<ReplyTransport>();
        auto service = std::make_shared<core::DeviceService>(transport);
        service->connect(transport::DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);

        controller.setNoiseControlOff();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 3000);
        QCOMPARE(controller.noiseControlMode(), QString("off"));

        controller.powerOff();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 3000);
        QVERIFY(!controller.isConnected());
    }
    void languageChangesWithoutRestart() {
        auto service = std::make_shared<SlowService>();
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        const auto original = controller.currentLanguage();
        QSignalSpy languageChanged(&controller, &DeviceCenterController::languageChanged);

        controller.setLanguage("en");
        QCOMPARE(controller.currentLanguage(), QString("en"));
        QCOMPARE(controller.t("nav_overview"), QString("Overview"));

        controller.setLanguage("zh_CN");
        QCOMPARE(controller.currentLanguage(), QString("zh_CN"));
        QCOMPARE(controller.t("nav_overview"), QString::fromUtf8("概览"));
        QCOMPARE(controller.t("speak_title"), QString::fromUtf8("智能免摘"));
        QCOMPARE(controller.t("auto_power_off"), QString::fromUtf8("自动关闭电源"));
        QVERIFY(languageChanged.count() >= 2);

        const QList<QPair<QString, QString>> translatedOverviews{
            {"pt_BR", "Visão Geral"},
            {"es", "Resumen"},
            {"de", "Übersicht"},
            {"fr", "Aperçu"},
            {"ja", "概要"},
        };
        for (const auto& [locale, overview] : translatedOverviews) {
            controller.setLanguage(locale);
            QCOMPARE(controller.t("nav_overview"), overview);
            QVERIFY(!controller.t("audio_features_title").isEmpty());
            QVERIFY(controller.t("tray_power_off") != QString("Power Off Headphones"));
        }
        QVERIFY(languageChanged.count() >= 7);

        controller.setLanguage(original);
    }
    void notificationsUpdateStateAndBands() {
        auto transport = std::make_shared<ReplyTransport>();
        auto service = std::make_shared<core::DeviceService>(transport);
        service->connect(transport::DeviceAddress("11:22:33:44:55:66"),"WH-1000XM5");
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),2000);
        QCOMPARE(controller.equalizerBands().size(),5);
        QCOMPARE(controller.equalizerBands()[4].toInt(),5);
        transport->notify({0x69,0x17,1,1,1,0,12});
        QTRY_COMPARE_WITH_TIMEOUT(controller.noiseControlMode(),QString("ambient"),1000);
        QCOMPARE(controller.ambientLevel(),12);
    }
    void queuedActionsKeepDiscreteOperationsAndFinalSliderValue() {
        auto transport = std::make_shared<GatedReplyTransport>();
        auto service = std::make_shared<core::DeviceService>(transport);
        service->connect(transport::DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        const auto before = transport->sentFrames().size();
        transport->blockSettings = true;
        controller.setAmbient(1);
        QTRY_VERIFY_WITH_TIMEOUT(transport->entered.load(), 1000);
        controller.setAnc(true);
        controller.setAmbient(3);
        controller.setAmbient(7);
        controller.setNoiseControlOff();
        transport->notify({0x69,0x17,1,1,1,0,12});
        QTRY_COMPARE_WITH_TIMEOUT(controller.ambientLevel(), 12, 1000);
        QVERIFY(controller.busy());
        transport->release();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 3000);
        QCOMPARE(controller.noiseControlMode(), QString("off"));
        QVERIFY2(controller.lastError().isEmpty(), qPrintable(controller.lastError()));
        std::vector<std::vector<uint8_t>> settings;
        const auto frames = transport->sentFrames();
        for (size_t i = before; i < frames.size(); ++i) {
            const auto frame = protocol::FrameCodec::decode(frames[i]);
            if (frame.type == protocol::DataType::DataMdr &&
                !frame.payload.empty() && frame.payload[0] == 0x68) settings.push_back(frame.payload);
        }
        QCOMPARE(settings.size(), size_t{4});
        QCOMPARE(settings[2][6], uint8_t{7});
    }
    void disconnectCancelsActiveAndUnsentOperations() {
        auto transport = std::make_shared<GatedReplyTransport>();
        auto service = std::make_shared<core::DeviceService>(transport);
        service->connect(transport::DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        transport->blockSettings = true;
        controller.setAmbient(1);
        QTRY_VERIFY_WITH_TIMEOUT(transport->entered.load(), 1000);
        controller.setAnc(true);
        controller.setDsee(true);
        QElapsedTimer timer;
        timer.start();
        controller.disconnectDevice();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 1000);
        QVERIFY(timer.elapsed() < 1000);
        QVERIFY(!controller.isConnected());
        QCOMPARE(controller.connectionState(), QString("manually_disconnected"));
        const auto frames = transport->sentFrames();
        for (const auto& bytes : frames) {
            const auto frame = protocol::FrameCodec::decode(bytes);
            QVERIFY(frame.payload.empty() || frame.payload[0] != 0xe8);
        }
    }
    void queueOverflowHasExplicitFeedbackAndStillDrains() {
        auto transport = std::make_shared<GatedReplyTransport>();
        auto service = std::make_shared<core::DeviceService>(transport);
        service->connect(transport::DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        transport->blockSettings = true;
        controller.setAmbient(1);
        QTRY_VERIFY_WITH_TIMEOUT(transport->entered.load(), 1000);
        for (int i = 0; i < 34; ++i) controller.setAnc(true);
        QTRY_VERIFY_WITH_TIMEOUT(controller.lastError().contains("queue is full"), 1000);
        transport->release();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 4000);
        QVERIFY(controller.isConnected());
    }
    void exitCancelsOutstandingProtocolWait() {
        auto transport = std::make_shared<GatedReplyTransport>();
        auto service = std::make_shared<core::DeviceService>(transport);
        service->connect(transport::DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
        auto controller = std::make_unique<DeviceCenterController>(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 2000);
        transport->blockSettings = true;
        controller->setAmbient(1);
        QTRY_VERIFY_WITH_TIMEOUT(transport->entered.load(), 1000);
        QElapsedTimer timer;
        timer.start();
        controller.reset();
        QVERIFY(timer.elapsed() < 1000);
        QVERIFY(service->connectionPreferences().pausedAddresses.empty());
    }
    void twelveSecondMultipointWaitKeepsGuiAndNotificationsResponsive() {
        auto transport = std::make_shared<WaitingMultipointTransport>();
        auto service = std::make_shared<core::DeviceService>(transport, nullptr,
            [] { return core::DeviceService::Clock::now(); }, core::InitialRefresh::All, false);
        service->connect(transport::DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QTimer heartbeat;
        heartbeat.setInterval(20);
        QSignalSpy beats(&heartbeat, &QTimer::timeout);
        heartbeat.start();
        QElapsedTimer elapsed;
        elapsed.start();
        controller.multipointConnect("AA:BB:CC:DD:EE:FF");
        QTRY_COMPARE_WITH_TIMEOUT(transport->actions.load(), 1u, 1000);
        transport->notify({0x25, 0, 84, 0});
        transport->notify({0x69, 0x17, 1, 1, 1, 0, 12});
        QTRY_COMPARE_WITH_TIMEOUT(controller.batteryLevel(), 84, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(controller.ambientLevel(), 12, 1000);
        controller.setNoiseControlOff();
        QVERIFY(controller.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 15000);
        QVERIFY(elapsed.elapsed() >= 11500);
        QVERIFY(beats.count() >= 50);
        QCOMPARE(transport->actions.load(), 1u);
        QCOMPARE(controller.noiseControlMode(), QString("off"));
        QCOMPARE(controller.batteryLevel(), 84);
        QVERIFY(controller.isConnected());
        QVERIFY(controller.lastError().contains("Timeout", Qt::CaseInsensitive));
    }
    void disconnectCancelsMultipointBusyBackoff() {
        auto transport = std::make_shared<WaitingMultipointTransport>();
        transport->reportBusy = true;
        auto service = std::make_shared<core::DeviceService>(transport, nullptr,
            [] { return core::DeviceService::Clock::now(); }, core::InitialRefresh::All, false);
        service->connect(transport::DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        controller.multipointConnect("AA:BB:CC:DD:EE:FF");
        QTRY_COMPARE_WITH_TIMEOUT(transport->actions.load(), 1u, 1000);
        QTest::qWait(100);
        QVERIFY(controller.busy());
        QElapsedTimer elapsed;
        elapsed.start();
        controller.disconnectDevice();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 1000);
        QVERIFY(elapsed.elapsed() < 1000);
        QVERIFY(!controller.isConnected());
        QTest::qWait(600);
        QCOMPARE(transport->actions.load(), 1u);
    }
    void acknowledgedButIgnoredActionKeepsVisibleConfirmedMode() {
        auto transport = std::make_shared<ReplyTransport>();
        auto service = std::make_shared<core::DeviceService>(transport);
        service->connect(transport::DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        transport->acceptSettings = false;
        controller.setNoiseControlOff();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QVERIFY(controller.isConnected());
        QCOMPARE(controller.noiseControlMode(), QString("cancelling"));
        QCOMPARE(controller.batteryLevel(), 85);
        QCOMPARE(controller.featureStatus().value("noiseControl").toMap()
            .value("availability").toString(), QString("stale"));
        QVERIFY(controller.statusDescription().contains(controller.t("status_update_delayed")));
        QTest::qWait(1100);
        QCOMPARE(controller.noiseControlMode(), QString("cancelling"));
    }
    void destructionDrainsWorkerAndCallbacks() {
        auto service = std::make_shared<SlowService>();
        auto controller = std::make_unique<DeviceCenterController>(nullptr,service);
        QTest::qWait(20);
        controller.reset();
        QCoreApplication::processEvents();
    }
    void reconnectionReleasesWorkerHandles() {
#ifdef Q_OS_WIN
        auto transport = std::make_shared<ReplyTransport>();
        auto service = std::make_shared<core::DeviceService>(transport, nullptr,
            [] { return core::DeviceService::Clock::now(); }, core::InitialRefresh::None, false);
        DeviceCenterController controller(nullptr, service);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        const auto reconnect = [&] {
            controller.connectDevice("11:22:33:44:55:66", "WH-1000XM5");
            QTRY_VERIFY_WITH_TIMEOUT(controller.isConnected() && !controller.busy(), 2000);
            controller.disconnectDevice();
            QTRY_VERIFY_WITH_TIMEOUT(!controller.isConnected() && !controller.busy(), 2000);
        };
        for (int cycle = 0; cycle < 5; ++cycle) reconnect();
        QTest::qWait(100);
        DWORD before = 0, after = 0;
        QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &before));
        for (int cycle = 0; cycle < 30; ++cycle) reconnect();
        QTest::qWait(200);
        QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &after));
        qInfo() << "Worker handles before:" << before << "after 30 cycles:" << after;
        QVERIFY(after <= before + 8);
#else
        QSKIP("Windows handle accounting");
#endif
    }
};
QTEST_GUILESS_MAIN(DeviceControllerTests)
#include "DeviceControllerTests.moc"
