#include "UiScenario.h"
#include "DeviceCenterController.h"
#include "../support/CallHandoffTransport.h"
#include "sony/core/DeviceService.h"
#include "sony/transport/PlatformTransport.h"
#include "sony/transport/Logger.h"
#include <QtTest>
#include <QAction>
#include <QMenu>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QTextStream>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

using namespace sony;
using namespace sony::devicecenter;

namespace {
std::shared_ptr<CallHandoffTransport> testTransport;
std::shared_ptr<transport::FakeDeviceDiscovery> testDiscovery;
std::atomic<int64_t> testNowMillis{0};
QTemporaryDir settingsDirectory;

class ObservedHardwareTransport : public transport::ITransport {
    std::shared_ptr<transport::ITransport> inner = transport::createPlatformTransport();
public:
    std::atomic<unsigned> connections{0}, batteryQueries{0};
    void connect(const transport::DeviceAddress& address) override { ++connections; inner->connect(address); }
    void disconnect() noexcept override { inner->disconnect(); }
    void cancelPendingIo() noexcept override { inner->cancelPendingIo(); }
    bool isConnected() const noexcept override { return inner->isConnected(); }
    size_t send(std::span<const std::byte> data) override {
        std::vector<uint8_t> bytes;
        for (auto byte : data) bytes.push_back(static_cast<uint8_t>(byte));
        const auto frame = protocol::FrameCodec::decode(bytes);
        if (!frame.payload.empty() && (frame.payload[0] == 0x66 || frame.payload[0] == 0x68))
            fprintf(stderr, "HARDWARE_MODE_TX %s\n", Logger::formatHex(frame.payload).c_str());
        if (frame.type == protocol::DataType::DataMdr && frame.payload == std::vector<uint8_t>{0x22, 0})
            ++batteryQueries;
        return inner->send(data);
    }
    size_t receive(std::span<std::byte> data) override { return inner->receive(data); }
    std::optional<protocol::SonyProtocolVersion> protocolVersion() const noexcept override {
        return inner->protocolVersion();
    }
};
std::shared_ptr<ObservedHardwareTransport> hardwareTransport;
std::shared_ptr<core::DeviceService> hardwareService;
QString hardwareAddress;

class HardwareScenario : public QObject {
    Q_OBJECT
    protocol::NoiseControlState originalNoise;
    bool restoreNoise{false};
public:
    DeviceCenterController* controller;
    QQuickWindow* window;
    QMenu* menu;
    QSystemTrayIcon* tray;
private slots:
    void connectedHeadsetRestoresControlAndHonorsManualPause() {
        QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected() && !controller->busy(), 30000);
        QVERIFY(controller->systemConnected());
        QCOMPARE(controller->deviceName(), QString("WH-1000XM6"));
        QVERIFY(controller->batteryLevel() >= 0);
        QVERIFY(controller->featureStatus().value("noiseControl").toMap()
            .value("availability").toString() == "valid");
        originalNoise = hardwareService->snapshot()->noiseControl;
        restoreNoise = true;
        qInfo() << "REAL HEADSET: connected, battery" << controller->batteryLevel()
                << "firmware" << QString::fromStdString(hardwareService->snapshot()->firmware);
        QMenu* modes = nullptr;
        for (auto* action : menu->actions()) if (action->menu()) modes = action->menu();
        QVERIFY(modes);
        const QStringList expected{"cancelling", "ambient", "off"};
        for (int cycle = 0; cycle < 9; ++cycle) {
            const auto index = cycle % 3;
            modes->actions()[index]->trigger();
            QTRY_COMPARE_WITH_TIMEOUT(controller->noiseControlMode(), expected[index], 10000);
            QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 10000);
            if (!controller->lastError().isEmpty())
                qInfo() << "Mode confirmation error; current snapshot"
                        << controller->noiseControlMode() << controller->lastError();
            QVERIFY2(controller->lastError().isEmpty(), qPrintable(controller->lastError()));
            qInfo() << "REAL HEADSET: tray mode confirmed" << expected[index];
            QTest::qWait(1200);
            QCOMPARE(controller->noiseControlMode(), expected[index]);
        }
        controller->disconnectDevice();
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 5000);
        QVERIFY(!controller->isConnected());
        QTRY_VERIFY_WITH_TIMEOUT(controller->systemConnected(), 3000);
        const auto attempts = hardwareTransport->connections.load();
        QTest::qWait(5000);
        QCOMPARE(hardwareTransport->connections.load(), attempts);
        QVERIFY(!controller->isConnected());
        QCOMPARE(controller->connectionLabel(), controller->t("system_connected_control_paused"));
        qInfo() << "REAL HEADSET: manual pause held; Windows link retained; no reconnect";
        controller->connectDevice(hardwareAddress, "WH-1000XM6");
        QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected() && !controller->busy(), 30000);
        QCOMPARE(hardwareTransport->connections.load(), attempts + 1);
        QVERIFY(controller->batteryLevel() >= 0);
        const auto updated = controller->featureStatus().value("battery").toMap().value("lastSuccessMs").toLongLong();
        const auto queries = hardwareTransport->batteryQueries.load();
        window->hide();
        QTRY_VERIFY_WITH_TIMEOUT(
            controller->featureStatus().value("battery").toMap().value("lastSuccessMs").toLongLong() > updated, 35000);
        QVERIFY(controller->isConnected());
        QVERIFY(tray->toolTip().contains(QString::number(controller->batteryLevel())));
        qInfo() << "REAL HEADSET: hidden-window battery updated; additional queries"
                << hardwareTransport->batteryQueries.load() - queries;
        window->show();
    }
    void physicalSystemReconnect() {
        if (qEnvironmentVariable("SONY_HARDWARE_WAIT_RECONNECT") != "1")
            QSKIP("Physical Windows/headset disconnect and reconnect not requested");
        QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected() && !controller->busy(), 30000);
        qInfo() << "PHYSICAL_RECONNECT_READY: waiting for a real system disconnect";
        QElapsedTimer elapsed;
        elapsed.start();
        while (controller->systemConnected() && elapsed.elapsed() < 60000) QTest::qWait(100);
        if (controller->systemConnected())
            QSKIP("No physical/system disconnect observed; this scenario remains unverified");
        QTRY_VERIFY_WITH_TIMEOUT(!controller->isConnected(), 5000);
        const auto attempts = hardwareTransport->connections.load();
        elapsed.restart();
        while (!controller->systemConnected() && elapsed.elapsed() < 90000) {
            QCOMPARE(hardwareTransport->connections.load(), attempts);
            QTest::qWait(100);
        }
        QVERIFY2(controller->systemConnected(), "Headset did not reconnect to Windows");
        QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected(), 30000);
        QCOMPARE(hardwareTransport->connections.load(), attempts + 1);
        QVERIFY(controller->batteryLevel() >= 0);
        qInfo() << "REAL HEADSET: no control attempts while disconnected; resumed after Windows reconnect";
    }
    void cleanupTestCase() {
        if (!restoreNoise) return;
        if (!controller->isConnected()) {
            controller->connectDevice(hardwareAddress, "WH-1000XM6");
            QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected() && !controller->busy(), 30000);
        }
        controller->setAmbient(originalNoise.ambientLevel, originalNoise.focusOnVoice);
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 10000);
        if (originalNoise.mode == protocol::NoiseControlMode::NoiseCancelling) controller->setAnc(true);
        else if (originalNoise.mode == protocol::NoiseControlMode::Off) controller->setNoiseControlOff();
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 10000);
        QVERIFY2(controller->lastError().isEmpty(), qPrintable(controller->lastError()));
        const auto confirmed = hardwareService->snapshot()->noiseControl;
        QCOMPARE(confirmed.mode, originalNoise.mode);
        QCOMPARE(confirmed.ambientLevel, originalNoise.ambientLevel);
        QCOMPARE(confirmed.focusOnVoice, originalNoise.focusOnVoice);
        qInfo() << "REAL HEADSET: restored original noise-control settings";
    }
};

