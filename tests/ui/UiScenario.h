#pragma once
#include "sony/core/IDeviceService.h"
#include <memory>

class QMenu;
class QQuickWindow;
class QSystemTrayIcon;
namespace sony::devicecenter { class DeviceCenterController; }

std::shared_ptr<sony::core::IDeviceService> createUiTestService();
void scheduleUiScenario(sony::devicecenter::DeviceCenterController& controller,
                        QQuickWindow* window, QMenu& menu, QSystemTrayIcon& tray);
