#include <algorithm>
#include <cstdlib>
#include <optional>
#include <QAction>
#include <QByteArray>
#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QCursor>
#include <QDir>
#include <QEventLoop>
#include <QFontMetricsF>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSharedPointer>
#include <QSharedMemory>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>

#include "DeviceCenterController.h"

#ifdef SONY_UI_INTEGRATION_TEST
#include "UiScenario.h"
#endif

namespace {

class TrayWindowEventFilter final : public QObject {
public:
    explicit TrayWindowEventFilter(bool *exiting, QObject *parent = nullptr)
        : QObject(parent), exiting_(exiting) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Close && !*exiting_) {
            if (auto *window = qobject_cast<QQuickWindow *>(watched))
                window->hide();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    bool *exiting_;
};

QString instanceId() {
    const auto userHome = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return QString::fromLatin1(QCryptographicHash::hash(
        userHome.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
}

QString instanceServerName() {
#ifdef SONY_UI_INTEGRATION_TEST
    return QStringLiteral("SonyDeviceCenter-ui-test-") + instanceId() +
        qEnvironmentVariable("SONY_UI_INSTANCE");
#else
    return QStringLiteral("SonyDeviceCenter-") + instanceId();
#endif
}

bool forwardToRunningInstance(const QByteArray& command) {
    QLocalSocket socket;
    QEventLoop loop;
    QTimer timeout;
    bool delivered = false;
    timeout.setSingleShot(true);
    timeout.setInterval(750);
    QObject::connect(&socket, &QLocalSocket::connected, &loop, [&] {
        socket.write(command);
        socket.write("\n");
        socket.waitForBytesWritten(500);
        socket.disconnectFromServer();
        delivered = true;
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
    });
    QObject::connect(&socket, &QLocalSocket::errorOccurred, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    socket.connectToServer(instanceServerName());
    timeout.start();
    loop.exec();
    socket.abort();
    return delivered;
}

QIcon trayIcon(int battery, bool connected, bool charging) {
    constexpr int size = 64;
    const QColor trayPurple("#4B2E83");
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    // Windows tray icons are tiny. Keep the battery value large and use the
    // same deep-purple mark for connected, disconnected, and charging states.
    painter.setPen(trayPurple);
    QFont font = painter.font();
    font.setPixelSize(battery >= 100 ? 52 : 62);
    font.setStretch(battery >= 100 ? 74 : 86);
    font.setBold(true);
    const QString label = connected && battery >= 0
        ? QString::number(qBound(0, battery, 100))
        : QStringLiteral("—");
    const auto textWidth = QFontMetricsF(font).horizontalAdvance(label);
    if (textWidth > size - 4)
        font.setStretch(static_cast<int>(font.stretch() * (size - 4) / textWidth));
    painter.setFont(font);
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, label);
    if (charging) {
        painter.setPen(QPen(trayPurple, 2.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(51, 9), QPointF(45, 20));
        painter.drawLine(QPointF(45, 20), QPointF(53, 20));
    }
    return QIcon(pixmap);
}

} // namespace

int main(int argc, char *argv[]) {
    const bool requestExit = std::any_of(argv + 1, argv + argc, [](const char *argument) {
        return QString::fromLocal8Bit(argument) == QStringLiteral("--exit");
    });
    if (requestExit) {
        QCoreApplication coreApp(argc, argv);
        coreApp.setApplicationName("Sony Device Center");
        coreApp.setOrganizationName("BEFUN-1");
        forwardToRunningInstance(QByteArray("exit"));
        // This helper intentionally does not enter the GUI event loop. Avoid
        // Qt's Windows socket teardown path, which can wait on an orphaned
        // named-pipe server after the command has already been delivered.
        std::_Exit(0);
    }

    QApplication app(argc, argv);
    app.setApplicationName("Sony Device Center");
    app.setOrganizationName("BEFUN-1");
    app.setApplicationVersion(SONY_DEVICE_CENTER_VERSION);
    QApplication::setQuitOnLastWindowClosed(false);

    const bool startInTray = app.arguments().contains(QStringLiteral("--tray"));
    QSharedMemory instanceGuard(instanceServerName() + QStringLiteral("-guard"));
    if (!instanceGuard.create(1)) {
        if (forwardToRunningInstance(QByteArray("restore")))
            return 0;
        return 0;
    }
    QLocalServer instanceServer;
    if (!instanceServer.listen(instanceServerName())) {
        if (forwardToRunningInstance(QByteArray("restore")))
            return 0;
        QLocalServer::removeServer(instanceServerName());
        if (!instanceServer.listen(instanceServerName()))
            return 0;
    }

    QQuickStyle::setStyle("Basic");
    QGuiApplication::setDesktopFileName("sony-device-center");
    const QIcon applicationIcon(":/resources/brand/app-icon.png");
    app.setWindowIcon(applicationIcon);

#ifdef SONY_UI_INTEGRATION_TEST
    sony::devicecenter::DeviceCenterController controller(nullptr, createUiTestService());
#else
    sony::devicecenter::DeviceCenterController controller;
#endif

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("controller", &controller);
    engine.rootContext()->setContextProperty("startInTray", startInTray);
    const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    engine.load(url);

    auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
    if (!window)
        return -1;
    window->setIcon(applicationIcon);

    QMenu trayMenu;
    // QSystemTrayIcon keeps a non-owning pointer to its context menu. Declare
    // the menu first so it outlives the tray icon during stack unwinding.
    QSystemTrayIcon tray(trayIcon(-1, false, false), &app);
    auto *titleAction = trayMenu.addAction("Sony Device Center");
    titleAction->setEnabled(false);
    trayMenu.addSeparator();
    auto *restoreAction = trayMenu.addAction(controller.t("open_window"));
    trayMenu.addSeparator();
    auto *modeMenu = trayMenu.addMenu(controller.t("tray_noise_control"));
    auto *ancAction = modeMenu->addAction(controller.t("tray_noise_cancelling"));
    auto *ambientAction = modeMenu->addAction(controller.t("tray_ambient_sound"));
    auto *offAction = modeMenu->addAction(controller.t("tray_off"));
    trayMenu.addSeparator();
    auto *powerOffAction = trayMenu.addAction(controller.t("tray_power_off"));
    trayMenu.addSeparator();
    auto *quitAction = trayMenu.addAction(controller.t("tray_quit"));
#ifndef Q_OS_WIN
    tray.setContextMenu(&trayMenu);
#endif

    const auto updateTrayTranslations = [&] {
        restoreAction->setText(controller.t("open_window"));
        modeMenu->setTitle(controller.t("tray_noise_control"));
        ancAction->setText(controller.t("tray_noise_cancelling"));
        ambientAction->setText(controller.t("tray_ambient_sound"));
        offAction->setText(controller.t("tray_off"));
        powerOffAction->setText(controller.t("tray_power_off"));
        quitAction->setText(controller.t("tray_quit"));
    };
    QObject::connect(&controller, &sony::devicecenter::DeviceCenterController::languageChanged,
                     &app, updateTrayTranslations);

    bool exiting = false;
    const auto restoreWindow = [&] {
        if (exiting)
            return;
        window->show();
        window->raise();
        window->requestActivate();
    };
    QObject::connect(&instanceServer, &QLocalServer::newConnection, &app, [&] {
        while (auto *socket = instanceServer.nextPendingConnection()) {
            const auto commandBuffer = QSharedPointer<QByteArray>::create();
            const auto processCommand = [&, socket, commandBuffer] {
                *commandBuffer += socket->readAll();
                if (!commandBuffer->contains('\n'))
                    return;
                const auto command = QString::fromUtf8(*commandBuffer).trimmed();
                if (command.compare(QStringLiteral("exit"), Qt::CaseInsensitive) == 0) {
                    exiting = true;
                    tray.hide();
                    app.quit();
                } else {
                    restoreWindow();
                }
                socket->deleteLater();
            };
            QObject::connect(socket, &QLocalSocket::readyRead, &app, processCommand);
            if (socket->bytesAvailable() > 0)
                processCommand();
        }
    });
    QObject::connect(restoreAction, &QAction::triggered, &app, restoreWindow);
    QObject::connect(&tray, &QSystemTrayIcon::activated, &app,
                     [&](QSystemTrayIcon::ActivationReason reason) {
#ifdef Q_OS_WIN
        // The Windows platform menu bridge recurses through this submenu tree
        // on activation and translation updates. Use the QWidget menu path.
        if (reason == QSystemTrayIcon::Context) {
            trayMenu.popup(QCursor::pos());
            return;
        }
#endif
        if (reason == QSystemTrayIcon::Trigger ||
            reason == QSystemTrayIcon::DoubleClick)
            restoreWindow();
    });
    enum class TrayCommand {
        SetAnc,
        SetAmbient,
        SetNoiseControlOff,
        PowerOff,
        ShowConnectFirst
    };
    std::optional<TrayCommand> pendingTrayCommand;
    bool pendingTrayRefresh = false;
    bool executingTrayCommand = false;
    QTimer trayCommandTimer;
    trayCommandTimer.setSingleShot(true);
    trayCommandTimer.setInterval(25);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] {
        exiting = true;
        pendingTrayCommand = {};
        pendingTrayRefresh = false;
        trayCommandTimer.stop();
        tray.hide();
    });

    int renderedBattery = -2;
    bool renderedConnected = false, renderedCharging = false;
    QString renderedTooltip;
    const auto refreshTray = [&] {
        if (exiting)
            return;
        const bool connected = controller.isConnected();
        const auto battery = controller.batteryLevel() >= 0
            ? controller.t("tray_battery").arg(controller.batteryLevel())
            : controller.t("tray_battery_unknown");
        if (renderedBattery != controller.batteryLevel() || renderedConnected != connected ||
            renderedCharging != controller.isCharging()) {
            renderedBattery = controller.batteryLevel();
            renderedConnected = connected;
            renderedCharging = controller.isCharging();
            tray.setIcon(trayIcon(renderedBattery, connected, renderedCharging));
        }
        const auto tooltip = connected ? battery + "\n" + controller.statusDescription() : controller.statusDescription();
        if (renderedTooltip != tooltip) {
            renderedTooltip = tooltip;
            tray.setToolTip(tooltip);
        }
    };
    const auto scheduleTrayCommand = [&] {
        // QAction::triggered is delivered from Windows' native TrackPopupMenu
        // loop. Wait until Qt reports that both menus have closed before
        // touching the tray icon or the Bluetooth session.
        if (!trayCommandTimer.isActive())
            trayCommandTimer.start();
    };
    QObject::connect(&trayCommandTimer, &QTimer::timeout, &app, [&] {
        if (exiting)
            return;
        if (trayMenu.isVisible() || modeMenu->isVisible()) {
            // Never close or mutate a native menu from this callback. The
            // action's native loop will close it; retry after that turn.
            trayCommandTimer.start();
            return;
        }

        const auto command = pendingTrayCommand;
        pendingTrayCommand.reset();
        executingTrayCommand = true;
        if (command.has_value()) {
            switch (*command) {
            case TrayCommand::SetAnc:
                controller.setAnc(true);
                break;
            case TrayCommand::SetAmbient:
                controller.setAmbient(qBound(1, controller.ambientLevel(), 20), controller.focusOnVoice());
                break;
            case TrayCommand::SetNoiseControlOff:
                controller.setNoiseControlOff();
                break;
            case TrayCommand::PowerOff:
                controller.powerOff();
                break;
            case TrayCommand::ShowConnectFirst:
                tray.showMessage("Sony Device Center", controller.t("tray_connect_first"));
                break;
            }
        }
        executingTrayCommand = false;

        if (pendingTrayRefresh) {
            pendingTrayRefresh = false;
            refreshTray();
        }
    });
    const auto runAfterTrayMenuCloses = [&](TrayCommand command) {
        // Queue the command; the timer executes it after the native menu loop.
        pendingTrayCommand = command;
        scheduleTrayCommand();
    };
    QObject::connect(&trayMenu, &QMenu::aboutToHide, &app, [&] {
        if (pendingTrayCommand || pendingTrayRefresh)
            scheduleTrayCommand();
    });
    QObject::connect(modeMenu, &QMenu::aboutToHide, &app, [&] {
        if (pendingTrayCommand || pendingTrayRefresh)
            scheduleTrayCommand();
    });
    QObject::connect(ancAction, &QAction::triggered, &app,
                     [&] {
        if (controller.isConnected() && controller.hasAnc())
            runAfterTrayMenuCloses(TrayCommand::SetAnc);
        else
            runAfterTrayMenuCloses(TrayCommand::ShowConnectFirst);
    });
    QObject::connect(ambientAction, &QAction::triggered, &app,
                     [&] {
        if (controller.isConnected() && controller.hasAmbient())
            runAfterTrayMenuCloses(TrayCommand::SetAmbient);
        else
            runAfterTrayMenuCloses(TrayCommand::ShowConnectFirst);
    });
    QObject::connect(offAction, &QAction::triggered, &app,
                     [&] {
        if (controller.isConnected() && controller.hasAnc())
            runAfterTrayMenuCloses(TrayCommand::SetNoiseControlOff);
        else
            runAfterTrayMenuCloses(TrayCommand::ShowConnectFirst);
    });
    QObject::connect(powerOffAction, &QAction::triggered, &app,
                     [&] {
        if (controller.isConnected() && controller.hasPowerOff())
            runAfterTrayMenuCloses(TrayCommand::PowerOff);
        else
            runAfterTrayMenuCloses(TrayCommand::ShowConnectFirst);
    });
    QObject::connect(quitAction, &QAction::triggered, &app, [&] {
        exiting = true;
        tray.hide();
        app.quit();
    });
    QObject::connect(&controller, &sony::devicecenter::DeviceCenterController::stateChanged,
                     &app, [&] {
        // Device commands and asynchronous notifications can both arrive
        // while a native tray menu is dispatching an action. Never mutate the
        // tray icon or tooltip from that re-entrant window.
        if (trayMenu.isVisible() || modeMenu->isVisible() ||
            pendingTrayCommand || executingTrayCommand) {
            pendingTrayRefresh = true;
            scheduleTrayCommand();
            return;
        }
        refreshTray();
    });
    if (qEnvironmentVariableIsEmpty("SONY_SKIP_TRAY") &&
        QSystemTrayIcon::isSystemTrayAvailable()) {
        tray.show();
        window->installEventFilter(new TrayWindowEventFilter(&exiting, window));
        if (startInTray)
            window->hide();
    } else {
        // A desktop without a tray cannot keep a hidden process alive.
        QApplication::setQuitOnLastWindowClosed(true);
        if (startInTray)
            window->show();
    }

#ifdef SONY_UI_INTEGRATION_TEST
    trayIcon(100, true, false).pixmap(64, 64).save("tray-battery-100.png");
    scheduleUiScenario(controller, window, trayMenu, tray);
#endif
    const int result = app.exec();
    tray.hide();
    tray.setContextMenu(nullptr);
    instanceServer.close();
    QLocalServer::removeServer(instanceServerName());
    return result;
}
