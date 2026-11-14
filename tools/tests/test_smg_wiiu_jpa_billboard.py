#!/usr/bin/env python3
"""Compile and exercise the pure JPA billboard packing contract on the host."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).with_name("smg_wiiu_jpa_billboard_host.cpp")
INCLUDE = ROOT / "games/smg/decomp/src/JSystem/JParticle"


def billboard_source() -> str:
    source = (ROOT / "games/smg/decomp/src/JSystem/JParticle/JPAResource.cpp").read_text(
        encoding="utf-8"
    )
    start = source.index("static void drawBillboardList")
    end = source.index("\n}\n#endif", start) + 2
    return source[start:end]


def test_billboard_marker_off_uses_legacy_loop() -> None:
    billboard = billboard_source()
    gate = "if (!noEmit && sms_wiiu_jpa_finalvx_enabled())"
    gate_start = billboard.index(gate)
    gate_end = billboard.index("\n            if (!noEmit && !finalSuppress)", gate_start)
    decision = billboard[gate_start:gate_end]
    assert "finalVxBillboardPayloadForRun" in decision
    assert "sms_wiiu_jpa_finalvx_begin_check" in decision
    assert "sms_wiiu_jpa_finalvx_submit" in decision
    assert "sms_wiiu_jpa_finalvx_alloc(" not in billboard
    assert "GXBegin(GX_QUADS" in billboard
    assert "GXEnd();" in billboard
    for operation in ("GXPosition3f32", "GXColor4u8", "GXTexCoord1x8"):
        assert operation in billboard


def test_billboard_check_emits_once() -> None:
    billboard = billboard_source()
    begin = billboard.index("sms_wiiu_jpa_finalvx_begin_check")
    gx_begin = billboard.index("GXBegin(GX_QUADS", begin)
    gx_end = billboard.index("GXEnd();", gx_begin)
    submit = billboard.index("sms_wiiu_jpa_finalvx_submit", gx_end)
    assert begin < gx_begin < gx_end < submit
    assert "if (finalSuppress)" in billboard
    assert "if (!noEmit && !finalSuppress)" in billboard


def test_billboard_visible_count_and_reverse_order() -> None:
    source = (ROOT / "games/smg/decomp/src/JSystem/JParticle/JPAResource.cpp").read_text(
        encoding="utf-8"
    )
    start = source.index("static SmsWiiuJpaFinalVxDesc* finalVxBillboardPayloadForRun")
    end = source.index("static void drawDirectionalList", start)
    payload = source[start:end]
    assert "JPANode< JPABaseParticle >* runEnd" in payload
    assert "visible * 4u" in payload
    assert "if (particle->checkStatus(8)) continue;" in payload
    assert "particleIndex * 4u" in payload
    assert "reverse ? scan->getPrev() : scan->getNext()" in payload


def test_billboard_colour_helpers_are_read_only() -> None:
    source = (ROOT / "games/smg/decomp/src/JSystem/JParticle/JPAResource.cpp").read_text(
        encoding="utf-8"
    )
    for name in ("billboardColor", "billboardPrmColor", "billboardEnvColor"):
        signature = "static GXColor " + name
        declaration = source.index(signature)
        start = source.index(signature, declaration + len(signature))
        end = source.index("\n}", start) + 2
        body = source[start:end]
        assert not re.search(r"(?:particle|emitter)->[^;\n]*=", body)
        assert "(*" not in body
    payload_start = source.index("static SmsWiiuJpaFinalVxDesc* finalVxBillboardPayloadForRun")
    payload_end = source.index("static void drawDirectionalList", payload_start)
    payload = source[payload_start:payload_end]
    assert "noteParticle" not in payload
    assert "funcs[" not in payload
    assert "(*func" not in payload
    assert "mBaseAxis" not in payload


def test_parent_child_billboard_arguments() -> None:
    source = (ROOT / "games/smg/decomp/src/JSystem/JParticle/JPAResource.cpp").read_text(
        encoding="utf-8"
    )
    parent = source[source.index("void JPAResource::drawP"):source.index("void JPAResource::drawC")]
    child = source[source.index("void JPAResource::drawC"):]
    assert "jpa_crd + (mpBaseShape->getTilingS() + 2 * mpBaseShape->getTilingT()) * 8" in parent
    assert "jpa_pos + center" in parent and "GX_TEXMAP0" in parent
    assert "jpa_pos + 0x30" in child and "jpa_crd, GX_TEXMAP1" in child


def main() -> int:
    test_billboard_marker_off_uses_legacy_loop()
    test_billboard_check_emits_once()
    test_billboard_visible_count_and_reverse_order()
    test_billboard_colour_helpers_are_read_only()
    test_parent_child_billboard_arguments()
    with tempfile.TemporaryDirectory(prefix="smg-jpa-billboard-") as directory:
        executable = Path(directory) / "jpa_billboard_host"
        build = subprocess.run(
            ["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
             "-I", str(INCLUDE), str(SOURCE), "-o", str(executable)],
            capture_output=True, text=True,
        )
        if build.returncode:
            print("jpa-billboard: FAIL (compile)\n" + build.stderr)
            return 1
        run = subprocess.run([str(executable)], capture_output=True, text=True)
        print((run.stdout + run.stderr).strip())
        return run.returncode


if __name__ == "__main__":
    sys.exit(main())
