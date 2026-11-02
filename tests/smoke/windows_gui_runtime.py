"""Exercise the actual Qt pages with fake headphones and only the staged runtime."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

payload, test_binary, test_qt, destination = (Path(value).resolve() for value in sys.argv[1:])
assert not destination.exists(), "Use a fresh deployment directory"
shutil.copytree(payload, destination)
binary = destination / "bin" / test_binary.name
shutil.copy2(test_binary, binary)
shutil.copy2(test_qt, binary.parent / test_qt.name)
assert (binary.parent / "qt.conf").is_file(), "Qt paths must resolve from the deployment"
environment = dict(os.environ)
environment["PATH"] = str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32")
for key in ("QT_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH",
            "SONY_SOAK_SECONDS", "SONY_UI_SCENARIO", "QT_QUICK_CONTROLS_STYLE"):
    environment.pop(key, None)
environment["SONY_UI_INSTANCE"] = "-deployment-check"
environment["QT_QPA_PLATFORM"] = "windows"
renderers = os.environ.get("SONY_TEST_RENDERERS", "software,default").split(",")
for renderer in renderers:
    environment["SONY_UI_INSTANCE"] = "-deployment-" + renderer
    environment.pop("QT_QUICK_BACKEND", None)
    environment.pop("QSG_RHI_BACKEND", None)
    if renderer == "software":
        environment["QT_QUICK_BACKEND"] = "software"
    elif renderer != "default":
        environment["QSG_RHI_BACKEND"] = renderer
    try:
        result = subprocess.run([str(binary)], cwd=binary.parent, env=environment,
                                capture_output=True, text=True, timeout=90)
    except subprocess.TimeoutExpired as error:
        def decoded(value):
            return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value or ""
        evidence = decoded(error.stdout) + decoded(error.stderr)
        (destination / f"gui-{renderer}.txt").write_text(evidence, encoding="utf-8")
        raise
    evidence = result.stdout + result.stderr
    (destination / f"gui-{renderer}.txt").write_text(evidence, encoding="utf-8")
    assert result.returncode == 0, evidence
    assert "0 failed" in evidence, evidence
    print(f"PASS staged GUI: clean PATH, {renderer}, pages/tray/controls/languages")