QQuickItem* findVisualItem(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems())
        if (auto* found = findVisualItem(child, name)) return found;
    return nullptr;
}

class UiScenario : public QObject {
    Q_OBJECT
public:
    DeviceCenterController* controller;
    QQuickWindow* window;
    QMenu* menu;
    QSystemTrayIcon* tray;

private slots:
    void captureDocumentationScreenshots() {
        const auto destination = qEnvironmentVariable("SONY_SCREENSHOT_DIR");
        if (destination.isEmpty())
            QSKIP("Set SONY_SCREENSHOT_DIR to capture documentation with synthetic devices");
        QVERIFY(testTransport && testDiscovery);
        QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected() && !controller->busy(), 5000);
        QVERIFY(QDir().mkpath(destination));
        controller->setLanguage("zh_CN");
        controller->setDarkTheme(true);
        window->resize(1280, 840);
        window->show();
        const QStringList pages{"overview", "noise-control", "equalizer", "audio-features"};
        for (int page = 0; page < pages.size(); ++page) {
            window->setProperty("navIndex", page);
            QTest::qWait(1000);
            QVERIFY(window->grabWindow().save(QDir(destination).filePath(pages[page] + ".png")));
        }
        window->setProperty("navIndex", 5);
        QTest::qWait(1000);
        QVERIFY(window->grabWindow().save(QDir(destination).filePath("settings.png")));
        controller->setLanguage("en");
        window->setProperty("navIndex", 0);
        QTest::qWait(1000);
        QVERIFY(window->grabWindow().save(QDir(destination).filePath("overview-en.png")));
    }

    void trayRestoreLatency() {
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 2000);
        window->setProperty("navIndex", 0);
        QList<qint64> samples;
        for (int sample = 0; sample < 35; ++sample) {
            window->hide();
            QTest::qWait(30);
            std::atomic<qint64> presentedNanos{-1};
            QElapsedTimer timer;
            const auto connection = connect(window, &QQuickWindow::frameSwapped,
                this, [&] { qint64 unset = -1; presentedNanos.compare_exchange_strong(unset, timer.nsecsElapsed()); }, Qt::DirectConnection);
            timer.start();
            QVERIFY(QMetaObject::invokeMethod(tray, "activated",
                Q_ARG(QSystemTrayIcon::ActivationReason, QSystemTrayIcon::DoubleClick)));
            QTRY_VERIFY_WITH_TIMEOUT(window->isVisible() && presentedNanos.load() >= 0, 2000);
            if (sample >= 5) samples.push_back(presentedNanos.load() / 1000000);
            disconnect(connection);
        }
        QFile file("tray-restore-ms.csv");
        QVERIFY(file.open(QIODevice::WriteOnly));
        QTextStream out(&file);
        out << "sample,milliseconds\n";
        for (int i = 0; i < samples.size(); ++i) out << i + 1 << ',' << samples[i] << '\n';
        std::sort(samples.begin(), samples.end());
        qInfo() << "Tray restore samples:" << samples.size() << "P95 ms:" << samples[28];
    }
    void simulatedTrayResourceSoak() {
        if (qEnvironmentVariableIsEmpty("SONY_SOAK_SECONDS")) QSKIP("Set SONY_SOAK_SECONDS for real-time simulated soak");
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 2000);
        const auto duration = qEnvironmentVariableIntValue("SONY_SOAK_SECONDS");
        QVERIFY(duration >= 60);
        window->hide();
        QFile file("tray-resource-soak.csv");
        QVERIFY(file.open(QIODevice::WriteOnly));
        QTextStream out(&file);
        out << "seconds,private_bytes,working_set,handles,connections,gdi_objects,user_objects,battery_queries,max_battery_gap_seconds\n";
        QElapsedTimer elapsed;
        elapsed.start();
        int cycle = -1;
        qint64 lastBatteryRead = 0, maxBatteryGap = 0;
        unsigned batteryQueries = 0;
        while (elapsed.elapsed() < qint64(duration) * 1000) {
            const auto seconds = elapsed.elapsed() / 1000;
            if (seconds / 60 != cycle) {
                cycle = int(seconds / 60);
                if (cycle > 0) {
                    controller->disconnectDevice();
                    QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 2000);
                    controller->connectDevice("11:22:33:44:55:66", "WH-1000XM6");
                    QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected() && !controller->busy(), 2000);
                }
            }
