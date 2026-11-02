# Qt 2.0.3 reconnect validation

Date: 2026-11-01 (Asia/Shanghai).

## Report and reproduction

The user confirmed that desktop power-off now produces the headset shutdown
sound. They then reported that, after powering the headset back on and restoring
its Windows Bluetooth connection, the application remained disconnected and
could not control it until restarted.

The 2.0.2 successful JSON/CLI power-off handlers called service.disconnect().
That operation permanently disabled the automatic connection policy. No
subsequent maintenance tick inspected the returning system Bluetooth link.
This was a regression from treating requested power-off as manual disconnect.

A new deterministic integration test drives the actual production JSON and CLI
handlers, simulates system Bluetooth disconnected/connected discovery states,
runs maintenance on an injected clock, and sends ANC afterward. Before the fix:

```text
sony-core-tests.exe "Powering the XM6 back on*"
1 test case failed; 8 passed and 8 failed assertions.
service.isConnected(): false
snapshot connected: false
connection attempts: 1 rather than 2
ANC command: unsuccessful
```

## Change

IDeviceService now exposes powerOff() as a distinct operation. DeviceService
sends the existing headset command and closes the control session, preserving
the user's confirmed shutdown path. It then enters waiting_for_power_on rather
than permanently disabling reconnect.

There is a five-second quiet period after success, followed by system connection
checks every second. Only the same headset is eligible. A known disconnected
system link suppresses RFCOMM attempts. When the OS link returns, the control
session reopens and normal connection/state publication resumes. An early
control-service failure retries with the existing backoff. An unavailable system
connection hint falls back to attempts after the quiet period. Manual disconnect
still suspends all recovery.

The official power-off payload and SonyDevice disconnect timing are unchanged.
The five-second quiet period is application policy, not a measured firmware
shutdown deadline.

## Verification scope

Core regressions cover both JSON and CLI, immediate shutdown protection, no
connection attempts while the OS reports off, ignoring a different connected
headset, a missed disconnect observation, manual disconnect, command failure,
and delayed control-service readiness.

The native UI scenario uses production main.cpp, QML, controller, worker, and
service, with simulated Bluetooth discovery/transport and an injected clock.
It exercises two shutdown/reconnect cycles, starting with the real tray menu
and then the QML power button. It checks the displayed connection text, power
button re-enablement, and an ANC command without restarting the application.

No physical headset power cycle has been performed by the agent for this
regression. Hardware behavior and the user's new sound confirmation are
distinguished from simulated lifecycle verification.

Results: 64 assertions passed in six focused power-off regression cases.
The full suite passed 180 tests with zero failures and one Windows-excluded
Unix-socket test skipped (181 registered), in 20.94 seconds. The native UI
scenario passed six QtTest cases, including the two reconnect cycles, in
6.877 seconds. git diff --check passed and no temporary DEBUG instrumentation
remains.

An additional live read-only check confirmed Windows discovery reports this
WH-1000XM6 as system-connected. The focused read-only probe completed noise,
auto-power options, firmware (3.0.0), and function-list reads. A broader CLI
settings-refresh attempt had previously reported "Bluetooth link closed while
refreshing device state" after its initial info reply. These observations do
not prove the physical shutdown/restart cycle; no shutdown command was sent
to the real headset during this validation.

## Packages

Release and NSIS builds completed. Portable ZIP CRC verification passed.
Required executable, compiler runtime, Qt/plugin/QML hashes matched the
installer input payload; all three packaged EXEs matched the Release build.
CLI and daemon startup passed with inherited and System32-only PATH from
both installer payload and extracted portable package. The installer was not
executed or used to upgrade the existing installation.

- SonyDeviceCenter-Qt2-2.0.3-Setup.exe: 59,084,148 bytes.
  SHA-256: 7de15f77b5d50d10262164d25471c3aafdf27e8887d96a44cb0c14c7ca772996.
- sony-device-center-2.0.3-win64.zip: 76,918,619 bytes.
  SHA-256: fbf717ae08dae09f9928a0a83bb0a928fd108c8a9c747fd82e102bb780288b3e.
- SHA256SUMS-2.0.3.txt contains both checksums.
