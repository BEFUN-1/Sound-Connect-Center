# Qt 2.0 acceptance record

Date: 2026-10-31

Historical local-build record. The elevated installer and recursive cleanup
described below were removed in 2.0.4; see `security-fixes-2026-11-01.md`.
These old binaries are not approved release candidates.

## Accepted in source review

- Qt Quick/QML is the only active desktop target in top-level CMake.
- Windows Forms 1.3 remains documented as a frozen rollback/reference build and
  is outside the active package pipeline.
- Discovery is exposed separately from paired-device switching.
- Multipoint state and actions use the structured JSON protocol:
  list, connect, disconnect, unpair, source switch, default source, and source
  keeping.
- Windows Qt startup uses direct Bluetooth service selection and does not attach
  to `sonyd` IPC.
- Tray lifecycle includes hide-on-close, restore, noise mode actions, battery
  tooltip, power off, quit, `--tray`, and `--exit`.
- The single-instance guard uses per-user `QSharedMemory` plus a
  `QLocalServer`; `--exit` forwards to that endpoint without starting the GUI
  platform.
- CPack produces the portable ZIP. The checked-in NSIS script produces the
  per-user Qt installer and migrates the existing startup value.

## Verification run

- Qt 6.12.0 MinGW 13.1.0 configuration with
  `SONY_REQUIRE_QT=ON` and `BUILD_TESTING=ON` passed.
- The Qt release build passed with Ninja and produced
  `build/apps/device-center/sony-device-center.exe`.
- `windeployqt` populated Qt DLLs, QML modules, Windows plugins, and MinGW
  runtime DLLs in the install tree.
- NSIS 3.13 compiled `SonyDeviceCenter-Qt2-Setup.exe` as a per-user installer.
- The Qt 2.0 installer was regenerated with the local NSIS compiler.
  The current installer is 59,045,509 bytes with SHA-256
  `3B87611BB12FB7221482496B7BBF1551B00526942CBFDBC71B4E36B164B3684A`.
- The installer requests the highest available user token for upgrades to
  existing custom paths, asks the running Qt/WinForms process to exit, waits
  for its worker-thread shutdown, and force-terminates a stale process before
  replacing locked runtime DLLs.
- The staged executable contains the `IDI_ICON1` Windows resource, and the
  installer creates both Start Menu and Desktop shortcuts targeting
  `bin/sony-device-center.exe` with that executable as the icon source.
- During an upgrade, the installer stops the old executable and removes the
  frozen WinForms 1.3 executable, PDB, .NET assemblies, dependency manifests,
  and `runtime` directory before writing the Qt payload.
- The staged payload contains Qt DLLs, QML modules, the Windows platform
  plugin, and MinGW runtime DLLs; no WinForms `.dll`, `.runtimeconfig.json`,
  `.deps.json`, or `runtime` directory is staged.
- The `--exit` path exits without loading the GUI platform; a normal Windows
  start remains alive in the tray-capable runtime tree.
- Catch2 was fetched during configuration and the full suite ran with 164
  tests: 163 passed and 1 skipped (`IpcServer and IpcClient end-to-end
  communication over socket`, which is disabled by the Windows test
  condition).
- `cmake --build build --parallel` passed for the Qt application, service,
  CLI, and all test targets.
- `git diff --check` passed.
- PowerShell parser validation passed for
  `packaging/windows/build-qt-installer.ps1`.
- QML/C++ brace balance checks passed.

## Environment limits

The managed session can compile and launch the Qt runtime, but it does not
provide a reliable interactive desktop observation channel for clicking the
tray menu or observing the taskbar icon. The tray action lifetime fix,
embedded icon resource, and shortcut targets were verified by source,
resource, and package inspection. Hardware acceptance still requires the XM6
and legacy-model matrix in `docs/device-matrix.md`.
