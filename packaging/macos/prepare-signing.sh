#!/usr/bin/env bash
# Import release credentials into an isolated keychain on a GitHub macOS runner.
# release.yml removes the keychain and credential files even if this step fails.
set -euo pipefail

for name in RUNNER_TEMP GITHUB_ENV SONY_MACOS_CERTIFICATE_BASE64 \
    SONY_MACOS_CERTIFICATE_PASSWORD SONY_CODESIGN_IDENTITY \
    SONY_NOTARY_KEY_BASE64 SONY_NOTARY_KEY_ID SONY_NOTARY_ISSUER_ID; do
    if [ -z "${!name:-}" ]; then
        echo "::error::Signing enabled but $name is missing" >&2
        exit 1
    fi
done
case "$SONY_CODESIGN_IDENTITY" in
    "Developer ID Application: "*) ;;
    *) echo "::error::Use a Developer ID Application signing identity" >&2; exit 1 ;;
esac

umask 077
keychain="$RUNNER_TEMP/sony-macos-signing.keychain-db"
certificate="$RUNNER_TEMP/sony-macos-signing.p12"
notary_key="$RUNNER_TEMP/sony-macos-notary.p8"
python3 - "$certificate" "$notary_key" <<'PY'
import base64
import os
import pathlib
import sys
for variable, destination in zip(
    ("SONY_MACOS_CERTIFICATE_BASE64", "SONY_NOTARY_KEY_BASE64"), sys.argv[1:]
):
    # Accept base64 wrapped by the platform's base64 utility.
    data = "".join(os.environ[variable].split())
    pathlib.Path(destination).write_bytes(base64.b64decode(data, validate=True))
PY

password=$(openssl rand -hex 32)
echo "::add-mask::$password"
security create-keychain -p "$password" "$keychain"
security set-keychain-settings -lut 21600 "$keychain"
security unlock-keychain -p "$password" "$keychain"
security import "$certificate" -P "$SONY_MACOS_CERTIFICATE_PASSWORD" \
    -k "$keychain" -T /usr/bin/codesign
security set-key-partition-list -S apple-tool:,apple:,codesign: \
    -s -k "$password" "$keychain"

# Use a team App Store Connect API key with access to the notary service.
xcrun notarytool store-credentials sony-release --keychain "$keychain" \
    --key "$notary_key" --key-id "$SONY_NOTARY_KEY_ID" --issuer "$SONY_NOTARY_ISSUER_ID"

{
    echo "SONY_CODESIGN_IDENTITY=$SONY_CODESIGN_IDENTITY"
    echo "SONY_SIGNING_KEYCHAIN=$keychain"
    echo "SONY_NOTARY_PROFILE=sony-release"
} >> "$GITHUB_ENV"
