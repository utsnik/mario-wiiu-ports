#!/usr/bin/env python3
"""Compile and exercise the overflow-safe EFB copy rectangle clipping."""

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HARNESS = r"""
#include <cassert>
#include <cstdint>
#include "platform/wiiu/imgfx_copy.h"

using sms_wiiu_imgfx::CopyRect;
using sms_wiiu_imgfx::clipCopyRect;

static void check(const CopyRect& r, bool valid, bool clipped,
                  unsigned x, unsigned y, unsigned w, unsigned h) {
    assert(r.valid == valid && r.clipped == clipped);
    assert(r.x == x && r.y == y && r.w == w && r.h == h);
}

int main() {
    check(clipCopyRect(8, 12, 32, 24, 640, 528), true, false, 8, 12, 32, 24);
    check(clipCopyRect(620, 500, 32, 40, 640, 528), true, true, 620, 500, 20, 28);

    // This is the minishot failure shape: the scaled origin is below the
    // target, so target - origin must never be evaluated as an unsigned size.
    check(clipCopyRect(1280, 720, 128, 64, 1280, 720), false, true, 1280, 720, 0, 0);
    check(clipCopyRect(1279, 719, 128, 64, 1280, 720), true, true, 1279, 719, 1, 1);
}
"""


def test_imgfx_copy_bounds() -> None:
    with tempfile.TemporaryDirectory(prefix="smg-imgfx-copy-") as directory:
        source = Path(directory) / "imgfx_copy_bounds_host.cpp"
        binary = Path(directory) / "imgfx_copy_bounds_host"
        source.write_text(HARNESS, encoding="utf-8")
        subprocess.run(
            ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
             str(source), "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    test_imgfx_copy_bounds()
    print("imgfx-copy-bounds: PASS")
