"""Fail release packaging unless the source is clean and at its version tag."""
import argparse
import json
from pathlib import Path
import re
import subprocess


def verify(repo, require_tag=True):
    def git(*args):
        return subprocess.check_output(
            ["git", "-C", str(repo), *args], text=True, encoding="utf-8"
        ).strip()

    version = re.search(
        r"project\(SonyDeviceCenter VERSION (\d+\.\d+\.\d+)",
        (repo / "CMakeLists.txt").read_text(encoding="utf-8"),
    ).group(1)
    if git("status", "--porcelain", "--untracked-files=all"):
        raise RuntimeError("Release source must be committed and clean, including untracked files")
    commit = git("rev-parse", "HEAD")
    tag = "v" + version
    if require_tag and git("rev-parse", "--verify", "refs/tags/" + tag + "^{commit}") != commit:
        raise RuntimeError(f"HEAD must match the committed release tag {tag}")
    return {"version": version, "commit": commit, "tag": tag if require_tag else None}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--allow-untagged", action="store_true", help="CI preview only")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        record = verify(args.repo.resolve(), require_tag=not args.allow_untagged)
    except (RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Release source rejected: {error}\n")
    data = json.dumps(record, indent=2) + "\n"
    if args.output:
        args.output.write_text(data, encoding="utf-8")
    print(data, end="")
