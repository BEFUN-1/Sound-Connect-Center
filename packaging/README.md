# Packaging & Distribution Guide

Sony Device Center supports multi-platform packaging for Linux, macOS, and Windows.

The **2.0.6 stable release** distributes the exact existing Windows installer
verified and explicitly approved by the maintainer on November 2, 2026.
The download filename omits `-dev`; its bytes and SHA-256 are unchanged. It has
no Authenticode signature and was not rebuilt for publication. See
[the release notes](../docs/releases/v2.0.6.md) for approval and provenance.
Linux/macOS packaging below describes build support; this release includes
no binaries for those platforms.

## 1. Linux Packaging

### CPack (DEB, RPM, Tarball)
From the CMake build directory:

```bash
# Build release binaries
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Generate .deb package (Debian / Ubuntu)
cpack --config build/CPackConfig.cmake -G DEB

# Generate .rpm package (Fedora / RHEL / openSUSE)
cpack --config build/CPackConfig.cmake -G RPM

# Generate .tar.gz archive
cpack --config build/CPackConfig.cmake -G TGZ
```

### AppImage
Run the included build script with `linuxdeploy`:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

./packaging/linux/build-appimage.sh build
linuxdeploy-x86_64.AppImage --appdir build/AppDir --plugin qt --output appimage
```

### Flatpak
Build using `flatpak-builder`:
```bash
flatpak-builder --force-clean build-dir packaging/linux/com.github.befun1.soundconnectcenter.yml
flatpak-builder --run build-dir packaging/linux/com.github.befun1.soundconnectcenter.yml sony-device-center
```

### Systemd Service
To run the `sonyd` background daemon automatically at user login:
```bash
mkdir -p ~/.config/systemd/user
cp packaging/linux/sonyd.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now sonyd.service
```

---

## 2. Windows Packaging

### Portable ZIP & NSIS Installer
Qt 2.0 is the only active Windows package. CPack produces the portable ZIP;
`packaging/windows/build-qt-installer.ps1` produces the per-user NSIS installer.
It installs per user under
`%LOCALAPPDATA%\Programs\Sony Device Center`, preserves an enabled
`SonyDeviceCenter` startup entry with `--tray`, and runs with `asInvoker`.
The installer and uninstaller reject elevated tokens, custom `/D` locations,
and symbolic links or junctions in the installation tree or its ancestors.
HKCU cannot override the installation directory. The finish-page launch is unchecked.
Close the application from its tray before installing or uninstalling.
The installer does not execute existing applications or kill processes by name.
Uninstall deletes only files listed in the package manifest; unknown files
and legacy WinForms runtimes are retained.

Remove earlier custom installations using their original uninstaller, started
without administrator privileges. The current installer does not reuse their
registry-controlled path or recursively remove their runtime.

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Build an unsigned development installer; distribution requires a separate release decision
.\packaging\windows\build-qt-installer.ps1 -BuildDir build `
  -Development `
  -MakeNsis 'C:\path\to\nsis\makensis.exe'

# Generate portable ZIP in the build directory
cpack --config build/CPackConfig.cmake -B build -G ZIP -C Release
```

Installers default to `build/artifacts/`; development filenames include `-dev`.
Historical binaries are kept outside the source repository; validation
documents remain in `docs/history/`.

### Release source and Authenticode

Automated signed production packaging requires clean committed source at `v<project version>`,
including no untracked source files. The build script verifies the source
checkout associated with the build directory, reconfigures and rebuilds it.
Use a new build directory for release acceptance.

Automated signed production packaging requires a code-signing certificate with its private key
in the current user's certificate store, Windows SDK SignTool, and an RFC 3161
timestamp endpoint from the certificate provider:

```powershell
python packaging/verify-release-source.py
.\packaging\windows\build-qt-installer.ps1 -BuildDir build `
  -MakeNsis 'C:\path\to\nsis\makensis.exe' `
  -SigningCertificateThumbprint '<40-hex-certificate-thumbprint>' `
  -TimestampServer 'https://<certificate-provider-timestamp-endpoint>'
