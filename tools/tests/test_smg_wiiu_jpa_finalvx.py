#!/usr/bin/env python3
"""Compile and exercise the pure JPA final-vertex contract on the host."""

from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).with_name("smg_wiiu_jpa_finalvx_host.cpp")


def source_contracts() -> None:
    renderer = (ROOT / "platform/wiiu/gx2_renderer.cpp").read_text(encoding="utf-8")
    profiler = (ROOT / "platform/wiiu/profiler.cpp").read_text(encoding="utf-8")
    counters = (ROOT / "platform/wiiu/jpa_counters.h").read_text(encoding="utf-8")
    jpa = (ROOT / "games/smg/decomp/src/JSystem/JParticle/JPAResource.cpp").read_text(encoding="utf-8")

    assert renderer.count('s_jpaFinalVxMarker = sdGameDefaultMarker("jpafinalvx")') == 1
    assert renderer.count('s_jpaFinalVxCheckMarker = sdMarkerPresent("jpafinalvxcheck")') == 1
    assert renderer.count('s_jpaFinalVxCopyMarker = sdMarkerPresent("jpafinalvxcopy")') == 1
    assert 'sdMarkerPresent("jpafinalvx")' not in renderer  # Galaxy default-on via sdGameDefaultMarker
    assert 'sdDefaultOnMarker("jpafinalvxcheck")' not in renderer
    gate_start = renderer.index("static bool jpaFinalVxGate()")
    gate = renderer[gate_start:renderer.index("static bool flushAndPublishJpaFinalVx", gate_start)]
    for needle in ("s_jpaFinalVxMarker", "s_gx2ProdEnabled", "s_compactVx", "s_gpuXfEnabled",
                   "s_ready", "s_jpaFinalVxCompactWarmed", "!s_pipe5Marker", "!s_pipe2Marker"):
        assert needle in gate, needle

    directional_start = jpa.index("static void drawDirectionalList")
    billboard_start = jpa.index("static void drawBillboardList")
    billboard_end = jpa.index("\n}\n#endif", billboard_start) + 2
    directional = jpa[directional_start:billboard_start]
    billboard = jpa[billboard_start:billboard_end]
    assert "finalVxPayloadForRun" in directional
    assert "sms_wiiu_jpa_finalvx_begin_check" in directional
    assert "sms_wiiu_jpa_finalvx_submit" in directional
    assert "finalVxBillboardPayloadForRun" in billboard
    assert "sms_wiiu_jpa_finalvx_begin_check" in billboard
    assert "sms_wiiu_jpa_finalvx_submit" in billboard
    assert "GXBegin(GX_QUADS" in billboard and "GXEnd();" in billboard

    draw_start = renderer.index("static void drawJpaFinalVx(const SmsWiiuJpaFinalVxDesc* desc) {")
    draw = renderer[draw_start:renderer.index("// GX clears the EFB", draw_start)]
    assert "decodeVertices" not in draw
    assert "s_batch.vertices.assign(desc->vertices" in draw
    assert "s_dlPending.ref" in draw
    for reason in ("WORKER_BADDESC", "WORKER_COLD", "WORKER_INACTIVE", "WORKER_DLOPEN", "WORKER_DLPENDING"):
        assert "SMS_WIIU_JPA_FINALVX_FALLBACK_" + reason in draw
    assert draw.index("DCFlushRange") < draw.index("GX2DrawIndexedEx")
    assert "GX2Invalidate(GX2_INVALIDATE_MODE_ATTRIBUTE_BUFFER" in draw
    assert draw.index("gx2DlOpenForDraw") < draw.index("prepareDrawState")

    for needle in ("runs=", "particles=", "vertices=", "bytes=", "fallback vat=",
                   "slot-full=", "slot-busy=", "record-full=", "dl-cache=", "check-split="):
        assert needle in profiler, needle
    for needle in ("worker-baddesc=", "worker-cold=", "worker-inactive=", "worker-dlopen=", "worker-dlpending="):
        assert needle in profiler, needle
    assert "[jpafinalvx] check runs=%u mismatches=%u first=%s" in profiler
    assert "sms_wiiu_jpa_finalvx_report();" in profiler
    assert "SMS_WIIU_JPA_FINALVX_FALLBACK_REASON_COUNT" in counters


def main() -> int:
    source_contracts()
    with tempfile.TemporaryDirectory(prefix="smg-jpa-finalvx-") as directory:
        executable = Path(directory) / "jpa_finalvx_host"
        build = subprocess.run(
            ["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
             str(SOURCE), "-o", str(executable)], capture_output=True, text=True
        )
        if build.returncode:
            print("jpa-finalvx: FAIL (compile)\n" + build.stderr)
            return 1
        run = subprocess.run([str(executable)], capture_output=True, text=True)
        print((run.stdout + run.stderr).strip())
        return run.returncode


if __name__ == "__main__":
    sys.exit(main())
