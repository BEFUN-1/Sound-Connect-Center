# Sony Device Center

This is an open-source experimental project for Sony headphones. It is intended to study and try to read or modify selected device states over Bluetooth.

The project includes:

- A desktop application based on C++ and Qt
- The `sonyctl` command-line tool
- The `sonyd` background service
- C++ code for Bluetooth transport, protocol parsing, and device state handling
- Windows desktop launcher and installer scripts

The project is still under development. Results may vary by model, firmware, and operating system. The commands implemented in the code do not mean that every device supports them. Check the test results and device notes before use. This project is not a replacement for Sony's official software.

## Current status

The current release is **1.3**. The Windows build includes the control-channel recovery fix for switching back to the computer after a phone call on a multipoint headset. See [`docs/release-v1.3.md`](docs/release-v1.3.md) for the release validation record.

Current work mainly covers:

- Frame parsing and validation for Sony headphone Bluetooth communication
- Reading and writing selected commands from the first and second protocol generations
- A common representation for battery, noise control, equalizer, and other device states
- Protocol implementation for selected multipoint features
- Basic cooperation between the desktop application, command-line tool, and background service

Protocol and simulation tests can run without a headphone, but they only verify code behavior. They do not prove that a model works in a real environment. Check `docs/device-matrix.md` and the hardware test records for actual device status.

## Device support

The project does not promise complete model support. Whether a device works depends on its model, firmware, Bluetooth adapter, and operating system.

The repository contains tests and notes for selected WH-series devices. Other devices may only support discovery, connection, or some state queries. For an unverified model, start with read-only commands and record the model, firmware version, operating system, and failed step.

## Build requirements

Common requirements include:

- A C++20 compiler
- CMake
- Qt 6 for the desktop application
- Catch2 3 for tests
- Bluetooth development libraries for the target operating system

The project includes an ImGui submodule. After cloning the repository, run:

```bash
git submodule update --init --recursive
```

### Linux

On Ubuntu or Debian, install the basic dependencies:

```bash
sudo apt update
sudo apt install -y build-essential cmake git \
  libbluetooth-dev libdbus-1-dev qt6-base-dev \
  qt6-declarative-dev qml6-module-qtquick-controls
```

Configure, build, and run the tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### macOS

Install the Xcode Command Line Tools, CMake, and Qt 6, then run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Windows

Use a Visual Studio 2022 Developer Command Prompt or PowerShell with Qt 6 and CMake installed:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Windows desktop packaging scripts are in `desktop-app/`, and installer scripts are in `packaging/windows/`. Generated runtime files and installers are not source files and are excluded from Git by default.

## Running

After building, the exact target names depend on the platform and CMake configuration. Common targets include:

- `sony-device-center`: desktop application
- `sonyctl`: command-line tool
- `sonyd`: background service

Start by viewing the help output:

```bash
sonyctl --help
sonyd --help
```

### Simulation mode

`sonyd --simulated` runs a no-hardware simulation for testing. Simulation mode only checks the interface, command flow, and state handling. It does not prove compatibility with a real headphone.

### Direct connection

The command-line tool supports direct connections. Before using this mode, make sure that no other program or background service is using the same headphone:

```bash
sonyctl --direct devices
sonyctl --direct info
```

Use commands that write to a device carefully. For an unverified model, query its state first instead of sending control commands immediately.

## Tests

The tests mainly cover:

- Frame encoding, escaping, checksums, and error handling
- Selected command layouts from protocol generations one and two
- Simulated transport, timeouts, and disconnect/reconnect behavior
- Device state and command-line protocol handling
- Selected multipoint commands

Run all configured tests with:

```bash
ctest --test-dir build --output-on-failure
```

A passing test only describes the tested scenario. It is not proof of real-device compatibility. Some Windows inter-process communication tests may be skipped because of platform conditions.

## Documentation

- `docs/device-matrix.md`: device and feature validation status
- `docs/architecture-current.md`: current code structure
- `docs/ipc-and-lifecycle.md`: background service and client communication
- `docs/reverse-engineering/`: protocol analysis notes
- `docs/windows-xm6-validation-2026-10-19.md`: Windows hardware validation record
- `docs/release-v1.3.md`: 1.3 release notes and phone-call handoff validation

Issues and test reports are welcome. When reporting a device problem, include the model, firmware version, operating system, Bluetooth adapter, command used, and the complete error message when possible.

## Disclaimer

This project is not affiliated with, authorized by, or partnered with Sony Corporation. Sony and related product names belong to their respective owners. This project is intended for learning, research, and interoperability testing. Use it at your own risk.

## License

This project is released under the MIT License. See [LICENSE](LICENSE).
