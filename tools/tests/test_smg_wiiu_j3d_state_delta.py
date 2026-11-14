#!/usr/bin/env python3
"""Host contract for the SMG J3D state-delta record and report ABI."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "platform/gx/src/j3d_state_delta.h"
FIFO = ROOT / "platform/gx/src/gx_fifo.cpp"
INTERNAL = ROOT / "platform/gx/src/gx_internal.h"
GPHASE = ROOT / "games/smg/platform/wiiu/gphase.cpp"
RENDERER = ROOT / "platform/wiiu/gx2_renderer.cpp"
HARNESS = ROOT / "tools/tests/smg_wiiu_j3d_state_delta_host.cpp"


def fail(msg):
    print(f"j3d-state-delta: FAIL: {msg}")
    return 1


def main():
    required = ["J3DStateDeltaOp", "j3dStateDeltaCanSkip", "j3dStateDeltaDirtyForBP",
                "J3D_DIRTY_TEV_TEXTURE", "J3D_DIRTY_CP_LAYOUT_ARRAY"]
    text = HEADER.read_text()
    for item in required:
        if item not in text:
            return fail(f"missing shared contract {item}")
    for marker in ("j3dstatedelta", "j3dstatedeltacheck"):
        if marker not in (FIFO.read_text() + GPHASE.read_text() + RENDERER.read_text()):
            return fail(f"missing marker name {marker}")
    for label in ("materialLists", "stateOps", "cpSame", "cpDiff", "bpSame", "bpDiff",
                  "xfSame", "xfDiff", "stateFlush", "draws"):
        if label not in (FIFO.read_text() + GPHASE.read_text()):
            return fail(f"missing counter label {label}")
    if "sms_wiiu_j3d_state_delta_take(uint32_t* values, uint32_t valueCount)" not in INTERNAL.read_text():
        # This is deliberately the first source-contract failure until Task 4 changes the ABI.
        print("j3d-state-delta: FAIL: missing bounded snapshot signature")
        return 1
    fifo = FIFO.read_text()
    renderer = RENDERER.read_text()
    if "if (s_j3dStateDeltaEnabled) e.stateDeltaOps.push_back" not in fifo:
        return fail("marker-off record construction bypass is missing")
    if "j3dStateDeltaPendingDirtyDomains() == 0" not in renderer:
        return fail("flush dirty-domain gate is missing")
    if "valueCount < J3D_STATE_DELTA_REPORT_COUNT" not in fifo:
        return fail("bounded snapshot clamp is missing")
    for name in ("J3D_DIRTY_TEV_TEXTURE", "J3D_DIRTY_PE_RASTER",
                 "J3D_DIRTY_VERTEX_KEY_XF_REG", "J3D_DIRTY_XF_MEMORY_UNIFORM",
                 "J3D_DIRTY_CP_LAYOUT_ARRAY"):
        if name not in text:
            return fail(f"missing dirty domain {name}")
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "state-delta-host"
        build = subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                                "-I", str(HEADER.parent), str(HARNESS), "-o", str(exe)],
                               capture_output=True, text=True)
        if build.returncode:
            return fail(build.stderr.strip() or "host harness compile failed")
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        if run.returncode:
            return fail(run.stderr.strip() or "host harness failed")
    print("j3d-state-delta: PASS: shared record/domain contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
