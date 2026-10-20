# Sony Device Center

[English version](README.en.md)

这是一个面向索尼耳机的开源实验项目，用于研究并尝试通过蓝牙连接读取和修改部分设备状态。

项目包含：

- 基于 C++ 和 Qt 的桌面程序
- `sonyctl` 命令行工具
- `sonyd` 后台服务
- 蓝牙传输、协议解析和设备状态相关的 C++ 代码
- Windows 桌面启动器和安装脚本

项目仍在开发中。不同型号、固件和操作系统的表现可能不同，代码中已经实现的命令不代表所有设备都支持。使用前请查看测试结果和设备说明，不要把本项目当作索尼官方软件的替代品。

## 当前状态

目前主要工作集中在以下内容：

- 索尼耳机蓝牙通信的帧解析和校验
- 第一代和第二代协议中部分命令的读取与写入
- 电量、降噪、均衡器等状态的统一表示
- 多点连接相关命令的协议实现
- 桌面程序、命令行工具和后台服务之间的基本协作

协议测试和模拟测试可以在没有耳机的情况下运行，但这只能验证代码逻辑，不能证明某个型号在真实环境中可用。真实设备支持情况请以 `docs/device-matrix.md` 和实际测试记录为准。

## 设备支持

项目没有承诺完整的型号支持。设备是否可用取决于型号、固件、蓝牙适配器和操作系统。

当前仓库中有针对部分 WH 系列设备的测试和记录；其他设备可能只能完成发现、连接或部分状态读取。遇到未验证的设备时，请先使用只读命令，并记录型号、固件版本、操作系统和失败步骤。

## 编译环境

常用依赖如下：

- C++20 编译器
- CMake
- Qt 6（桌面程序需要）
- Catch2 3（运行测试时使用）
- 对应系统的蓝牙开发库

项目包含 ImGui 子模块。首次获取代码后执行：

```bash
git submodule update --init --recursive
```

### Linux

在 Ubuntu 或 Debian 上可以先安装基础依赖：

```bash
sudo apt update
sudo apt install -y build-essential cmake git \
  libbluetooth-dev libdbus-1-dev qt6-base-dev \
  qt6-declarative-dev qml6-module-qtquick-controls
```

配置、编译并运行测试：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### macOS

请安装 Xcode 命令行工具、CMake 和 Qt 6，然后执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Windows

使用 Visual Studio 2022 开发者命令提示符或 PowerShell，并准备 Qt 6 和 CMake：

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Windows 桌面封装的脚本位于 `desktop-app/`，安装器脚本位于 `packaging/windows/`。生成的运行时文件和安装包不属于源代码，默认不会提交到 Git。

## 运行方式

构建完成后，具体目标名称取决于平台和 CMake 配置。常见目标如下：

- `sony-device-center`：桌面程序
- `sonyctl`：命令行工具
- `sonyd`：后台服务

可以先查看帮助：

```bash
sonyctl --help
sonyd --help
```

### 模拟模式

`sonyd --simulated` 可用于运行无硬件的模拟测试。模拟模式只用于检查界面、命令流程和状态处理，不代表真实耳机兼容。

### 直接连接

命令行工具支持直接连接模式。使用前应确认没有其他程序或后台服务占用同一副耳机：

```bash
sonyctl --direct devices
sonyctl --direct info
```

涉及写入设备的命令请谨慎使用。对于未验证的型号，建议先执行查询命令，不要直接发送控制命令。

## 测试

测试主要覆盖以下内容：

- 帧编码、转义、校验和错误处理
- 协议第一代和第二代的部分命令布局
- 模拟传输、超时和断开重连
- 设备状态和命令行协议
- 部分多点连接命令

运行全部已配置测试：

```bash
ctest --test-dir build --output-on-failure
```

测试通过只说明对应测试场景通过，不等同于真实设备验证。Windows 的部分进程间通信测试可能因平台条件跳过。

## 资料

- `docs/device-matrix.md`：设备和功能验证情况
- `docs/architecture-current.md`：当前代码结构说明
- `docs/ipc-and-lifecycle.md`：后台服务和客户端通信说明
- `docs/reverse-engineering/`：协议分析记录
- `docs/windows-xm6-validation-2026-10-19.md`：Windows 实机验证记录

欢迎提交问题和测试记录。报告设备问题时，请尽量附上型号、固件版本、操作系统、蓝牙适配器、使用的命令以及完整错误信息。

## 声明

本项目与索尼公司没有隶属、授权或合作关系。Sony 及相关产品名称归其各自所有者所有。本项目用于学习、研究和互操作性测试，使用者应自行承担使用风险。

## 许可证

本项目使用 MIT 许可证，详见 [LICENSE](LICENSE)。
