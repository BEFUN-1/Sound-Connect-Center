# Sony Device Center Desktop

Release 1.3

This Windows Forms client controls paired Sony headphones through the bundled
`sonyctl.exe` backend. It is intended for the WH-1000XM6 and provides:

- the headset's connected devices and paired history, including its playback source;
- switch playback, keep a preferred source, connect/disconnect and remove a pairing;
- normal, noise-cancelling, and ambient sound modes;
- equalizer presets and custom bands, including the WH-1000XM6 ten-band path;
- DSEE enhancement control;
- battery percentage and connection status;
- immediate headphone power-off;
- optional launch at Windows sign-in, disabled by default and controlled in settings;
- closing the window hides the app to the notification area; the tray menu shows
  the latest battery text and provides normal, noise-cancelling, and ambient modes.

The installer is `SonyDeviceCenter-Setup.exe` in the repository root. It installs
to the current user's `%LOCALAPPDATA%\Programs\Sony Device Center` directory,
creates both Start Menu and Desktop shortcuts, includes the .NET 8 Desktop
Runtime and a static native backend. The target machine does not need to install
.NET or add a compiler to PATH. An uninstaller is included.

The preferred playback source uses Sony's source-keeping function; persistence
across headphone restarts depends on the firmware. Unsupported controls are
disabled. Normal operation displays device data; sample data is restricted to
the explicit `--preview` layout check.

To rebuild in this workspace:

```powershell
.\desktop-app\build-desktop.ps1
.\desktop-app\package-installer.ps1
```

The 1.3 release includes recovery for a stale RFCOMM control session after
audio ownership returns from a phone call. A timed-out command now invalidates
the session and the next operation reconnects and completes the protocol
handshake before sending another control command.

The UI invokes `sonyctl.exe --direct`, so it does not require a separate daemon.
Bluetooth pairing and exclusive access are still managed by Windows and the
headset firmware. The startup option uses the current user's Run key and does
not require administrator privileges.