#ifdef Q_OS_WIN
            bool batteryRead = false;
            for (const auto& bytes : testTransport->sentFrames()) {
                const auto frame = protocol::FrameCodec::decode(bytes);
                if (frame.type == protocol::DataType::DataMdr && !frame.payload.empty() &&
                    frame.payload[0] == 0x22) {
                    ++batteryQueries;
                    batteryRead = true;
                }
            }
            maxBatteryGap = (std::max)(maxBatteryGap, seconds - lastBatteryRead);
            if (batteryRead) lastBatteryRead = seconds;
            PROCESS_MEMORY_COUNTERS_EX memory{};
            memory.cb = sizeof(memory);
            DWORD handles = 0;
            QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)));
            QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &handles));
            out << seconds << ',' << memory.PrivateUsage << ',' << memory.WorkingSetSize << ',' << handles << ','
                << testTransport->connections.load() << ',' << GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)
                << ',' << GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) << ',' << batteryQueries
                << ',' << maxBatteryGap << '\n';
            out.flush();
#endif
            testTransport->clearSent();
            testNowMillis += 5000;
            QTest::qWait(5000);
            QVERIFY(controller->isConnected());
        }
    }
    void supportedAutoPowerOffChoices() {
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 2000);
        window->setProperty("navIndex", 3);
        auto* combo = window->findChild<QQuickItem*>("autoPowerOffCombo");
        QVERIFY(combo);
        QCOMPARE(combo->property("count").toInt(), 2);
        QCOMPARE(controller->autoPowerOffOptions(), (QVariantList{0, 5}));
        auto* toggle = window->findChild<QQuickItem*>("autoPowerOffSwitch");
        auto* description = window->findChild<QQuickItem*>("autoPowerOffDescription");
        QVERIFY(toggle);
        QVERIFY(description);
        QTRY_VERIFY(toggle->isVisible());
        QVERIFY(!combo->isVisible());
        QCOMPARE(description->property("text").toString(), controller->t("auto_power_off_removed_desc"));
        for (const auto expected : {5, 0}) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2)).toPoint());
            QTRY_COMPARE_WITH_TIMEOUT(controller->autoPowerOff(), expected, 2000);
            QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 2000);
            QCOMPARE(toggle->property("checked").toBool(), expected == 5);
        }
        QVERIFY2(controller->lastError().isEmpty(), qPrintable(controller->lastError()));
        controller->setSpeakToChat(true);
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 2000);
        QVERIFY(controller->speakToChat());
        controller->setLanguage("zh_CN");
        QCOMPARE(controller->currentLanguage(), QString("zh_CN"));
        auto* speakTitle = findVisualItem(window->contentItem(), "speakTitle");
        auto* speakDescription = findVisualItem(window->contentItem(), "speakDescription");
        auto* powerTitle = window->findChild<QQuickItem*>("autoPowerOffTitle");
        QVERIFY(speakTitle);
        QVERIFY(speakDescription);
        QVERIFY(powerTitle);
        QTRY_COMPARE(speakTitle->property("text").toString(), controller->t("speak_title"));
        QTRY_COMPARE(speakDescription->property("text").toString(), controller->t("speak_desc"));
        QTRY_COMPARE(powerTitle->property("text").toString(), controller->t("auto_power_off"));
        QTRY_COMPARE(description->property("text").toString(),
                     controller->t("auto_power_off_removed_desc"));
        QTest::qWait(250);
        QVERIFY(window->grabWindow().save("xm6-auto-power-off-switch.png"));
    }

    void callHandoffRestoresControlWithoutRestart() {
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 2000);
        QVERIFY(controller->isConnected());
        const auto connections = testTransport->connections.load();
        const auto closed = testTransport->closedConnections.load();
        testTransport->failure = CallHandoffTransport::Failure::AckAndNotifications;
        testNowMillis += 5000;
        QTRY_VERIFY_WITH_TIMEOUT(!controller->featureStatus().value("noiseControl")
            .toMap().value("error").toString().isEmpty(), 5000);
        QVERIFY(controller->isConnected());
        testNowMillis += 5000;
        QTRY_VERIFY_WITH_TIMEOUT(!controller->isConnected(), 4500);
        QCOMPARE(controller->connectionState(), QString("retrying"));
        QTRY_VERIFY_WITH_TIMEOUT(testTransport->connections.load() > connections, 6000);
        QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected(), 2000);
        QCOMPARE(controller->deviceAddress(), QString("11:22:33:44:55:66"));
        QCOMPARE(testTransport->connections.load(), connections + 1);
        QCOMPARE(testTransport->closedConnections.load(), closed + 1);
        QCOMPARE(testTransport->reopenedWithoutClose.load(), 0u);
        QVERIFY(controller->featureStatus().value("noiseControl").toMap().value("error").toString().isEmpty());
        controller->setNoiseControlOff();
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 2000);
        QCOMPARE(controller->noiseControlMode(), QString("off"));
        QVERIFY2(controller->lastError().isEmpty(), qPrintable(controller->lastError()));
    }

    void actualTrayMenuAndPowerOff() {
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 2000);
        QVERIFY(controller->isConnected());
        QVERIFY(tray->icon().pixmap(64, 64).save("tray-battery-85.png"));
        QMenu* modes = nullptr;
        QAction* power = nullptr;
        for (auto* action : menu->actions()) {
            if (action->menu()) modes = action->menu();
            if (action->text() == controller->t("tray_power_off")) power = action;
        }
        QVERIFY(modes);
        QVERIFY(power);
        // Use the real menu's event handling, including its submenu lifetime.
        for (int i = 0; i < 9; ++i) {
#ifdef Q_OS_WIN
            QVERIFY(QMetaObject::invokeMethod(tray, "activated",
                Q_ARG(QSystemTrayIcon::ActivationReason, QSystemTrayIcon::Context)));
#else
            menu->popup(QPoint(10, 10));
#endif
            QTRY_VERIFY(menu->isVisible());
            menu->setActiveAction(modes->menuAction());
            QTest::keyClick(menu, Qt::Key_Right);
            QTRY_VERIFY(modes->isVisible());
            auto* selected = modes->actions()[i % 3];
            QSignalSpy triggered(selected, &QAction::triggered);
            modes->setActiveAction(selected);
            QTest::keyClick(modes, Qt::Key_Return);
            QTRY_COMPARE_WITH_TIMEOUT(triggered.count(), 1, 2000);
            QTRY_VERIFY(!menu->isVisible());
            const QStringList expectedModes{"cancelling", "ambient", "off"};
            // The tray timer starts the command after the menu has closed.
            QTRY_COMPARE_WITH_TIMEOUT(controller->noiseControlMode(), expectedModes[i % 3], 2000);
            QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 2000);
            QVERIFY2(controller->lastError().isEmpty(), qPrintable(controller->lastError()));
        }
        menu->popup(QPoint(10, 10));
        menu->setActiveAction(power);
        QTest::keyClick(menu, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!controller->isConnected(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 2000);
        QVERIFY(controller->lastError().isEmpty());
    }

    void powerOnReconnectUpdatesWindowWithoutRestart() {
        window->setProperty("navIndex", 3);
        auto* status = window->findChild<QQuickItem*>("headsetConnectionStatus");
        auto* power = window->findChild<QQuickItem*>("headsetPowerOffButton");
        QVERIFY(status);
        QVERIFY(power);
        const auto setSystemConnection = [](bool connected) {
            testDiscovery->setDevices({
                {"WH-1000XM6", transport::DeviceAddress("11:22:33:44:55:66"), true, connected}});
        };
        for (int cycle = 0; cycle < 2; ++cycle) {
            if (controller->isConnected()) {
                QTRY_VERIFY(power->isVisible() && power->isEnabled());
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    power->mapToScene(QPointF(power->width() / 2, power->height() / 2)).toPoint());
                QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 2000);
                QTRY_VERIFY_WITH_TIMEOUT(!controller->isConnected(), 2000);
            }
            QCOMPARE(status->property("text").toString(), controller->t("waiting_for_power_on_short"));
            const auto attempts = testTransport->attempts.size();
            setSystemConnection(false);
            testNowMillis += 6000;
            QTest::qWait(1100); // Allow the actual backend's maintenance timer to observe the OS link.
            QVERIFY(!controller->isConnected());
            QCOMPARE(testTransport->attempts.size(), attempts);

            setSystemConnection(true);
            testNowMillis += 1000;
            QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected(), 4000);
            QCOMPARE(status->property("text").toString(), controller->t("connected"));
            QCOMPARE(testTransport->attempts.size(), attempts + 1);
            QVERIFY(power->isEnabled());
            controller->setNoiseControlOff();
            QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 2000);
            QCOMPARE(controller->noiseControlMode(), QString("off"));
            QVERIFY2(controller->lastError().isEmpty(), qPrintable(controller->lastError()));
        }
    }

    void pauseLabelDistinguishesSystemAndSoftwareConnections() {
        auto* status = window->findChild<QQuickItem*>("headsetConnectionStatus");
        QVERIFY(status);
        controller->disconnectDevice();
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(controller->systemConnected(), 2000);
        QVERIFY(!controller->isConnected());
        QCOMPARE(status->property("text").toString(), controller->t("system_connected_control_paused"));
        const auto attempts = testTransport->attempts.size();
        QTest::qWait(1100);
        QCOMPARE(testTransport->attempts.size(), attempts);
        controller->connectDevice("11:22:33:44:55:66", "WH-1000XM6");
        QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected(), 2000);
        QCOMPARE(status->property("text").toString(), controller->t("connected"));
    }

    void languagePopupUpdatesLive() {
        window->setProperty("navIndex", 5);
        auto* combo = window->findChild<QQuickItem*>("languageCombo");
        auto* title = window->findChild<QQuickItem*>("settingsTitle");
        QVERIFY(combo);
        QVERIFY(title);
        for (int round = 0; round < 5; ++round) {
            const auto languages = controller->availableLanguages();
            for (int i = 0; i < languages.size(); ++i) {
                auto* popup = combo->property("popup").value<QObject*>();
                QVERIFY(popup);
                QVERIFY(QMetaObject::invokeMethod(popup, "open"));
                QTest::qWait(5);
                combo->setProperty("currentIndex", i);
                QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, i)));
                QVERIFY(QMetaObject::invokeMethod(popup, "close"));
                QCoreApplication::processEvents();
                QCOMPARE(controller->currentLanguage(), languages[i].toMap()["code"].toString());
                QCOMPARE(combo->property("displayText").toString(), languages[i].toMap()["name"].toString());
                QVariant translated;
                QVERIFY(QMetaObject::invokeMethod(window, "tr", Q_RETURN_ARG(QVariant, translated),
                                                  Q_ARG(QVariant, QVariant("nav_settings"))));
                QCOMPARE(translated.toString(), controller->t("nav_settings"));
                QCOMPARE(title->property("text").toString(), controller->t("settings_title"));
                auto* connectionStatus = window->findChild<QQuickItem*>("headsetConnectionStatus");
                QVERIFY(connectionStatus);
                QCOMPARE(connectionStatus->property("text").toString(), controller->connectionLabel());
                auto* description = window->findChild<QQuickItem*>("autoPowerOffDescription");
                QVERIFY(description);
                QCOMPARE(description->property("text").toString(),
                         controller->t("auto_power_off_removed_desc"));
                QCOMPARE(menu->actions()[2]->text(), controller->t("open_window"));
            }
        }
    }
};
}

