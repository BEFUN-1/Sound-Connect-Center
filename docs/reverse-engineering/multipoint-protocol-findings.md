# Sound Connect 13.3.0 multipoint protocol findings

Inspection date: 2026-10-19. These are static observations and implementation
inferences from the caller-supplied APK. The APK was not executed and this
report does not establish WH-1000XM6 runtime support or persistence behavior.

## Artifact and analysis identity

- APK: `analysis-input/sound_connect_v13.3.0.apk`
- APK SHA-256: `e6c5ac8085d5cb785a9391b53ef96944c3aac9dcd20512794a4a4847e819e1a2`
- Package: `com.sony.songpal.mdr`, version `13.3.0`, version code `130305109`.
- REA CLI: `rea-agents@6.7.0`; JADX headless MCP: `0.7.1`; Java: `17.x`.
- JADX JAR SHA-256: `6e5eacf500b64292bfb73c49797c1958f6ee44646e43e868039ae7feb573ff75`.
  This is the tool JAR digest, **not** the APK digest.
- REA `inspect_android_package` Evidence:
  `ev_5d94c2b8e513f534bb6e4727913d753df6a8b7d1e69d720e6cd7d2d52318314b`.
  The focused complete Evidence JSON records are stored beside this document.
- Supporting local decompilation root:
  `analysis-output/jadx_out_verify8/sources`.
  Line numbers below refer to that existing Java tree, not original Sony source.

## Reproduction commands

The configured desktop MCP did not inherit the already deployed environment.
The authorized CLI fallback was used without modifying REA configuration:

```powershell
$env:REA_JADX_MCP_JAR = '<tool-root>/rea-android/jadx-headless-mcp-0.7.1-all.jar'
$env:JAVA_HOME = '<tool-root>/jdk17'
npx -y rea-agents@6.7.0 inspect-android-package 'analysis-input/sound_connect_v13.3.0.apk' --format json --full-output
npx -y rea-agents@6.7.0 inspect-android-method 'analysis-input/sound_connect_v13.3.0.apk' 'po0.b' b --format json --full-output
```

Every saved `multipoint-rea-*.json` identifies the exact class/method in
`data.parameters`, and contains its full returned Evidence record. Substitute
those selectors into the second command to repeat a focused inspection.
Nested JADX class display names use dots, for example `jo0.a.b` and
`oo0.b.C0944b`; `$` display selectors are not accepted by this provider.

## Capability discovery and limits

All payloads below are hexadecimal and exclude the outer Sony frame. The
peripheral commands belong to MDR V2 table 2 (`DATA_MDR_NO2`, data type `0x0e`).

1. Existing table 1 protocol-info query `{00,00}` returns
   `{01,00,version32BE,table1Enable,table2Enable}`, exactly 8 bytes.
   `in0/k.java:24` checks the layout; getters `e()`/`f()` at lines 40/44 read
   offsets 6/7. `vv/e.java:628-637` gates querying each support-function table on
   these fields. **ENABLE=0 and DISABLE=1**, verified in
   `multipoint-rea-enable-disable.json`.
2. Table 2 function discovery sends `{06,00}` and accepts
   `{07,00,count,(functionCode,displayOrder)*}` with exact length `3+2*count`.
   `jo0/a.java:35-39`, `jo0/b.java:43-44,78-89`. The pair's second byte is
   `cn0.c.c()` and is used for ordering (`cn0/c.java:48-54`), not a boolean.
3. Table 2 function codes in `FunctionType.java:392-395` are `0x30` legacy
   paired-device management, `0x31` source switch/keep, `0x32` paired-device
   management with Bluetooth class for Classic BT, and `0x33` the class variant
   for Classic/LE. Both class variants use peripheral subtype `2`; legacy uses
   `0`; source switch/keep uses `1` (`PeripheralInquiredType.java:6-9`).
4. Query device limits with `{30,managementSubtype}`. Reply is exactly
   `{31,managementSubtype,maxPaired,maxConnected,fileTransferInMultipoint}`.
   `oo0/p.java:43-48,65-75` checks/exposes the fields. `p.f()` reads offset 3;
   `vv/a.java:333-334` forwards `p.g(),p.f(),p.e()` to builder `z0`.
   `DeviceCapabilityTableset2Builder.java:1813-1816,1349-1353` constructs `r0`;
   `r0.java:28-29,51-52` names the second integer Max Connected.
   `r30/b.java:93-94` exposes it to the UI. Offset 4 is `0` possible, `1`
   impossible for file transfer in multipoint; it is not a device limit.

