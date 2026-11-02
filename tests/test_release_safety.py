"""Regression checks for release provenance and the per-user installer boundary."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("release_source", REPO / "packaging/verify-release-source.py")
release_source = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release_source)


class ReleaseSourceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.repo = Path(self.directory.name)
        self.git("init", "-q")
        self.git("config", "user.email", "test@example.invalid")
        self.git("config", "user.name", "Release test")
        (self.repo / "CMakeLists.txt").write_text("project(SonyDeviceCenter VERSION 2.0.4)\n")
        self.git("add", ".")
        self.git("commit", "-qm", "Create release fixture")

    def git(self, *args):
        return subprocess.check_output(
            ["git", "-C", str(self.repo), *args], text=True, stderr=subprocess.DEVNULL
        ).strip()

    def test_clean_tag_records_exact_commit(self):
        self.git("tag", "v2.0.4")
        record = release_source.verify(self.repo)
        self.assertEqual(record["commit"], self.git("rev-parse", "HEAD"))
        self.assertEqual(record["tag"], "v2.0.4")

    def test_missing_tag_rejected(self):
        with self.assertRaises(subprocess.CalledProcessError):
            release_source.verify(self.repo)

    def test_tag_at_another_commit_rejected(self):
        self.git("tag", "v2.0.4")
        (self.repo / "README.md").write_text("New change\n")
        self.git("add", ".")
        self.git("commit", "-qm", "Move HEAD")
        with self.assertRaisesRegex(RuntimeError, "HEAD must match"):
            release_source.verify(self.repo)

    def test_dirty_tracked_source_rejected(self):
        self.git("tag", "v2.0.4")
        (self.repo / "CMakeLists.txt").write_text("project(SonyDeviceCenter VERSION 2.0.5)\n")
        with self.assertRaisesRegex(RuntimeError, "committed and clean"):
            release_source.verify(self.repo)

    def test_untracked_source_rejected(self):
        (self.repo / "uncommitted.cpp").write_text("// pending\n")
        with self.assertRaisesRegex(RuntimeError, "committed and clean"):
            release_source.verify(self.repo, require_tag=False)

    def test_manual_ci_preview_records_no_release_tag(self):
        self.assertIsNone(release_source.verify(self.repo, require_tag=False)["tag"])


class InstallerSafetyTests(unittest.TestCase):
    def test_no_elevated_or_directory_controlled_execution(self):
        source = (REPO / "packaging/windows/qt-installer.nsi").read_text(encoding="utf-8")
        self.assertIn("RequestExecutionLevel user", source)
        self.assertNotIn("RequestExecutionLevel highest", source)
        self.assertNotIn("InstallDirRegKey", source)
        self.assertNotIn("ExecWait", source)
        self.assertNotIn("taskkill", source)
        self.assertNotIn("RMDir /r", source)
        self.assertIn('!define MUI_FINISHPAGE_RUN_NOTCHECKED', source)
        for prefix in ("", "un."):
            self.assertIn(f"Call {prefix}RequireUserToken", source)
            self.assertIn(f"Call {prefix}ValidateInstallDir", source)

    @unittest.skipUnless(os.name == "nt" and os.environ.get("SONY_TEST_INSTALLER"),
                         "Set SONY_TEST_INSTALLER to test the compiled NSIS rejection path")
    def test_compiled_installer_rejects_custom_directory(self):
        installer = Path(os.environ["SONY_TEST_INSTALLER"]).resolve()
        self.assertTrue(installer.is_file())
        # Test only a rejected /D path: it must exit before changing files or HKCU.
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "refused-install"
            result = subprocess.run([str(installer), "/S", f"/D={target}"], timeout=15)
            self.assertIn(result.returncode, (2, 740))
            self.assertFalse(target.exists())
        self.assertIn(b'level="asInvoker"', installer.read_bytes())

    @unittest.skipUnless(os.name == "nt" and os.environ.get("SONY_TEST_NSIS"),
                         "Set SONY_TEST_NSIS to compile and run the actual directory guards")
    def test_compiled_guards_accept_only_the_allowed_real_directory(self):
        compiler = os.environ["SONY_TEST_NSIS"]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            allowed = root / "allowed"
            allowed.mkdir()
            (allowed / "existing.txt").write_text("preserve")
            source = (REPO / "packaging/windows/qt-installer.nsi").read_text(encoding="utf-8")
            # Reuse production guards, redirect the whitelist to the disposable
            # fixture, and replace install sections with a no-write exit.
            source = source[:source.index('Section "Sony Device Center" Main')]
            source += 'Section\nSetErrorLevel 0\nQuit\nSectionEnd\n'
            source = source.replace("$LOCALAPPDATA\\Programs\\Sony Device Center", str(allowed))
            script = root / "guards.nsi"
            script.write_text(source, encoding="utf-8")
            executable = root / "guards.exe"
            subprocess.run([
                compiler, "/V1", "/INPUTCHARSET", "UTF8",
                f"/DPAYLOAD_DIR={root}", f"/DOUTPUT_FILE={executable}",
                f"/DREPO_DIR={REPO}", "/DVERSION=0.0.0",
                f"/DUNINSTALL_MANIFEST={root / 'unused.nsh'}", str(script),
            ], check=True, capture_output=True, timeout=30)
            def run(*args):
                return subprocess.run([str(executable), "/S", *args], timeout=10).returncode
            result = run()
            if result == 740:
                self.skipTest("Elevated runner correctly refused; positive directory checks require a normal token")
            self.assertEqual(result, 0)
            self.assertEqual(run(f"/D={root / 'outside'}"), 2)
            outside = root / "preserved"
            outside.mkdir()
            sentinel = outside / "sentinel.txt"
            sentinel.write_text("preserve")
            junction = allowed / "junction"
            subprocess.run(
                ["cmd", "/c", "mklink", "/J", str(junction), str(outside)],
                check=True, capture_output=True,
            )
            try:
                self.assertEqual(run(), 2)
                self.assertEqual(sentinel.read_text(), "preserve")
            finally:
                junction.rmdir()
            self.assertEqual((allowed / "existing.txt").read_text(), "preserve")


if __name__ == "__main__":
    unittest.main()
