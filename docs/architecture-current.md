# Current Sound Connect Center architecture

Qt 2.0 is the only desktop UI and release target. The application is split into
transport, protocol, service, and presentation layers:

```text
Qt Quick/QML
    -> DeviceCenterController
    -> DeviceBackend
    -> JsonProtocol (direct service or sonyd IPC)
    -> sony-core
    -> sony-protocol
    -> sony-transport
    -> platform Bluetooth connector
```

## Runtime layers

- `apps/device-center` owns the Qt Quick presentation, tray lifecycle, single
  instance behavior, and user-facing state transitions. Its coordinator accepts
  bounded commands while a persistent Qt I/O thread serializes device access;
  reader notifications publish immutable snapshots independently.
- `apps/sonyctl` is the command-line client. It can use structured IPC through
  `sonyd` or a direct service session where supported.
- `apps/sonyd` owns discovery, reconnect policy, serialized device operations,
  and the local IPC endpoint.
- `libs/sony-core` owns device state, service orchestration, JSON protocol
  handling, and IPC lifecycle.
- `libs/sony-protocol` owns frame encoding, protocol generations, device
  profiles, capability discovery, and event dispatch.
- `libs/sony-transport` owns the typed transport interface, discovery filters,
  fake transport, and adapters for the platform connectors.

## Platform connectors

The platform Bluetooth implementations remain under `Client/` because the
transport library still adapts their existing `IBluetoothConnector` contract:

| Platform | Connector | Additional support |
| --- | --- | --- |
| Linux | `Client/linux/LinuxBluetoothConnector.cpp` | `DBusHelper.cpp`, `FindDBus.cmake` |
| Windows | `Client/windows/WindowsBluetoothConnector.cpp` | `Client/ByteMagic.cpp` |
| macOS | `Client/macos/MacOSBluetoothConnector.mm` | IOBluetooth framework |

The shared connector boundary is defined by `Client/IBluetoothConnector.h` and
`Client/Exceptions.h`. `Client/Constants.h` contains the UUID and wire constants
needed by the connectors and compatibility tests. The Qt application also
embeds the model artwork from `Client/resources/devices/`.

## Compatibility boundary

The current protocol and transport libraries are the preferred implementation.
`Client/BluetoothWrapper.*`, `Client/CommandSerializer.*`, and
`Client/Headphones.*` remain as compatibility code for focused regression tests
and adapter coverage. They are not desktop UI targets. A future cleanup can
remove them after those tests are migrated fully to the modern libraries.

## Packaging and validation

Windows packaging uses CPack for the portable ZIP and
`packaging/windows/build-qt-installer.ps1` with `qt-installer.nsi` for the
per-user NSIS installer. macOS packaging builds a deployed DMG through the
`dmg` target. Linux packaging uses CPack, AppImage, and Flatpak metadata.
The Windows application fixes its QML style to Basic; deployment removes the six
unused optional style families while retaining platform plugins, graphics
fallbacks, and compiler runtimes. `qt.conf` resolves imports from the payload.

The Qt controller tests, protocol tests, transport tests, core tests, and the
simulated application smoke test run without physical hardware. Hardware
compatibility remains tracked in `docs/device-matrix.md` and the records under
`docs/reverse-engineering/`.
