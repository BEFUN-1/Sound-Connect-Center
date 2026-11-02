# Qt 2.0.1 validation

Date: 2026-11-01 (Asia/Shanghai).

## Reproductions and causes

1. Windows tray / language switching: the controller-only tests passed. The
   new `sony-ui-lifecycle-tests` executable runs the production `main.cpp`,
   actual tray menus, and actual QML, with an injected simulated XM6 service.
   Running only `languagePopupUpdatesLive` with the Windows platform reproduced
   exit `0xC0000005`. GDB showed repeated recursion in `qwindows.dll`. Changing
   only the Windows tray menu attachment made the same reproduction pass.
   Windows now opens the QWidget menu from the tray's Context activation;
   other platforms retain their platform menu integration. Existing delayed
   Bluetooth command dispatch remains in place. Language changes update the
   displayed QML title, language selector, and tray labels during the session.
2. Battery text now uses larger glyphs with the existing brand color `#4B2E83`.
   Font metrics constrain the width, including 100%, to avoid clipping.
   The UI test exports 64-pixel 85% and 100% images for visual inspection.
3. Auto power-off previously compared both the current mode and independently
   remembered timer. A response `27 05 10 03` incorrectly became "disabled".
   The regression test failed with `0 == 5` before the fix. Unknown modes also
   silently became disabled. The decoder now validates and separates these
   fields and matches the requested subtype.
4. Auto power-off setters previously published the requested index after the
   transport ACK, even if the headset ignored it. A fake headset that ACKs
   without applying the setting reproduced that false success. The service
   now reads capabilities (`20/21`), rejects unadvertised choices, writes
   (`28`), then confirms the setting with a fresh parameter read (`26/27`).
   The remembered duration survives switching to removal/off modes.
5. `sonyctl --help` reproduced the reported loader error `0xC0000139`. Its
   build directory contained the EXE without compiler runtime DLLs, allowing
   PATH to select an older C++ standard library. Both CLI/daemon build outputs
   and the installed payload now receive runtime DLLs from the selected
   compiler directory.

## Software verification

The following command completed with 174 tests passed, zero failures, and
one Windows-excluded Unix-socket test skipped (175 registered tests):

```powershell
ctest --test-dir build-win-tests2 --output-on-failure --no-tests=error -j 4
```

The actual UI scenario exercises nine tray noise-mode actions, power-off,
capability-filtered auto power-off selection, and 35 live language selections
across seven locales. It asserts controller results, actual displayed QML
text, selector text, and tray translations. Test preferences use temporary
INI settings rather than the user's preferences.

The focused auto-power regression tests cover unknown modes, an independent
remembered duration, ignored writes, unsupported times, all advertised timer
codes, and separation from playback commands. Older ACK-only test fixtures
were updated to return real parameter/capability replies; the IPC fixture
now alternates response sequence numbers.

`tests/smoke/windows_runtime.py` passed for Debug and Release `sonyctl` and
`sonyd` with both inherited PATH and a PATH containing only Windows System32.
PowerShell parser validation and `git diff --check` passed. No temporary
`[DEBUG-...]` instrumentation remains.

## Physical WH-1000XM6 verification

Hardware: WH-1000XM6, firmware **3.0.0**, Windows 11, RFCOMM/V2. The adapter
model could not be enumerated in this managed session. Bluetooth addresses
and peer names are omitted from this record.

The explicit read-only probe is not part of unattended CTest or the installer:

```powershell
cmake --build build-win-tests2 --target sony-xm6-readonly-probe
.\build-win-tests2\tests\sony-xm6-readonly-probe.exe
```

It successfully read:

- Noise subtype `0x19`, ambient level 10, adaptation off, sensitivity 0.
- Current auto power-off index 5 ("when removed").
- Advertised auto power-off indexes **[0, 5]**, meaning disabled / when removed.
  This firmware does not advertise selectable 5/15/30/60/180-minute timers.
