#!/usr/bin/env python3
"""Contract and host tests for the projected JPA final-vertex format."""

from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).with_name("smg_wiiu_jpa_prj_host.cpp")


def source_contracts() -> None:
    header = (ROOT / "platform/wiiu/jpa_final_vertex.h").read_text(encoding="utf-8")
    internal = (ROOT / "platform/gx/src/gx_internal.h").read_text(encoding="utf-8")
    fifo = (ROOT / "platform/gx/src/gx_fifo.cpp").read_text(encoding="utf-8")
    render = (ROOT / "platform/gx/src/gx_render.cpp").read_text(encoding="utf-8")
    renderer = (ROOT / "platform/wiiu/gx2_renderer.cpp").read_text(encoding="utf-8")
    jpa = (ROOT / "games/smg/decomp/src/JSystem/JParticle/JPAResource.cpp").read_text(encoding="utf-8")
    selftest = (ROOT / "platform/gx/tests/gx_selftest.cpp").read_text(encoding="utf-8")

    assert "SMS_WIIU_JPA_FINALVX_TEX0_STQ" in header
    assert "sms_wiiu_jpa_finalvx_pack_stq" in header
    assert "sms_wiiu_jpa_finalvx_projective_uv" in header
    assert "VF_TEX0_STQ" in internal
    assert "t == 0 && (fmt & VF_TEX0_STQ)" in fifo
    assert "hv.texQ" in fifo
    assert "VF_TEX0_STQ" in render
    assert "GX_TEX_STQ" in fifo
    assert "tex0Stq" in (ROOT / "platform/gx/src/gx_api.cpp").read_text(encoding="utf-8")
    assert "SMS_GXV3(GXTexCoord, f32)" in (ROOT / "platform/gx/include/sms_gx/gxvert_pc.h").read_text(encoding="utf-8")
    assert "GXTexCoord3f32" in (ROOT / "platform/gx/src/gx_vert.cpp").read_text(encoding="utf-8")
    assert "stqQuad" in selftest and "GX_TEX_STQ" in selftest

    assert renderer.count('s_jpaPrjMarker = sdMarkerPresent("jpaprj")') == 1
    gate_start = renderer.index("extern \"C\" int sms_wiiu_jpa_prj_enabled")
    gate = renderer[gate_start:renderer.index("static bool flushAndPublishJpaFinalVx", gate_start)]
    assert "jpaFinalVxGate()" in gate
    assert "s_jpaPrjMarker" in gate
    assert 'sdGameDefaultMarker("jpaprj")' not in renderer
    assert jpa.count("sms_wiiu_jpa_jpa_prj_enabled") == 0
    assert jpa.count("sms_wiiu_jpa_prj_enabled()") >= 4
    assert "work->mPrjType < 1 || work->mPrjType > 2" in jpa

    projected_start = jpa.index("static bool finalVxProjected")
    projected = jpa[projected_start:jpa.index("// Read-only counterpart", projected_start)]
    assert "sms_wiiu_jpa_prj_enabled()" in projected

    vat_start = jpa.index("static bool finalVxVatForRun")
    vat = jpa[vat_start:jpa.index("static SmsWiiuJpaFinalVxDesc* finalVxPayloadForRun", vat_start)]
    assert "texDesc != GX_DIRECT" in vat
    assert "texCnt != GX_TEX_STQ" in vat
    assert vat.index("if (projected)") < vat.index("else if (texDesc != GX_INDEX8")
    assert "finalVxProjectionMatrix" in jpa
    assert "GXTexCoord3f32" in jpa

    tev_pad = renderer[renderer.index("static void tevPadTexClause"):renderer.index("static bool initTevPixel")]
    assert "if (cf & 1) program[cf++] = 0;" in tev_pad
    assert "tevPadTexClause(program, cf);" in tev_pad
    compare_start = renderer.index("void jpaFinalVxSubmit")
    compare = renderer[compare_start:renderer.index("extern \"C\" int sms_wiiu_jpa_finalvx_enabled", compare_start)]
    for needle in ("SMS_WIIU_JPA_FINALVX_TEX0_STQ", "vertex-count", "index-count",
                   "vertex-bytes", "index-bytes", "memcmp(s_batch.vertices",
                   "memcmp(s_batch.indices"):
        assert needle in compare
    draw_start = renderer.index("static void drawJpaFinalVx")
    draw = renderer[draw_start:renderer.index("// GX clears the EFB", draw_start)]
    assert draw.count("gx2DlOpenForDraw()") >= 2

    # The old ST contract remains explicit and the new contract cannot be
    # silently accepted as an ordinary two-component TEX0.
    assert "SMS_WIIU_JPA_FINALVX_TEX0_STQ" in header
    assert "(packedFmt & SMS_WIIU_JPA_FINALVX_TEX0_STQ)" in header


def main() -> int:
    source_contracts()
    with tempfile.TemporaryDirectory(prefix="smg-jpa-prj-") as directory:
        executable = Path(directory) / "jpa_prj_host"
        build = subprocess.run(
            ["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
             str(SOURCE), "-o", str(executable)], capture_output=True, text=True
        )
        if build.returncode:
            print("jpa-prj: FAIL (compile)\n" + build.stderr)
            return 1
        run = subprocess.run([str(executable)], capture_output=True, text=True)
        print((run.stdout + run.stderr).strip())
        return run.returncode


if __name__ == "__main__":
    sys.exit(main())
