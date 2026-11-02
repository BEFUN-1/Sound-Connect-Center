# Windows connection ownership: 2.0.6 candidate

Date: 2026-11-02, Asia/Shanghai.

This record supersedes the permanent-pause recovery rule in the 2.0.5 acceptance
record. The headset and Windows decide whether a system connection exists. The
application follows that connection for its selected headset without opening a
control connection while the system link is disconnected or unknown.

## Findings and changes

- The 2.0.5 startup path interpreted an unavailable initial link as a disconnect
  and persisted a pause. A later Windows connection could not resume control.
  Startup now waits without manufacturing a disconnect event.
- Paused addresses previously never resumed automatically, including after the
  headset reconnected to Windows. A system-disconnect pause now resumes on a
  positive Windows connection for the same selected address.
- A software Disconnect still closes control, cancels queued work, and remains
  effective while the original Windows link is unchanged, including after an
  application restart. Once a system disconnect is observed, the next affirmative
  Windows connection authorizes recovery. This latch is persisted per address in
  `connection/resumeOnSystemConnectionAddresses`.
- Old settings did not retain the cause of a pause. On the first run with the
  new key absent, these records migrate to passive system observation. No new
  control connection is attempted without a positive Windows link.
- Unknown or failed discovery previously closed a healthy control session.
  It now leaves that session intact. If control is also lost, the application
  waits for affirmative system connectivity before recovering.
- Windows discovery returns lowercase addresses, whereas service snapshots use
  uppercase. The controller previously discarded the successful connection
  snapshot because of a case-sensitive target check. The target is now
  normalized before snapshot filtering. A subsequent real-headset test also
  caught the inverse path: automatic connection retained a lowercase selected
  address, so the manual-disconnect snapshot was discarded. Automatic selection
  and incoming UI addresses are now normalized as well. Its regression failed
  before the fix with the service disconnected and the UI still connected.
- Device cards now identify the current connected target by address rather than
  by its model name. Connection labels distinguish Windows connectivity,
  software pause, waiting for Windows, and control recovery.
- Software shutdown retains its separate quiet period and waiting state.
  Explicit Connect remains available; it clears only its selected target.

## Automated validation

The new startup/discovery tests initially failed 24 assertions. The follow-system
tests initially failed six assertions. A separate Qt controller test proved that
the underlying service connected successfully while the UI stayed disconnected
for a lowercase discovery address. All corresponding tests pass after the fix.

Release configuration: version 2.0.6, GCC 13.1, Qt 6.12.0.

- CTest: 202 registered; 201 passed, one Unix socket case skipped on Windows;
  zero failures.
- Qt controller: 27 passed, zero failures.
- Actual main/QML/tray with simulated transport: nine passed, one optional
  long-duration soak skipped; zero failures.
- Recovery subset: 17 cases and 253 assertions passed.
- Tray restore: 30 measured samples, software renderer, P95 30 ms. The prior
  same-renderer baseline was 13 ms; the increase is within the 50 ms budget.
- Worker handles: 243 before and 243 after 30 simulated reconnect cycles.

No new two-hour soak or clean-Windows deployment acceptance is claimed for this
candidate. The earlier 2.0.5 evidence remains scoped to that build.

Evidence is under `build-2.0.5-audit/system-link-fix/`. The reused build directory
is named `build-2.0.5-release`; its newly compiled executables report version
2.0.6. Previously delivered 2.0.5 packages are unchanged.

## Initial real-headset investigation

The user explicitly authorized real-headset tests in this session.

- Passive Windows API checks confirmed a connected WH-1000XM6.
- The actual control channel opened successfully and returned battery 35% and
  normal/off noise mode.
- The real Qt/backend test, using isolated settings with the old pause format,
  successfully restored control and read battery 35%.
- The first real mode test failed with
  `Headset did not confirm the noise-control setting`. The UI observed the
  requested mode, but the command reported a confirmation failure. The original
  noise settings were restored successfully. This is an unresolved hardware
  acceptance failure, not a passing control test.
- Subsequent passive Windows checks reported the headset disconnected. During
  three minutes of waiting, no new system connection was observed and the
  observer made no RFCOMM attempt.

The test executable supports an explicit `SONY_REAL_HEADPHONES=1` opt-in. It
requires an already connected real XM6, traces only noise-control protocol
payloads, exercises the real controller and tray actions, checks manual pause and
hidden-window battery updates, and restores original noise settings. An optional
`SONY_HARDWARE_WAIT_RECONNECT=1` step observes a physical/system disconnect and
reconnect; absence of that action is recorded as unverified, not passed.

## Final real-headset checks on 2026-11-02

Environment: WH-1000XM6; Windows 11 Home build 26200; Intel Wireless Bluetooth.
The firmware query returned an empty string, so no firmware version is claimed.
These checks use the actual Qt main, QML, controller, backend, Windows transport,
and headphone responses. The settings directory is isolated from user settings.

After the automatic-disconnect address fix, the real suite passed:

- An already Windows-connected headset restored control from legacy pause
  settings without a Connect click and reported battery 34%.
- Nine successive real tray actions cycled ANC, ambient, and normal/off three
  times. Each action had a matching device readback and no command error; the
  confirmed mode remained visible between changes.
- Software Disconnect closed control and held the pause for five seconds with
  zero further connection attempts, while Windows remained connected.
- Explicit Connect restored the same headset, exactly once, and restored battery
  and control state.
- With the actual window hidden, a fresh battery read occurred within the
  35-second observation budget; exactly one additional battery query was counted,
  and the tray tooltip contained the reported battery level.
- The suite restored the noise settings recorded at the start of that run.

Result: three passed, zero failed, one optional physical-transition case skipped.
The earlier mode-confirmation failure did not recur in this nine-action run or
the preceding three-action trace. Both notification and fresh inquiry frames
confirmed each mode in the final trace. No speculative protocol change or
guarantee of eliminating every possible transient confirmation error is claimed.

A separate physical-disconnect/reconnect observer waited 60 seconds. No system
disconnect occurred, so that test explicitly skipped. It did not pass a synthetic
event off as hardware validation. The corresponding disconnect, restart, no-steal
and follow-system transitions pass the deterministic service/controller tests.

The resulting 2.0.6 package is a local unsigned Development build validated for
the scope above. Real physical disconnect/reconnect, power-off/power-on,
phone-call/multipoint audio routing, headphone-button actions, clean-Windows
installation/upgrade/uninstall, and repeated battery-life comparisons are still
outside this round's completed hardware checks. No battery-life improvement
percentage is claimed.
