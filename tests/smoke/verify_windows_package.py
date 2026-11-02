"""Compare portable release binaries with the installer payload and build."""
import hashlib
from pathlib import Path
import subprocess
import sys
import zipfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


archive, payload, build = (Path(value).resolve() for value in sys.argv[1:])
required = (
    "bin/sony-device-center.exe", "bin/sonyctl.exe", "bin/sonyd.exe",
    "bin/libstdc++-6.dll", "bin/libgcc_s_seh-1.dll", "bin/libwinpthread-1.dll",
    "bin/Qt6Core.dll", "bin/Qt6Gui.dll", "bin/Qt6Qml.dll", "bin/Qt6Quick.dll",
    "bin/Qt6QuickControls2.dll", "bin/platforms/qwindows.dll",
    "bin/Qt6QuickControls2Basic.dll", "bin/Qt6QuickTemplates2.dll", "bin/qt.conf",
    "bin/opengl32sw.dll", "bin/D3Dcompiler_47.dll", "bin/dxcompiler.dll", "bin/dxil.dll",
    "bin/qml/QtQml/qmldir", "bin/qml/QtQuick/qmldir", "bin/qml/QtQuick/Controls/qmldir",
)
destination = build / (archive.stem + "-verify")
with zipfile.ZipFile(archive) as package:
    assert package.testzip() is None, "Portable archive CRC failure"
    candidates = [name for name in package.namelist() if name.endswith("/bin/sony-device-center.exe")]
    assert len(candidates) == 1, "Portable archive has no unique application root"
    prefix = candidates[0][:-len("bin/sony-device-center.exe")]
    for relative in required:
        data = package.read(prefix + relative)
        assert hashlib.sha256(data).hexdigest() == digest(payload / relative), relative
    payload_files = {path.relative_to(payload).as_posix(): path
                     for path in payload.rglob("*") if path.is_file()}
    archive_files = {name[len(prefix):] for name in package.namelist()
                     if name.startswith(prefix) and not name.endswith("/")}
    assert archive_files == payload_files.keys(), "Portable and installer file inventories differ"
    for relative, path in payload_files.items():
        assert hashlib.sha256(package.read(prefix + relative)).hexdigest() == digest(path), relative
    for name in ("sony-device-center", "sonyctl", "sonyd"):
        source = build / "apps" / ("device-center" if name == "sony-device-center" else name) / f"{name}.exe"
        assert digest(source) == digest(payload / "bin" / f"{name}.exe"), f"Stale {name}.exe"
    for name in package.namelist():
        assert (destination / name).resolve().is_relative_to(destination.resolve()), "Invalid archive path"
    package.extractall(destination)

portable_bin = destination / prefix / "bin"
checker = Path(__file__).with_name("windows_runtime.py")
for root in (payload / "bin", portable_bin):
    subprocess.run(
        [sys.executable, str(checker), str(root / "sonyctl.exe"), str(root / "sonyd.exe")],
        check=True,
    )
print("PASS ZIP CRC, required Qt/runtime files, build/payload hashes, extracted CLI/daemon startup")
