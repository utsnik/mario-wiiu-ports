#!/usr/bin/env python3
"""Host contract test for shadow-volume triangle extraction and transforms."""

from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).with_name("smg_wiiu_shadow_volume_merge_host.cpp")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="smg-shadow-volume-") as directory:
        executable = Path(directory) / "shadow_volume_merge_host"
        build = subprocess.run(
            ["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
             str(SOURCE), "-o", str(executable)], capture_output=True, text=True
        )
        if build.returncode:
            print("shadow-volume-merge: FAIL (compile)\n" + build.stderr)
            return 1
        run = subprocess.run([str(executable)], capture_output=True, text=True)
        print((run.stdout + run.stderr).strip())
        return run.returncode


if __name__ == "__main__":
    sys.exit(main())
