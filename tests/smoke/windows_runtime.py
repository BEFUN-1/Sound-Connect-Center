"""Run command-line executables with and without the developer PATH."""
import ctypes
import os
from pathlib import Path
import subprocess
import sys


def main():
    # Prevent loader failures from opening a modal Windows dialog in CI.
    previous = ctypes.windll.kernel32.SetErrorMode(0x8003)
    try:
        for argument in sys.argv[1:]:
            executable = Path(argument).resolve()
            for mode in ("inherited", "clean"):
                environment = dict(os.environ)
                if mode == "clean":
                    environment["PATH"] = str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32")
                result = subprocess.run(
                    [str(executable), "--help"], env=environment,
                    capture_output=True, text=True, timeout=10,
                )
                assert result.returncode == 0, (
                    f"{executable.name} ({mode} PATH): loader/process exit "
                    f"0x{result.returncode & 0xffffffff:08x}; {result.stderr}"
                )
                assert "Usage:" in result.stdout, f"{executable.name}: no help output"
                print(f"PASS {executable.name}: {mode} PATH")
    finally:
        ctypes.windll.kernel32.SetErrorMode(previous)


if __name__ == "__main__":
    main()