# Run CPack after signing so portable EXEs have the same signatures.
cpack --config build/CPackConfig.cmake -B build -G ZIP -C Release
```

`sign-artifacts.ps1` signs the three project executables before staging, then
signs the installer. Each operation is followed by `signtool verify /pa /all /tw`;
failure stops packaging. Third-party Qt/runtime libraries retain their original
signatures.

The Release workflow requires these values for tag builds:

| GitHub configuration | Purpose |
| --- | --- |
| Secret `SONY_WINDOWS_CERTIFICATE_BASE64` | PFX encoded as base64 |
| Secret `SONY_WINDOWS_CERTIFICATE_PASSWORD` | PFX password |
| Variable `SONY_WINDOWS_TIMESTAMP_SERVER` | Certificate provider's timestamp endpoint |

The workflow imports the private key into the ephemeral runner user's store,
removes the temporary PFX in a `finally` block, and removes the imported signing
certificate after packaging. No certificate is committed. Missing credentials
block production releases. Manual previews use `-Development` and do not publish.
No real signing was performed locally because no signing certificate was provided.

Each platform includes `SOURCE-<platform>.json` with commit, version and tag.
The publishing job emits `SHA256SUMS` with asset basenames and creates a new
Release without replacing existing attachments. See
`docs/release-preparation.md` before uploading.

---

## 3. macOS Packaging

### Disk image
One target, one script. It runs `macdeployqt`, folds `sonyd` and `sonyctl` into the
bundle, signs, and writes the DMG with [dmgbuild](https://dmgbuild.readthedocs.io/)
(no Finder scripting, so it works on a headless runner).

```bash
pipx install dmgbuild
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
cmake --build build --target dmg              # or: packaging/macos/build-dmg.sh build
packaging/macos/verify-dmg.sh build/*.dmg     # mounts it and checks what is inside
```

The release workflow builds it universal with `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`
on the Qt archive from `install-qt-action`; Homebrew Qt gives you the host
architecture only.

Files in `packaging/macos/`:

| File | Role |
| :--- | :--- |
| `build-dmg.sh` | Deploys Qt, signs the bundle, builds and optionally notarizes the DMG. |
| `verify-dmg.sh` | Mounts a DMG and asserts Qt, QML modules, daemon, CLI, signature. CI gate. |
| `prepare-signing.sh` | Imports GitHub release credentials into a temporary keychain. |
| `app.entitlements` | Allows the Qt Quick GUI's JavaScript JIT under hardened runtime. |
| `dmgbuild.py` | Window geometry and icon positions for dmgbuild. |
| `gragen.py` | Paints `dmg-background.png` / `@2x` from the app's palette. Standard library only. |
| `Info.plist.in`, `AppIcon.icns` | Bundle metadata and icon, used by CMake. |

### Signing and notarization
Off by default. Ad-hoc builds pass signature integrity checks but can still be
blocked by Gatekeeper. A trusted production distribution requires an Apple Developer Program
membership, a **Developer ID Application** certificate with its private key,
and notarization credentials.

For a local Mac with the certificate already imported into its keychain:

```bash
export SONY_CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
export SONY_NOTARY_PROFILE="notary"    # from: xcrun notarytool store-credentials notary
export SONY_REQUIRE_NOTARIZATION=true
cmake --build build --target dmg
packaging/macos/verify-dmg.sh build/*.dmg
```

The script signs nested code before the app, using hardened runtime and secure
timestamps. Only the GUI gets the JIT entitlement; helpers keep the default
runtime protections. The DMG is also signed. Notarization must return `Accepted`
before its ticket is stapled and validated. With `SONY_REQUIRE_NOTARIZATION=true`,
the verifier also requires Gatekeeper acceptance of both the DMG and app.
It fails instead of silently producing an ad-hoc release if credentials are missing.

#### GitHub Actions setup

The **Release** workflow supports signing on tag builds and manual runs; PR CI
continues producing ad-hoc builds without signing secrets. Configure these six
repository secrets under **Settings → Secrets and variables → Actions**:

| Secret | Value |
| :--- | :--- |
| `SONY_MACOS_CERTIFICATE_BASE64` | Base64 of the Developer ID Application `.p12` export, including its private key. |
| `SONY_MACOS_CERTIFICATE_PASSWORD` | Password protecting that `.p12`. |
| `SONY_CODESIGN_IDENTITY` | Full identity, such as `Developer ID Application: Your Name (TEAMID)`. |
| `SONY_NOTARY_KEY_BASE64` | Base64 of a team App Store Connect API private key (`.p8`) with notary service access. |
| `SONY_NOTARY_KEY_ID` | API key ID. |
| `SONY_NOTARY_ISSUER_ID` | API key issuer ID. |

Then set the repository **variable** `SONY_MACOS_SIGNING_ENABLED` to `true`.
Only enable it on a repository whose release branches and tags are trusted;
restrict who can push release tags and dispatch workflows with these secrets.
The job uses an isolated temporary keychain and removes it and the imported
credential files even when packaging fails.

Run **Release → Run workflow** on the reviewed branch first. A manual run builds
packages without publishing a GitHub release. Download its DMG through a browser
onto a clean Mac, install it, and verify normal launch and Bluetooth permissions.
Keep the macOS launch issue open until a notarized release has been published and
the reported launch failure is resolved.

References: [Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution),
[signing nested code](https://developer.apple.com/documentation/xcode/creating-distribution-signed-code-for-the-mac/).

### Tarball
`cpack -G TGZ` still works but wraps the undeployed bundle; it is for developers, not users.
