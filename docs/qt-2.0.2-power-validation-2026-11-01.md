# Qt 2.0.2 power validation

Date: 2026-11-01 (Asia/Shanghai).

## Scope and verified behavior

The current request covers XM6 removal-triggered automatic shutdown, its UI
description, and the reported missing shutdown sound. Earlier tray, language,
and runtime fixes remain covered by the regression suite.

The physical WH-1000XM6, firmware 3.0.0, was queried again on Windows 11:

```json
{
  "autoPowerOff": 5,
  "autoPowerOffOptions": [0, 5],
  "powerSoundSettingAdvertised": false
}
```

The explicit read-only probe completed successfully. Addresses and peer names
are omitted. This is capability/configuration evidence, not an observation
of elapsed shutdown or audible sound. The earlier 2.0.1 write/readback run
confirmed both advertised options and restored the initial settings.

This firmware advertises off/removal mode, with no selectable duration.
When those are the only advertised choices, the UI now presents a switch.
The Chinese description is:

> 开启后，摘下耳机一段时间会自动关机。

All seven locales have the corresponding description. Devices advertising
timers retain a capability-filtered selector. The existing auto-power
regressions confirm that this operation uses the power parameter command,
preserves an independently remembered duration, rejects unsupported choices,
and sends no removing-to-pause system command.

## Confirmed shutdown lifecycle defect

Before the change, both production JSON and CLI callers sent the user power-off
command and disconnected the SonyDevice but left DeviceService automatic
recovery enabled. The next maintenance tick reconnected. A ReplyTransport with
paired/connected XM6 discovery and an injected clock reproduced this:

```text
sony-core-tests.exe [poweroff]
connectionState: "retrying" instead of "manually_disconnected"
connection attempts: 2 instead of 1
```

The successful power-off paths now call service.disconnect() after the
device command, disabling automatic recovery. A failed command still reports
failure and retains connection recovery. Explicitly starting a new connection
still works. Both production command paths pass 36 assertions in two regression
cases. No shutdown command bytes or transport timing were changed.

This defect is confirmed independently of sound. There is no listening evidence
showing that it caused the missing shutdown sound.

## Official documentation and static protocol evidence

Sony's current WH-1000XM6 guide distinguishes removing-to-pause from automatic
shutdown and documents shutdown after 15 minutes without wearing the headset:

- [Wearing the headset](https://helpguide.sony.net/mdr/2984/v1/en/contents/TP1001861255.html)
- [Turning off the headset](https://helpguide.sony.net/mdr/2984/v1/en/contents/TP1001856801.html)
- [About the voice guidance](https://helpguide.sony.net/mdr/2984/v1/en/contents/TP1001856908.html)

The current turning-off page describes the physical power button and indicator;
it does not promise a shutdown sound. Earlier chat references to other
TP10018567xx pages and a mandatory shutdown sound were incorrect. The guide
alone cannot establish whether remote shutdown should play a sound.

The supplied Sound Connect 13.3.0 APK was not executed. Its SHA-256 is
e6c5ac8085d5cb785a9391b53ef96944c3aac9dcd20512794a4a4847e819e1a2.
REA class metadata and retained decoded Java show the official remote command
chain g40.a -> yn0.b1 -> USER_POWER_OFF, table-1 payload 24 03 01, which matches
ProtocolV2. The command has no sound argument. Independent power sound settings
exist for some models but were not advertised by this physical XM6.
That absence does not establish that sound is impossible.

Additional REA metadata evidence:

- ev_fda5a9d08d844907776bbdca1f9f7eb65d874692c1dee012ce37a59c18454ce6
- ev_f3b52388b030c29864658ca22402aa06a9c639e8bf55458b714194d13dc2ff90

Metadata establishes declarations; command bodies were checked in the retained
decoded export. Neither establishes headset firmware audio behavior.

## Software validation

Full CTest: 176 passed, zero failed, one Windows-excluded Unix-socket test
skipped (177 registered), 18.85 seconds. The native production-QML scenario
checks real switch clicks in both directions, confirmed state, hidden timer
selector, tray controls, and 35 live language selections across seven locales.
The switch test was first run red against the old selector before the UI
change. It passed after the change. A final focused native UI run also passed
after adding a Chinese screenshot export.

The screenshot xm6-auto-power-off-switch.png was visually checked. Test
preferences remain isolated from user settings.

PowerShell syntax validation passed for the human observation recorder,
tests/hardware/Observe-Xm6Power.ps1. It adapts the diagnosing-bugs HITL template
to Windows and records physical-button, official-app, and desktop-app outcomes
without sending Bluetooth commands. Optional -IncludeRemoval records a separate
wear/removal observation. This script has not been run with a human listener.

No temporary DEBUG instrumentation remains. git diff --check passed.

## Remaining hardware checks

- Actual elapsed shutdown after removal has not been observed.
- The shutdown sound needs an official-app/desktop listening comparison.
- The reconnect fix must not be presented as a verified sound fix.

These observations remain required before calling the complete user goal
validated. The 2.0.2 packages contain the verified software changes and can be
used for the remaining hardware comparison.

## Packages

The Release build and NSIS compilation completed. The portable ZIP passed
CRC verification. Required executable, Qt/QML/plugin, and compiler runtime
hashes matched the installer input payload; all three EXEs matched the
Release build. CLI and daemon --help passed with inherited and System32-only
PATH from the build, installer payload, and extracted portable package.
This rechecks the reported sonyctl C++ runtime entry-point failure.
The installer itself was not executed and an existing installation was not
upgraded.

- SonyDeviceCenter-Qt2-2.0.2-Setup.exe: 59,096,527 bytes.
  SHA-256: 786001b1094f90969d925a3c2a7fc9f2250810f63597ec24e75fbb6082fbd3ce.
- sony-device-center-2.0.2-win64.zip: 76,914,379 bytes.
  SHA-256: f661cf06bfe48d49553487b0f77bdcfc4eec3ccd0b80bf18e1228665d7b3f98a.
- SHA256SUMS-2.0.2.txt contains both checksums.
