#!/usr/bin/env python3
"""Run the projected producer stream through the real GX FIFO parser."""

from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).with_name("smg_wiiu_jpa_stream_host.cpp")


def projected_directional_colour_contract() -> bool:
    """A projected directional run without raster colours must clear stale VCD colours."""
    source = (ROOT / "games/smg/decomp/src/JSystem/JParticle/JPAResource.cpp").read_text()
    start = source.index("static void drawDirectionalList(")
    end = source.index("static GXColor billboardColor(", start)
    directional = source[start:end]
    projected = directional[directional.index("if (projected) {"):directional.index("} else if (rasterMode != NULL) {")]
    safe = all(f"GXSetVtxDesc(GX_VA_CLR{slot}, GX_NONE)" in projected for slot in (0, 1))
    print("jpa-stream: " + ("PASS" if safe else "FAIL") +
          " projected directional run clears inherited colour descriptors")
    return safe


def main() -> int:
    contract_ok = projected_directional_colour_contract()
    with tempfile.TemporaryDirectory(prefix="smg-jpa-stream-") as directory:
        executable = Path(directory) / "jpa_stream_host"
        command = [
            "g++", "-std=gnu++11", "-Wall", "-Wextra",
            "-D__WIIU__", "-DPORT_GAME_SMG", "-DSMS_GX_HOST_STREAM_TEST",
            "-I", str(ROOT / "platform/gx/src"),
            "-I", str(ROOT / "platform/gx/include"),
            "-I", str(ROOT / "platform/gx/port_include"),
            "-I", str(ROOT / "decomp/include"),
            "-I", str(ROOT / "decomp/libs/dolphin/include"),
            "-I", str(ROOT), "-I", str(ROOT / "platform"),
            "-I", str(ROOT / "games/smg/include"), "-I", str(ROOT / "src"),
            "-ffunction-sections", "-fdata-sections", str(SOURCE),
            str(ROOT / "platform/gx/src/gx_api.cpp"),
            str(ROOT / "platform/gx/src/gx_fifo.cpp"),
            str(ROOT / "platform/gx/src/gx_vert.cpp"),
            "-Wl,--gc-sections", "-o", str(executable),
        ]
        build = subprocess.run(command, capture_output=True, text=True)
        if build.returncode:
            print("jpa-stream: FAIL (compile)\n" + build.stderr)
            return 1
        failed = False
        for case in ("legacy", "particle"):
            run = subprocess.run([str(executable), case], capture_output=True, text=True)
            print((run.stdout + run.stderr).strip())
            failed |= run.returncode != 0
        return int(failed or not contract_ok)


if __name__ == "__main__":
    sys.exit(main())
