# Sound Connect 13.3.0 NCASM subtype `0x19`

Inspection date: 2026-10-19. These are static observations from the supplied
APK, combined with the user's XM6 packet readback. The APK was not executed.

## Meaning

`0x19` is
`MODE_NC_ASM_DUAL_NC_MODE_SWITCH_AND_ASM_SEAMLESS_NA`, the dual NC/ambient
mode with noise adaptation. The table-1 capability function is
`MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT_NOISE_ADAPTATION`
(FunctionType byte `0x6d`).

The parameter payload is nine bytes:

```text
66/67/68/69 19 status effect ncAsmMode ambientMode ambientLevel adaptiveOnOff sensitivity
```

The fields are:

- byte 2: `ValueChangeStatus` (`UNDER_CHANGING=0`, `CHANGED=1`);
- byte 3: total NC/ASM effect (`OFF=0`, `ON=1`);
- byte 4: `NcAsmMode` (`NC=0`, `ASM=1`);
- byte 5: `AmbientSoundMode` (`NORMAL=0`, `VOICE=1`);
- byte 6: unsigned ambient level;
- byte 7: noise-adaptive on/off (`OFF=0`, `ON=1`);
- byte 8: noise-adaptive sensitivity (`STANDARD=0`, `HIGH=1`, `LOW=2`).

The set request is therefore:

```text
68 19 <status> <effect> <ncAsmMode> <ambientMode> <level> <adaptiveOnOff> <sensitivity>
```

Evidence: `ev_beab3fee2d5788e68429c9c08d1d7a23d2ae5b4d20f08ba4deb77ca24c1577b0`
(`sn0.d1.b.h`) and
`ev_2e07d897aae6c6b8734a3aca0b4c32d69253ac9488fc69a16c3af3526de529e4`
(`un0.g.g`).

## XM6 packet interpretation

The reported readback/notification:

```text
69 19 01 01 00 00 0a 00 00
```

means a completed (`1`) enabled (`1`) NC (`0`) normal ambient submode (`0`),
ambient level `10`, noise adaptation off (`0`), and standard sensitivity (`0`).
The generic notification parser validates bytes 2 and 3, then subtype `0x19`
is parsed by `sn0.h` using the same fields at offsets 4, 5, 6, 7 and 8.

The app's `w30.e` implementation preserves and resends the adaptive on/off and
sensitivity values when changing NC/ASM/off mode. A desktop implementation
must not reduce subtype `0x19` to the seven-byte subtype `0x17`, otherwise it
will omit the two adaptive fields and mis-handle XM6 notifications.

## Capability query

The capability query is `60 19`, with a `61 19` response containing one record
per ambient mode:

```text
61 19 count [ambientMode min max step] * count
```

`min`, `max`, and `step` are unsigned byte range values. The APK uses the same
range parser as other adjustable ambient modes; it does not hard-code a level
maximum.
