# Sound Connect 13.3.0 NCASM findings

Inspection date: 2026-10-19. These are static observations from the supplied
APK; the APK was not executed and no headset mutation was performed.

## XM6 dual NC/ASM parameter payload

The WH-1000XM6 response observed during the existing hardware readback is:

```text
67 17 <valueChangeStatus> <ncAsmOnOff> <ncAsmMode> <ambientMode> <ambientLevel>
```

Subtype `0x17` is
`MODE_NC_ASM_DUAL_NC_MODE_SWITCH_AND_ASM_SEAMLESS`. The APK parser reads:

- byte 2: `ValueChangeStatus`, `UNDER_CHANGING=0`, `CHANGED=1`;
- byte 3: `NcAsmOnOffValue`, `OFF=0`, `ON=1`;
- byte 4: `NcAsmMode`, `NC=0`, `ASM=1`;
- byte 5: `AmbientSoundMode`, `NORMAL=0`, `VOICE=1`;
- byte 6: unsigned ambient level.

The set builder emits the same field order after `68 17`:

```text
68 17 <valueChangeStatus> <ncAsmOnOff> <ncAsmMode> <ambientMode> <ambientLevel>
```

Evidence: `ev_076b8c179e862683a529ff9fb8f9d76f6e460a053b53340be850790c1bd47014`
(`sn0.c1.b.h`) and
`ev_90466342c6db185035049a2d8865ac7093a2f70ba4b1ef4cddcdd5416f2be92`
(`un0.f.e`).

## Status and level behavior

Sound Connect's mode shortcut maps NC and ASM to `NcAsmSendStatus.ON`, and OFF
to `NcAsmSendStatus.OFF`. Both statuses use `ValueChangeStatus.CHANGED`, hence
byte 2 is `1` for a completed mode switch. `UNDER_CHANGING` (byte 2 `0`) is
used for an in-progress adjustment. Evidence:
`ev_e47b8c4b41d31c9c62cef44c9a4cd924658b6b9bb92b929eec2e42fa28998c93`
(`NcAsmSendStatus.<clinit>`) and
`ev_dc1f1fa4501960db9492919e5683ba734e3d84e6b756b79b6e6157bab9affc60`
(`u1.m`).

The shortcut passes the current ambient level through for NC, ASM, and OFF
changes. It does not replace the level with zero or one when the mode is not
ambient. A desktop implementation should retain the last valid level and only
change it when the user explicitly adjusts ambient level.

The capability response for NCASM is a list of records:

```text
61 <inquiredType> <count> [<ambientMode> <min> <max> <step>] * count
```

The parser validates `min >= 0`, `max >= 1`, and `step >= 1`, each within an
unsigned byte. It does not impose a fixed maximum such as 20. Evidence:
`ev_da5101cd8780665a4944b190f495cff4dfaca3d241df15e0c94ea534850f94f9`
(`un0.b.a`) and the APK's `cn0.b` range constructor.

## Implementation implication

The existing desktop parser should accept response status bytes `0` and `1`,
interpret byte 3 as the on/off value, and preserve the most recently read
ambient level while switching between normal, noise-cancelling, and transparent
modes. Actual write acceptance and firmware persistence remain unverified on
hardware.