std::shared_ptr<core::IDeviceService> createUiTestService() {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    if (qEnvironmentVariable("SONY_REAL_HEADPHONES") == "1") {
        Logger::setLogLevel(LogLevel::Debug);
        Logger::setDeveloperMode(true);
        Logger::setLogSink([](auto, auto, std::string_view message) {
            if (message.find("payload=67 ") != std::string_view::npos ||
                message.find("payload=69 ") != std::string_view::npos)
                fprintf(stderr, "HARDWARE_MODE_RX %.*s\n", static_cast<int>(message.size()), message.data());
        });
        auto discovery = transport::createPlatformDiscovery();
        const auto devices = discovery->discover();
        const auto found = std::find_if(devices.begin(), devices.end(), [](const auto& device) {
            return device.name == "WH-1000XM6" && device.connected == true;
        });
        if (found == devices.end()) throw std::runtime_error("Real XM6 must already be connected to Windows");
        hardwareAddress = QString::fromStdString(found->address.str());
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "BEFUN-1", "SonyDeviceCenter");
        settings.setValue("connection/selectedAddress", hardwareAddress);
        settings.setValue("connection/pausedAddresses", QStringList{hardwareAddress});
        settings.sync();
        hardwareTransport = std::make_shared<ObservedHardwareTransport>();
        hardwareService = std::make_shared<core::DeviceService>(
            hardwareTransport, std::move(discovery), [] { return core::DeviceService::Clock::now(); },
            core::InitialRefresh::All, true, core::DeviceService::RecoveryPolicy::RequireSystemConnection);
        return hardwareService;
    }
    testTransport = std::make_shared<CallHandoffTransport>();
    testTransport->ambientLevel = 10;
    testTransport->autoPowerOffModes = {0x10, 0x11};
    testDiscovery = std::make_shared<transport::FakeDeviceDiscovery>();
    testDiscovery->addDevice(
        {"WH-1000XM6", transport::DeviceAddress("11:22:33:44:55:66"), true, true});
    testNowMillis = 0;
    auto service = std::make_shared<core::DeviceService>(testTransport, testDiscovery, [] {
        return core::DeviceService::Clock::time_point(std::chrono::milliseconds(testNowMillis.load()));
    }, core::InitialRefresh::All, true, core::DeviceService::RecoveryPolicy::RequireSystemConnection);
    service->connect(transport::DeviceAddress("11:22:33:44:55:66"), "WH-1000XM6");
    return service;
}