The inspected startup helper (`vv/e.java:405-411`) goes directly from the
protocol-info gate to the two function-list queries. No separate table 2
initialization handshake was found in this bounded startup path.

## Paired, connected and playing devices

Send `{36,managementSubtype}`. Replies use `0x37`; unsolicited list changes
use `0x39`. For the class variant the layout is:

```text
opcode, subtype=02, count,
  [addressUtf8[17], connectedSlot[1], classOfDeviceBE[3], nameByteLength[1], nameUtf8[N]] * count,
playbackrightDevice[1]
```

`po0/b.java:42-55` parses records from offset 3, with per-record fixed size 22
plus the UTF-8 name bytes. Address is the colon-separated 17-byte form. Class
of device is **three-byte big endian**, confirmed by `com/sony/songpal/util/e.java:44-48`.
`com/sony/songpal/util/z.java:12-13` explicitly selects UTF-8.
`po0/b.java:68-75` locates the final playback-right byte; lines 77-118 validate
the record count, address syntax, name lengths 1..128, and exact final length.
The legacy subtype omits the three class bytes and uses the same address,
connected-slot/name and final playback-right concepts (`oo0/s.java`).

`connectedSlot > 0` means currently connected; `0` means pairing history.
Connected rows are sorted by their positive slot (`po0/b.java:26-39,58-65`).
The slot is not an index into the complete paired list. The UI compares
`device.connectedSlot == playbackrightDevice` to display the playing icon
(`feature/multipoint/view/v.java:1215-1220`). An implementation should also
require a positive slot so history entries cannot be marked as playing when
the final byte is zero.

Mapping through `oo0.a` into `p30.k` is preserved in `r30/a.java:77-82`:

| Getter | Meaning |
| --- | --- |
| `b()` | Bluetooth device address |
| `d()` | Connected slot/status (0 is history) |
| `c()` | Bluetooth friendly name |
| `a()` | Bluetooth class of device; legacy unknown default is `0xffffff` |

## Control messages and real result messages

| Operation | Request payload | Result payload |
| --- | --- | --- |
| Read source keep | `36 01` | `37 01 onOff` (3 bytes) |
| Set source keep | `38 01 onOff` | `39 01 onOff result` (4 bytes) |
| Switch playing source | `3c 01 address[17]` | `3d 01 result address[17]` (20 bytes) |
| Disconnect/connect/unpair | `3c managementSubtype action address[17]` | `3d managementSubtype action result address[17]` (21 bytes) |

`onOff` is **0 for ON, 1 for OFF**, not C++/JSON boolean encoding. A normal
frame ACK only acknowledges transport and is insufficient to report an
operation as successful.

The layouts are checked by `oo0/u.java:14-15`, `oo0/l.java:15-16`,
`oo0/g.java:15,28-33`, `oo0/f.java:49,66-71`, and their parent dispatchers.
`oo0/c0.java:29-33`, `oo0/z.java:29-36` and `oo0/y.java:62-67` construct the
corresponding writes. `ConnectivityActionType.java:6-8` assigns `0` disconnect,
`1` connect, `2` unpair. `r30/b.java:59-67,98-100` maps the feature API to them.

Source keep/switch results: `00` success, `01` failure, `02` calling,
`03` no A2DP connection, `04` voice-assistant priority. Connection-management
results are operation-specific:

| Operation | Success | Error | In progress | Busy |
| --- | --- | --- | --- | --- |
| Disconnect | `00` | `01` | `02` | `03` |
| Connect | `10` | `11` | `12` | `13` |
| Unpair | `20` | `21` | `22` | `23` |
| Pairing | `30` | `31` | `32` | `33` |

The enum `<clinit>` Evidence uses smali fallback and preserves the actual
constructor constants, avoiding decompiler aliases such as BSON constants.

## Sony UI sequencing and the meaning of default

The APK implements a global source-keep flag and the current playing source.
The bounded analysis did not find a separate persistent default-address field.
For the desktop UI, the user's default device can be presented as a fixed
playing source, while the runtime values remain explicit.

- Fix the already playing device: set source keep ON.
- Switch and fix another device: if source keep is OFF, turn it ON and wait
  for its success result, then switch to the selected address; if keep is
  already ON, switch directly (`v.java:539-584`).
