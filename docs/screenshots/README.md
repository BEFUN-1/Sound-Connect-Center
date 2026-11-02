# 界面截图与隐私

## 当前截图

`v2.0.6/` 中的六张 PNG 是 2026-11-02 从当前 Qt 2.0.6 QML 界面直接抓取的窗口内容：

| 文件 | 页面 |
| --- | --- |
| `overview.png` | 中文概览 |
| `overview-en.png` | English overview |
| `noise-control.png` | 降噪控制 |
| `equalizer.png` | WH-1000XM6 十段均衡器 |
| `audio-features.png` | 音频与设备功能 |
| `settings.png` | 设置与版本 |

截图使用测试专用 `CallHandoffTransport` 与 `FakeDeviceDiscovery`，型号为
模拟 WH-1000XM6，电量为模拟值，蓝牙地址为固定测试占位符。
QSettings 使用临时目录；未启用 `SONY_REAL_HEADPHONES`。
只抓取 QML 窗口内容，不包含桌面、任务栏、通知、真实扫描结果、账户或用户路径。
没有通过图像生成工具重绘界面，也未把模拟值表述为真机结果。

未知编解码器或未确认功能状态按当前模拟数据原样显示。设置页启动项开关可以
反映系统是否已注册启动项，截图流程不修改系统启动项。

发布前已逐张检查 PNG 的可见内容；PNG 不包含文本或 EXIF 元数据。
当前文件为 1280 × 840 逻辑窗口在 125% 缩放下的 1600 × 1050 图像。

## 重新生成

构建测试目标（使用与当前 Qt 匹配的编译器）：

```powershell
cmake --build build --config Release --target sony-ui-lifecycle-tests --parallel
$env:SONY_UI_SCENARIO = 'captureDocumentationScreenshots'
$env:SONY_SCREENSHOT_DIR = Join-Path (Get-Location) 'docs/screenshots/v2.0.6'
$env:SONY_UI_INSTANCE = '-documentation'
$env:QT_QUICK_BACKEND = 'software'
$env:QT_QPA_PLATFORM = 'windows'
Remove-Item Env:SONY_REAL_HEADPHONES -ErrorAction SilentlyContinue
# 单配置生成器；多配置生成器请使用对应 Release 子目录中的可执行文件。
& .\build\apps\device-center\sony-ui-lifecycle-tests.exe
```

该用例仅在设置 `SONY_SCREENSHOT_DIR` 后执行；普通测试运行会跳过。
`SONY_UI_SCENARIO` 将运行限制为截图用例，不执行真实设备操作。
其他平台可使用 `QT_QPA_PLATFORM=offscreen`，但需要部署对应平台插件。
重新生成后仍须逐张检查隐私和布局。

根目录旧截图记录较早界面，保留供历史引用；项目首页与站点使用 `v2.0.6/`。