void scheduleUiScenario(DeviceCenterController& controller, QQuickWindow* window,
                        QMenu& menu, QSystemTrayIcon& tray) {
    if (qEnvironmentVariable("SONY_REAL_HEADPHONES") == "1") {
        QTimer::singleShot(0, qApp, [&, window] {
            HardwareScenario scenario;
            scenario.controller = &controller; scenario.window = window;
            scenario.menu = &menu; scenario.tray = &tray;
            auto arguments = QStringList{"sony-hardware-ui", "-o", "hardware-ui-results.txt,txt"};
            if (!qEnvironmentVariableIsEmpty("SONY_UI_SCENARIO"))
                arguments << qEnvironmentVariable("SONY_UI_SCENARIO");
            const int result = QTest::qExec(&scenario, arguments);
            QFile results("hardware-ui-results.txt");
            if (results.open(QIODevice::ReadOnly)) fprintf(stderr, "%s", results.readAll().constData());
            QCoreApplication::exit(result);
        });
        return;
    }
    if (!qEnvironmentVariableIsEmpty("SONY_SOAK_SECONDS")) {
        const auto timeout = (qint64(qEnvironmentVariableIntValue("SONY_SOAK_SECONDS")) + 300) * 1000;
        qputenv("QTEST_FUNCTION_TIMEOUT", QByteArray::number(timeout));
    }
    QTimer::singleShot(0, qApp, [&, window] {
        UiScenario scenario;
        scenario.controller = &controller;
        scenario.window = window;
        scenario.menu = &menu;
        scenario.tray = &tray;
        auto arguments = QStringList{"sony-ui-lifecycle-tests", "-o", "ui-lifecycle-results.txt,txt"};
        if (!qEnvironmentVariableIsEmpty("SONY_UI_SCENARIO"))
            arguments << qEnvironmentVariable("SONY_UI_SCENARIO");
        const int result = QTest::qExec(&scenario, arguments);
        QFile results("ui-lifecycle-results.txt");
        if (results.open(QIODevice::ReadOnly))
            fprintf(stderr, "%s", results.readAll().constData());
        QCoreApplication::exit(result);
    });
}

#include "UiScenario.moc"
