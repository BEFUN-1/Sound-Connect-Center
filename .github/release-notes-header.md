Sony Device Center is an independent open-source project, free for anyone to use,
study and modify. It is **not affiliated with, endorsed by, or connected to Sony
Corporation**. Sony and all product names are trademarks of their respective owners.
It talks to the headphones over a reverse-engineered protocol, for interoperability.

## Contents

- **`sony-device-center`** — Qt 6 / QML desktop app
- **`sonyd`** — background daemon with a local IPC socket
- **`sonyctl`** — scriptable CLI
- **`sony-core` / `sony-protocol` / `sony-transport`** — C++20 libraries

## Install

```bash
# Fedora / RHEL (dnf installs the RPM)
sudo dnf install ./sony-device-center-*-Linux.rpm

# Debian / Ubuntu
sudo apt install ./sony-device-center-*-Linux.deb
```

Windows: run `SonyDeviceCenter-Setup.exe`, or use `SonyDeviceCenter.exe` from
the portable package. The Windows desktop package bundles the .NET 8 Desktop
Runtime and the native backend; the Qt packages bundle the Qt and MSVC runtimes.
The Linux packages depend on system Qt 6.

macOS (13+, Apple silicon and Intel): open the `.dmg` and drag **Sony Device
Center** to Applications. `sonyd` and `sonyctl` live inside the bundle at
`Contents/MacOS`.

### macOS: “Apple could not verify” warning

Builds distributed without Developer ID signing and Apple notarization trigger
Gatekeeper (see the macOS launch notes in the repository issue tracker).
If you trust this release, try opening the installed app once, click **Done**,
then go to **System Settings → Privacy & Security → Open Anyway** and confirm.
On macOS 15 Sequoia and later, right-click → Open does not override this warning.
See [Apple's instructions](https://support.apple.com/en-us/102445).

If Open Anyway is unavailable, the workaround reported in the issue tracker is:

```bash
xattr -dr com.apple.quarantine "/Applications/Sony Device Center.app"
```

This bypasses the quarantine check for this app and its bundled files; it does
not verify their safety or notarize them. Only use it for a release you trust.
The recursive `-r` is required for quarantined files inside the bundle.

## Known limitations

- **IPC is Unix-socket only.** On Windows `sonyd` cannot serve `sonyctl` or the GUI —
  it reports that it is listening while nothing is. Both clients fall back to a direct
  Bluetooth session, which does work.
- Protocol generation is selected from the Bluetooth device name, not from the SDP
  service UUID. Connecting by address alone to a Protocol V1 headset is not yet safe.
- The desktop app reads device state once at startup and does not refresh.

---
