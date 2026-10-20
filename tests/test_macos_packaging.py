"""Exercise release failure paths without Apple credentials or a macOS host.

Native signing, notarization and Gatekeeper still need a signed macOS CI run.
Run with: python3 -m unittest discover -s tests -p 'test_macos_packaging.py' -v
"""
import base64
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
MOCK = r'''#!/usr/bin/env python3
import json, os, pathlib, shutil, sys
tool = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ["MOCK_LOG"], "a") as log:
    log.write(json.dumps([tool, *args]) + "\n")
if tool == "file":
    print("Mach-O 64-bit executable")
elif tool == "dmgbuild":
    pathlib.Path(args[-1]).touch()
elif tool == "codesign":
    if os.environ.get("MOCK_CODESIGN_FAIL") == "true":
        sys.exit(1)
elif tool == "xcrun":
    if args[:2] == ["notarytool", "submit"]:
        print(json.dumps({"id": "test-submission", "status": os.environ.get("MOCK_NOTARY_STATUS", "Accepted")}))
    elif args[:2] == ["stapler", "validate"]:
        sys.exit(int(os.environ.get("MOCK_STAPLER_EXIT", "0")))
elif tool == "hdiutil" and args[0] == "attach":
    destination = pathlib.Path(args[args.index("-mountpoint") + 1])
    shutil.copytree(os.environ["MOCK_STAGING"], destination, dirs_exist_ok=True, symlinks=True)
elif tool == "hdiutil" and args[0] == "detach":
    shutil.rmtree(args[1])
elif tool == "spctl":
    sys.exit(int(os.environ.get("MOCK_GATEKEEPER_EXIT", "0")))
'''


class MacPackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sony packaging ")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.bin = self.directory / "bin"
        self.bin.mkdir()
        for tool in ("macdeployqt", "dmgbuild", "codesign", "file", "xcrun", "hdiutil", "spctl", "security"):
            path = self.bin / tool
            path.write_text(MOCK)
            path.chmod(0o755)
        self.build = self.directory / "build"
        for name in ("device-center/sony-device-center.app/Contents/MacOS/sony-device-center", "sonyd/sonyd", "sonyctl/sonyctl"):
            path = self.build / "apps" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        self.log = self.directory / "calls.jsonl"
        self.env = {key: value for key, value in os.environ.items() if not key.startswith("SONY_")}
        self.env.update(PATH=f"{self.bin}:{os.environ['PATH']}", MOCK_LOG=str(self.log))

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def run_script(self, script, *args):
        return subprocess.run(["bash", str(ROOT / "packaging/macos" / script), *map(str, args)],
                              env=self.env, capture_output=True, text=True)

    def signed(self):
        self.env.update(SONY_CODESIGN_IDENTITY="Developer ID Application: Test (TEAM)",
                        SONY_NOTARY_PROFILE="test-profile", SONY_REQUIRE_NOTARIZATION="true")

    def test_ad_hoc_build_does_not_submit_to_apple(self):
        result = self.run_script("build-dmg.sh", self.build)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Ad-hoc build", result.stdout)
        self.assertFalse(any(call[0] == "xcrun" for call in self.calls()))

    def test_required_notarization_rejects_missing_credentials_before_build(self):
        self.env["SONY_REQUIRE_NOTARIZATION"] = "true"
        self.assertNotEqual(self.run_script("build-dmg.sh", self.build).returncode, 0)
        self.assertEqual(self.calls(), [])

    def test_profile_without_signing_identity_is_rejected(self):
        self.env["SONY_NOTARY_PROFILE"] = "test-profile"
        self.assertNotEqual(self.run_script("build-dmg.sh", self.build).returncode, 0)
        self.assertEqual(self.calls(), [])

    def test_rejected_notarization_is_not_stapled(self):
        self.signed()
        self.env["MOCK_NOTARY_STATUS"] = "Invalid"
        result = self.run_script("build-dmg.sh", self.build)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not Accepted", result.stderr)
        self.assertFalse(any(call[:2] == ["xcrun", "stapler"] for call in self.calls()))

    def test_accepted_submission_is_stapled_after_signing(self):
        self.signed()
        self.env["SONY_SIGNING_KEYCHAIN"] = str(self.directory / "temporary.keychain-db")
        result = self.run_script("build-dmg.sh", self.build)
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        signatures = [call for call in calls if call[0] == "codesign" and "--sign" in call]
        self.assertEqual(len(signatures), 5)  # three binaries, app, DMG
        self.assertTrue(all("--timestamp" in call and "--keychain" in call for call in signatures))
        self.assertTrue(all("--deep" not in call for call in signatures))
        self.assertEqual(sum("--entitlements" in call for call in signatures), 1)
        self.assertTrue(signatures[-2][-1].endswith(".app"))
        self.assertTrue(signatures[-1][-1].endswith(".dmg"))
        self.assertEqual([call[1:3] for call in calls if call[0] == "xcrun"],
                         [["notarytool", "submit"], ["stapler", "staple"], ["stapler", "validate"]])

    def test_signing_failure_stops_before_notarization(self):
        self.signed()
        self.env["MOCK_CODESIGN_FAIL"] = "true"
        self.assertNotEqual(self.run_script("build-dmg.sh", self.build).returncode, 0)
        self.assertFalse(any(call[0] == "xcrun" for call in self.calls()))

    def prepare_verification(self):
        staging = self.directory / "staging"
        app = staging / "Sony Device Center.app"
        for name in ("MacOS/sony-device-center", "MacOS/sonyd", "MacOS/sonyctl",
                     "Frameworks/QtCore.framework", "Frameworks/QtQuick.framework",
                     "Frameworks/QtQuickControls2.framework", "Resources/qml/QtQuick/Controls",
                     "Resources/qml/QtQuick/Layouts", "Resources/qml/QtQuick/Shapes",
                     "PlugIns/platforms/libqcocoa.dylib", "Resources/AppIcon.icns"):
            path = app / "Contents" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        (staging / "Applications").symlink_to("/Applications")
        self.env.update(MOCK_STAGING=str(staging), SONY_REQUIRE_NOTARIZATION="true")

    def test_verification_rejects_missing_notarization_ticket(self):
        self.prepare_verification()
        self.env["MOCK_STAPLER_EXIT"] = "1"
        result = self.run_script("verify-dmg.sh", "test.dmg")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no valid stapled", result.stderr)
        self.assertFalse(any(call[0] == "spctl" for call in self.calls()))

    def test_verification_rejects_gatekeeper_failure_despite_valid_signature(self):
        self.prepare_verification()
        self.env["MOCK_GATEKEEPER_EXIT"] = "1"
        result = self.run_script("verify-dmg.sh", "test.dmg")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Gatekeeper rejected", result.stderr)

    def test_signing_setup_fails_when_secrets_are_missing(self):
        result = self.run_script("prepare-signing.sh")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("is missing", result.stderr)
        self.assertEqual(self.calls(), [])

    def setup_credentials(self):
        self.env.update(RUNNER_TEMP=str(self.directory), GITHUB_ENV=str(self.directory / "github-env"),
                        SONY_MACOS_CERTIFICATE_BASE64=base64.b64encode(b"test-certificate").decode(),
                        SONY_MACOS_CERTIFICATE_PASSWORD="test-password",
                        SONY_CODESIGN_IDENTITY="Developer ID Application: Test (TEAM)",
                        SONY_NOTARY_KEY_BASE64=base64.b64encode(b"test-api-key").decode(),
                        SONY_NOTARY_KEY_ID="TESTKEY", SONY_NOTARY_ISSUER_ID="TESTISSUER")

    def test_setup_exports_custom_keychain_and_keeps_credentials_private(self):
        self.setup_credentials()
        result = self.run_script("prepare-signing.sh")
        self.assertEqual(result.returncode, 0, result.stderr)
        exported = (self.directory / "github-env").read_text()
        self.assertIn("SONY_NOTARY_PROFILE=sony-release", exported)
        self.assertIn(f"SONY_SIGNING_KEYCHAIN={self.directory}/sony-macos-signing.keychain-db", exported)
        for filename, content in (("sony-macos-signing.p12", b"test-certificate"),
                                  ("sony-macos-notary.p8", b"test-api-key")):
            path = self.directory / filename
            self.assertEqual(path.read_bytes(), content)
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    def test_setup_rejects_invalid_base64_before_import(self):
        self.setup_credentials()
        self.env["SONY_MACOS_CERTIFICATE_BASE64"] = "not base64!"
        self.assertNotEqual(self.run_script("prepare-signing.sh").returncode, 0)
        self.assertEqual(self.calls(), [])
