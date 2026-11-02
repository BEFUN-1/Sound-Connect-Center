# Qt 2.0 migration boundary

Qt 2.0 is the only desktop UI and release target. The Windows Forms 1.3
source and release packages were removed from the active tree after the
migration. The 1.3 validation records are preserved under `docs/history/v1.3/`.
Historical binaries are kept outside the source repository.

The migration keeps one service boundary:

```text
Qt Quick/QML
    -> DeviceCenterController
    -> DeviceBackend
    -> JsonProtocol (direct service or sonyd IPC)
    -> sony-core / sony-protocol / sony-transport
```

The Qt controller now owns:

- device discovery and connection selection;
- headset multipoint history and connected peers;
- playback source switching and source keeping;
- multipoint connect, disconnect, and unpair actions;
- tray lifecycle, startup-to-tray, and per-user single-instance behavior;
- Qt 2.0 NSIS/ZIP packaging (`CPack` produces ZIP; the checked-in NSIS script
  is driven by `packaging/windows/build-qt-installer.ps1`).

The structured JSON `multipoint` method is the protocol seam used by both
direct Windows execution and `sonyd`. This keeps the UI independent from the
legacy CLI text parser and avoids duplicating protocol logic in QML.

The Windows installer uses a per-user location under
`%LOCALAPPDATA%\Programs\Sony Device Center`. During an upgrade it preserves an
existing `SonyDeviceCenter` startup preference, changes it to the Qt
executable with `--tray`. Since 2.0.4, the installer is restricted to this fixed
directory and a non-elevated current-user token. It does not execute installed
programs, kill processes, or recursively remove legacy runtime trees. Close the
application before upgrading. Older custom paths are not migrated automatically.

## Acceptance matrix

| Area | Qt 2.0 behavior |
| --- | --- |
| UI ownership | Qt Quick/QML only |
| WinForms | Archived 1.3 release and validation records; outside release build |
| Discovery | Refreshable discovered-device list with connect and disconnect |
| Multipoint | List, connect, disconnect, confirmed unpair, switch source, keep source |
| Tray | Close hides to tray; tray restores, changes noise mode, powers off, or quits |
| Single instance | Second launch restores the existing per-user instance |
| Startup | `--tray` is written to the existing `SonyDeviceCenter` startup value |
| Packaging | Checked-in per-user NSIS installer plus CPack ZIP portable package |

Real-device acceptance still requires the hardware matrix in
`docs/device-matrix.md`; simulated tests validate command routing and state
transitions only.