- Switch normally: if source keep is ON, turn it OFF and wait for success,
  then switch; otherwise switch directly (`v.java:655-730`).
- Cancel fixation: turn source keep OFF (`v.java:588-605`).
- The pending source-switch action runs only after the keep result is
  `SUCCESS` (`v.java:920-955`). Do not continue after a keep error.

This is static APK behavior; cross-power-cycle persistence, actual audio
handover, and WH-1000XM6 firmware support need real-device readback.

## Evidence ledger

| Complete local Evidence record | Evidence ID | Observation |
| --- | --- | --- |
| `multipoint-rea-function-get.json` | `ev_39204c807be746fa27cdf07714d97160cbe0fc543bbf754b8c9eec5fa9ba8f64` | `jo0.a.b.f`, function query builder |
| `multipoint-rea-function-list.json` | `ev_a75002d7faf1a328028ba7e969374099860566359ec674058f7373df1e9c54f8` | `jo0.b.c`, table 2 function pairs |
| `multipoint-rea-capability-get.json` | `ev_c369c42b8e9b8c614f73aa87c6812f8e6aa4a670851b43cb969b36450a2e5869` | `oo0.b.C0944b.f`, capability query builder |
| `multipoint-rea-capability-max-connected.json` | `ev_8035ae00f0895776fab8abdbb2c8674dc544fc2d034eb3a594113db309949881` | `oo0.p.f`, maximum connected offset |
| `multipoint-rea-class-list.json` | `ev_885c02d8825d051abd1d0a4a58082f8154109dedfedca5c14518b857bf15817e` | `po0.b.b`, class-bearing device list |
| `multipoint-rea-class-endian.json` | `ev_efe3f0dd8d4f5625e31a4f3c8b2d50876e438aef8c497cc9acd857d2e3b5d63b` | `util.e.f`, three-byte big-endian value |
| `multipoint-rea-playing-mapping.json` | `ev_4dc22b48fdea9ab447ff2750435141f356ec165837d0898b5e15b0dd4183b2fa` | `view.v.r0`, connected slot equals playback right |
| `multipoint-rea-on-off.json` | `ev_6e0374aa2f2229e4617b887e6ce3cc26ce5371a28ecdae7416ae4bb94f1cfdc3` | ON=0, OFF=1 (smali fallback) |
| `multipoint-rea-enable-disable.json` | `ev_028c203eb1ad9d2e5f030909a6bbaf64f5b94eb1627560020101befc0f6a40ad` | ENABLE=0, DISABLE=1 (smali fallback) |
| `multipoint-rea-source-result-values.json` | `ev_5e40e5b2f5045d43ff1d9c8f7cafab058ac0d857e985dd7a43016b1a6a34404e` | Source result bytes (smali fallback) |
| `multipoint-rea-connection-result-values.json` | `ev_91e4dcd9bb2e28981157e1105a6d88765c2eee89be5ce5911a10ce621bd6102d` | Management result bytes (smali fallback) |
| `multipoint-rea-keep-query-format.json` | `ev_997c7799b32b2e80545423d9be0f50e4a23e01b34e33084f0742c379ed95af64` | `oo0.u.b.b`, keep read length 3 |
| `multipoint-rea-keep-notify-format.json` | `ev_12baff1b8746e497884ab7b29d2bcc430b15fcb175f3e5cc521d4ef3a5513801` | `oo0.l.b.b`, keep result length 4 |
| `multipoint-rea-change-and-fix.json` | `ev_323d6e12e293482907b07bb6fc4091bf1b40ec1adbdcce869c9d92193b14e631` | Enable keep before changing source |
| `multipoint-rea-change-source.json` | `ev_b65c6dc41a74039f7d311b45af4487e0b17b43ce0ff15186b6fec345a4f9609e` | Disable keep before normal source switch |

## Coverage and remaining unknowns

Answered statically: table 2 discovery, management limits, class list fields
and endian, connected/history split, playing marker, keep flag encoding,
control request/result fields, result constants, and Sony UI sequencing.

Not observed: headset firmware responses, concurrent phone/PC operation,
notification timing, source-keep persistence after power off, audible handover,
or Bluetooth reconnection following removal of the controlling PC. No runtime
capture, emulator, APK installation, or headset mutation was performed for this
report. Original-source positions, reflection and dynamic-call completeness
remain unknown as stated in every retained REA record. Keep unsupported
controls disabled until the device advertises support and supplies valid data.
