#!/usr/bin/env python3
"""Compile and run the independent pipe5 overlap state-machine model."""

from pathlib import Path
import subprocess
import sys
import tempfile


SOURCE = Path(__file__).with_name("smg_wiiu_pipe5_overlap_host.cpp")


def main() -> int:
	with tempfile.TemporaryDirectory(prefix="smg-pipe5-overlap-") as directory:
		executable = Path(directory) / "pipe5_overlap_host"
		build = subprocess.run(
			["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", str(SOURCE), "-o", str(executable)],
			capture_output=True,
			text=True,
		)
		if build.returncode:
			print("pipe5-overlap: FAIL (compile)\n" + build.stderr)
			return 1
		run = subprocess.run([str(executable)], capture_output=True, text=True)
		print((run.stdout + run.stderr).strip())
		return run.returncode


if __name__ == "__main__":
	sys.exit(main())