- Firmware version 3.0.0.
- Second-table function list: independent power sound setting `0x47` absent.

Early connection attempts dropped during reads; the separated read-only probe
then completed all queries, followed by the successful write/readback run.

The following command exercised actual device controls and restored the
original noise and auto-power modes:

```powershell
python tests/smoke/xm6_controls.py build-win-tests2/apps/sonyctl/sonyctl.exe --exercise
```

Results: ANC -> cancelling, ambient 10 -> ambient, ANC off -> off; each was
confirmed by a fresh noise query. Auto power-off indexes 0 and 5 were each
confirmed by device parameter readback. Original modes were restored.
No playback-control or removing-to-pause settings were changed.

This verifies the headset configuration and protocol path. Actual elapsed
shutdown after physically removing the headset, and subjective sound, require
wear/removal and listening observations.

## Power-off sound investigation

The supplied Sound Connect 13.3.0 APK has SHA-256
`e6c5ac8085d5cb785a9391b53ef96944c3aac9dcd20512794a4a4847e819e1a2`.
REA package/class inspection and the local decoded Java export were used
as static evidence:

- `g40.a` -> `yn0.b1.b.h(USER_POWER_OFF)` uses the table-1 request
  **24 03 01**, the same request used here. There is no sound argument.
- `PowerInquiredType.AUTO_POWER_OFF_WEARING_DETECTION` is `0x05`;
  `AutoPowerOffWearingDetectionElements` distinguishes timers, removal
  (`0x10`), and disabled (`0x11`). `yn0.w0` and `r00.d` keep the current
  element separate from the selected duration.
- Removing-to-pause is the separate
  `PLAYBACK_CONTROL_BY_WEARING_REMOVING_HEADPHONE_ON_OFF` system function.
- Some devices have second-table `VOICE_GUIDANCE_POWER_ON_OFF_SOUND`
  function `0x47`, queried as voice-guidance subtype `0x31`.
  This physical XM6 did not advertise it.

REA metadata evidence IDs:
`ev_bdb62170b09076d412db5cea022c27c8ac0205ff87658c78c11e1cea2e4fb2c0`,
`ev_b4fcc62cb44ab2c9b8a5739347b23224c280a07d35f08065487a260f83b76287`.
Metadata inspection confirms declarations; command-body details come from
the retained local decoded export. Static APK inspection does not establish
headset firmware audio behavior.

The application retains the official user power-off request. The reported
missing sound has no confirmed controllable protocol parameter on this
headset. Audible remote-power-off behavior remains a hardware observation;
this release does not claim to force the original headset sound.

## Release artifacts

Release binaries are rebuilt in `build-win-release`. The installer reads
the version from CMakeCache instead of hard-coding 2.0.0. Installer and
portable artifacts are versioned 2.0.1; compiler runtime DLLs are required
payload files. SHA-256 checksums accompany the final artifacts.

The NSIS installer compiled successfully. The portable ZIP passed full CRC
verification. Required executable, runtime, Qt plugin, and QML module hashes
matched the installer input payload; all three EXEs matched the final Release
build. CLI/daemon smoke tests passed from the installer payload and from the
extracted ZIP, each with inherited and clean PATH. Installer execution and
upgrade of the user's existing installation were not performed.

```powershell
python tests/smoke/verify_windows_package.py sony-device-center-2.0.1-win64.zip build-win-release/qt-installer-payload build-win-release
```

- Installer: `SonyDeviceCenter-Qt2-2.0.1-Setup.exe`, 59,076,328 bytes.
  SHA-256: `bfd41479cc655db0e38b8252a2a204696962028ca5b76a9a5255dffa93134564`.
- Portable: `sony-device-center-2.0.1-win64.zip`, 76,913,190 bytes.
  SHA-256: `a657c92479855540a8c7495ba716a6bf4c48f4f2ddc07751d4963f4b2aac02d9`.
- The final font-width adjustment was followed by another successful native
  `device-ui-lifecycle` test run.
