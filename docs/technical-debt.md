# Remaining technical debt

Updated 2026-11-02 for the Qt 2.0.6 development preview. Current validation and
production release blockers are recorded in `acceptance-2.0.6-2026-11-02.md` and
`release-preparation.md`. Historical hardware observations below do not replace
acceptance of the current version.

## Highest priority

1. **Hardware acceptance and protocol gaps (#8–12).** Test reconnect, physical NC
   button changes, battery refresh, and commands on XM3/XM5 hardware. V1 battery,
   NC and EQ readback is decoded and verified on an XM4; the XM3 still needs a
   re-test on the same path, and V1 DSEE / auto power-off remain unimplemented.
   V1 Speak-to-Chat (XM4 Smart Talking Mode, `F6 05` plus config `FC 05`
   Standard timeout) is wired; hardware on a WH-1000XM4 confirmed enable and
   that a talking session closes after ~30s. The timeout picker is still a
   nice-to-have (see UI section).
   WH-1000XM6 10-band EQ is implemented, with packet fixtures and community
   hardware verification on firmware 3.1.5. Limited 2.0.6 XM6 checks confirmed
   noise-control readback and battery refresh, including a hidden window.
   Broader firmware coverage and intermittent failure investigation remain
   open, as does MDR-1000X connectivity.
   ACK receipt still cannot establish that an EQ change had an audible effect.
2. **Sanitizer validation.** The local ASan build cannot link because
   `/usr/lib64/libasan.so.8.0.0` is missing. Re-run ASan/UBSan in CI or after fixing
   the local compiler runtime. Normal builds and regression tests are separate
   evidence; they do not replace sanitizer coverage.
3. **Native platform checks.** Historical Linux tests and current Windows
   Qt/MinGW builds and simulated tests have passed. Limited XM6 hardware
   observations are recorded in the versioned validation documents. macOS and
   current Linux changes still require CI/native acceptance.
   Windows IPC is still unsupported; socket tests explicitly skip it.
4. **Bluetooth ownership across endpoints/processes (#29).** `--direct` refuses a
   daemon at its configured endpoint. Two different custom endpoints, a direct GUI
   already running when sonyd starts, or third-party software can still compete
   for RFCOMM. Add a per-device process lock or an ownership broker before claiming
   global exclusion.
5. **Pairing / connecting-mode window on legacy owners (XM4 hardware).** The
   Windows GUI now persists a pause after manual/system/unknown disconnects
   and requires a positive OS link for automatic recovery. Other platforms and
   daemon/CLI owners retain their existing policy. Re-test their pairing-mode
   hold-off before claiming the Windows GUI fix applies to every owner.

## Service and IPC follow-up

- **Serial operation budgets.** The GUI uses a persistent Qt I/O thread and a
  responsive coordinator, with immutable notification snapshots and cancellable
  Windows socket waits. The protocol remains serial: a pending wire response can
  delay the next command or battery query until its existing budget expires.
  Daemon/CLI and other native connectors still need equivalent cancellation and
  status-publication validation.
- **SDK concurrency contract.** Applications serialize access, but the service still
  exposes `activeDevice()` as a raw pointer and uses a coarse recursive mutex. Move
  callers to typed service operations and immutable complete snapshots before
  advertising arbitrary concurrent SDK use.
- **Client resource/failure testing.** Existing tests exercise idle, oversized,
  disappearing, malformed, and duplicate clients. Add deterministic injected partial
  writes, queue saturation, slow readers, absolute slowloris deadlines, actual
  cross-UID peer rejection, and concurrent startup stress. The bounded queue may
  return a legacy-format busy/error response to a JSON client; make all transport
  errors preserve the request's structured envelope.
- **Compatibility negotiation.** New GUI clients require version 1 structured IPC
  and tell users to upgrade old daemons; legacy CLI requests remain supported.
  Add explicit negotiation and compatibility fixtures before the next wire version.
- **Endpoint portability.** Secure paths reject symlink components, including custom
  paths through common macOS aliases. Document canonical paths in native packaging.
  Consider directory-descriptor-relative operations for stronger protection against
  path replacement in exotic, writable custom ancestor directories.
- **Discovery metadata.** The Linux and macOS connectors list paired and connected
  devices and set the paired/connected hints. On macOS, `connected` comes from
  `[isConnected]`, which is NO for headsets that connect only for audio or BLE. The
  Windows connector exposes paired/connected hints. `SonyDeviceDiscovery`
  requires a valid address and headphone model name; an OUI alone is not
  sufficient. A renamed device without a recognized name may disappear.
  Automatic connection still requires an explicitly selected address.
  Bound the D-Bus calls, and read the
  repeated properties in one snapshot.
- **Notification/polling efficiency.** Direct sessions consume notifications;
  daemon clients poll cached snapshots once per second. Add IPC subscriptions only
  after backpressure and per-client event limits are designed. Rotating hardware
  refreshes take longer than five seconds when requests time out.
- **Typed CLI output.** The GUI has stopped parsing prose. Legacy human-readable
  formatting still lives in IpcProtocol for compatibility; move presentation to
  sonyctl when introducing a structured CLI mode. Some optional controls exist only
  in the structured API, not the legacy CLI parser.

## UI, packaging, and maintenance

- Add interactive QML tests for switch/slider rollback, rapid actions, device
  switching, and daemon disappearance/version mismatch. Current Qt tests cover
  controller/worker behavior, and the smoke test checks QML loading.
- Speak-to-Chat timeout and sensitivity (nice-to-have). The on/off toggle
  always writes Standard (~30s). Headphones Connect also exposes ~15s, ~1 min,
  and "do not close automatically." Those are already on the wire as `FC 05`
  timeout `0x00` / `0x01` / `0x02` / `0x03` (plus Auto/High/Low sensitivity).
  Surface them in the GUI and IPC later; do not default to `0x03`.
- Improve per-feature unknown/stale/error presentation beyond the current metadata,
  battery/noise/codec labels, feature descriptions, and error footer. Show separate
  left/right/case batteries and richer read-error details where useful.
- The GUI queues up to 32 pending operations, coalesces adjacent unsent slider
  values, and preserves confirmed state separately. Expand real-device coverage
  for rapid interactions on multiple controls and delayed notifications.
- Wire controller error dismissal into the UI and improve lifecycle translations;
  new diagnostic/pending strings currently use English.
- Core JSON uses a vendored, pinned nlohmann/json v3.12.0 header. Document/update its
  provenance when upgrading and add an installation/export rule if SDK distribution
  is introduced. No new package-manager dependency is required for current builds.
- Release source remains uncommitted. Review the Qt migration and 2.0.5 fixes,
  commit them, and build a new tagged release instead of uploading old local
  2.0.0–2.0.3 packages. Production builds now reject dirty/tag-mismatched source.
- Windows Authenticode signing is wired and required for production packages,
  but no certificate has been supplied or signing run locally. Configure the
  secrets and timestamp endpoint documented in `packaging/README.md`.
- Release attestation (#7; macOS signing has hooks, see packaging/README.md), Main.qml extraction
  (#21), AUR packaging (#22), and cosmetic work remain deferred.

## Local test instructions

Build and test:

```sh
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

For a real-device test, stop the installed daemon first to avoid mixing versions,
then start the freshly built daemon and GUI in separate terminals. Replace the
example address with the explicitly selected headset's paired Bluetooth address:

```sh
systemctl --user stop sonyd.service
./build/apps/sonyd/sonyd -v --device AA:BB:CC:DD:EE:FF
```

```sh
./build/apps/device-center/sony-device-center
./build/apps/sonyctl/sonyctl status
```

Start with headphones off, turn them on, then test power cycling and the physical
NC/ambient button. Check that failures are visible and the UI stays responsive.
For hardware-free testing use `sonyd --simulated` instead of the real-device command.
The socket path now requires a private directory; the default handles this.
