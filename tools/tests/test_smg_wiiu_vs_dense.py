#!/usr/bin/env python3
"""Host contract for the SMG generated-VS dense register compactor."""

from __future__ import annotations

from pathlib import Path
import re
import hashlib
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "platform/wiiu/gx2_vs_dense.h"
RENDERER = ROOT / "platform/wiiu/gx2_renderer.cpp"
GENERATOR = ROOT / "tools/tests/smg_vs_dense_generator.h"
HARNESS = ROOT / "tools/tests/smg_vs_dense_harness.cpp"
GENERATOR_SOURCE_SHA256 = "b4372dcf525f46d3097c6aa3493b76d3bc3ff400baf4351758e0154bb232ab9a"

# These are the representative VertexKey dimensions that the renderer-side
# integration must keep covered.  The amendment deliberately makes the
# allocator fixture-driven, so this table documents the generator corpus while
# the C++ driver below exercises the real compactor.
VERTEX_KEY_CORPUS = (
    "fixed-untextured-coord0",
    "fixed-textured-coord7",
    "tev-no-lighting",
    "tev-two-lit-channels",
    "perspective-position",
    "orthographic-position",
    "zero-texgens",
    "one-texgen",
    "eight-texgens",
    "projective-division",
    "emboss",
    "dual-texture-post-matrix",
    "disabled-texgens",
    "uniform-register",
    "uniform-block",
)


DRIVER = r'''
#include "gx2_vs_dense.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using smg_vs_dense::DenseInput;
using smg_vs_dense::DenseResult;
using PackedAlu = std::vector<uint64_t>;

static const char* VERTEX_KEY_CORPUS[] = {
    "fixed-untextured-coord0", "fixed-textured-coord7", "tev-no-lighting",
    "tev-two-lit-channels", "perspective-position", "orthographic-position",
    "zero-texgens", "one-texgen", "eight-texgens", "projective-division",
    "emboss", "dual-texture-post-matrix", "disabled-texgens",
    "uniform-register", "uniform-block",
};

static uint64_t packInstruction(uint32_t word0, uint32_t word1) {
    return (static_cast<uint64_t>(__builtin_bswap32(word0)) << 32) |
           __builtin_bswap32(word1);
}

static void splitAlu(const PackedAlu& packed, std::vector<uint32_t>& words,
                     std::vector<uint32_t>& groups) {
    words.clear();
    groups.clear();
    for (size_t at = 0; at < packed.size();) {
        groups.push_back(static_cast<uint32_t>(at));
        bool ended = false;
        do {
            const uint64_t op = packed[at++];
            const uint32_t word0 = __builtin_bswap32(static_cast<uint32_t>(op >> 32));
            const uint32_t word1 = __builtin_bswap32(static_cast<uint32_t>(op));
            words.push_back(word0);
            words.push_back(word1);
            ended = (word0 & (1u << 31)) != 0;
        } while (at < packed.size() && !ended);
    }
}

static void putOp2(PackedAlu& words, uint32_t s0, uint32_t s1,
                   uint32_t dst, bool write = true, bool rel0 = false,
                   bool rel1 = false, bool dstRel = false) {
    const uint32_t word0 = (s0 & 0x1ffu) | ((rel0 ? 1u : 0u) << 9) |
                           ((s1 & 0x1ffu) << 13) | ((rel1 ? 1u : 0u) << 22);
    const uint32_t word1 = (write ? 1u : 0u) << 4 | (0x19u << 7) |
                           ((dst & 0x7fu) << 21) | ((dstRel ? 1u : 0u) << 28);
    words.push_back(packInstruction(word0, word1));
}

static void putOp3(PackedAlu& words, uint32_t s0, uint32_t s1,
                   uint32_t s2, uint32_t dst) {
    const uint32_t word0 = (s0 & 0x1ffu) | ((s1 & 0x1ffu) << 13);
    const uint32_t word1 = (s2 & 0x1ffu) | (0x10u << 13) | (1u << 4) |
                           ((dst & 0x7fu) << 21);
    words.push_back(packInstruction(word0, word1));
}

static void finishGroup(PackedAlu& words) {
    words.back() |= static_cast<uint64_t>(1u) << 32;
}

static void putExport(std::vector<uint32_t>& words, uint32_t src,
                      bool relative = false) {
    words.push_back((src & 0x7fu) << 15 | (relative ? 1u : 0u) << 22);
    words.push_back(0x28u << 23);
}

static DenseResult compact(PackedAlu& packed,
                           std::vector<uint32_t>& exports,
                           uint32_t reserved = 14) {
    std::vector<uint32_t> alu, groups;
    splitAlu(packed, alu, groups);
    DenseInput input = {
        alu.data(), alu.size(), groups.data(), groups.size(), exports.data(),
        exports.size() / 2, reserved,
    };
    DenseResult result = {};
    if (!smg_vs_dense::compactGeneratedVertexGprs(input, result)) return result;
    if (!smg_vs_dense::measureDenseProgram(input, result)) result.accepted = false;
    packed.resize(alu.size() / 2);
    for (size_t i = 0; i < packed.size(); ++i)
        packed[i] = packInstruction(alu[2 * i], alu[2 * i + 1]);
    return result;
}

static int expectReject(PackedAlu alu,
                        std::vector<uint32_t> exports) {
    return compact(alu, exports).accepted ? 0 : 1;
}

int main() {
    uint32_t maxDeclared = 0;
    auto fail = [](const char* where) {
        std::fprintf(stderr, "fixture failed: %s\n", where);
        return 1;
    };

    // OP2, OP3, constants, literal/PV/PS selectors, disabled destination, and
    // an alias whose first value is dead after its group.
    {
        PackedAlu alu;
        std::vector<uint32_t> exports;
        putOp2(alu, 0x100, 0xfe, 60); finishGroup(alu);
        putOp3(alu, 60, 0xfc, 0xff, 61); finishGroup(alu);
        putOp2(alu, 0x101, 0xf8, 62); finishGroup(alu);
        putOp2(alu, 62, 0xf9, 63); finishGroup(alu);
        putExport(exports, 63);
        DenseResult result = compact(alu, exports);
        if (!result.accepted || result.declaredCount > 32 || result.declaredCount != result.physicalMaximum + 1) {
            std::fprintf(stderr, "first result accepted=%d declared=%u physical=%u reason=%s logical=%u\n",
                         result.accepted ? 1 : 0, result.declaredCount, result.physicalMaximum,
                         result.reason, result.logicalMaximum);
            return fail("op2-op3-alias");
        }
        maxDeclared = result.declaredCount > maxDeclared ? result.declaredCount : maxDeclared;
    }

    // A value used only by an export must not be recycled before CF_EXP.
    {
        PackedAlu alu;
        std::vector<uint32_t> exports;
        putOp2(alu, 0x102, 0xfc, 50); finishGroup(alu);
        putOp2(alu, 0x103, 0xfc, 51); finishGroup(alu);
        putExport(exports, 50);
        DenseResult result = compact(alu, exports);
        if (!result.accepted || result.declaredCount > 32) return fail("live-through-export");
        const uint32_t firstDst = (static_cast<uint32_t>(__builtin_bswap32(static_cast<uint32_t>(alu[0]))) >> 21) & 0x7f;
        const uint32_t secondDst = (static_cast<uint32_t>(__builtin_bswap32(static_cast<uint32_t>(alu[1]))) >> 21) & 0x7f;
        const uint32_t exportSrc = (exports[0] >> 15) & 0x7f;
        if (firstDst != exportSrc || firstDst == secondDst) return fail("live-through-export-map");
        maxDeclared = result.declaredCount > maxDeclared ? result.declaredCount : maxDeclared;
    }

    // Relative constant-file reads are preserved; relative GPR source/dest
    // addressing is an explicit fallback condition.
    {
        PackedAlu relativeConst;
        std::vector<uint32_t> noExports;
        putOp2(relativeConst, 0x100, 0xf8, 20, true, true); finishGroup(relativeConst);
        if (!compact(relativeConst, noExports).accepted) return fail("relative-constant");

        PackedAlu relativeGpr;
        putOp2(relativeGpr, 20, 0xf8, 21, true, true); finishGroup(relativeGpr);
        if (!expectReject(relativeGpr, noExports)) return fail("relative-gpr");

        PackedAlu relativeDst;
        putOp2(relativeDst, 0x100, 0xf8, 21, true, false, false, true); finishGroup(relativeDst);
        if (!expectReject(relativeDst, noExports)) return fail("relative-dst");
    }

    // Literal selectors are outside the generated-VS dense helper contract.
    {
        PackedAlu literal;
        std::vector<uint32_t> noExports;
        putOp2(literal, 0xfd, 0xf8, 20); finishGroup(literal);
        DenseResult result = compact(literal, noExports);
        if (result.accepted || std::string(result.reason) != "literal") return fail("literal");
    }

    // 124..127 are clause temporaries and must never be compacted as ordinary GPRs.
    std::vector<uint32_t> noExports;
    for (uint32_t selector : {124u, 125u, 126u, 127u}) {
        PackedAlu bad;
        putOp2(bad, selector, 0xf8, 20); finishGroup(bad);
        if (!expectReject(bad, noExports)) return fail("clause-selector");
    }

    // A deliberately exhausted live set must reject at the hard ceiling.
    {
        PackedAlu alu;
        std::vector<uint32_t> exports;
        for (uint32_t i = 0; i < 30; ++i) {
            putOp2(alu, 0x100 + i, 0xf9, 20 + i);
            finishGroup(alu);
            putExport(exports, 20 + i);
        }
        if (!expectReject(alu, exports)) return fail("hard-ceiling");
    }

    std::printf("vs-dense: PASS: corpus=%zu max_declared_vs=%u target=%u hard_max=%u\n",
                sizeof(VERTEX_KEY_CORPUS) / sizeof(*VERTEX_KEY_CORPUS), maxDeclared,
                smg_vs_dense::kVsDenseTargetCount, smg_vs_dense::kVsDenseHardMaxCount);
    return 0;
}
'''


def main() -> int:
    renderer = RENDERER.read_text(encoding="utf-8")
    generator = GENERATOR.read_text(encoding="utf-8")
    generator_source_start = renderer.index("static void vertexEmit(")
    generator_source_end = renderer.index(
        "    std::vector<VertexAluClause> aluClauses;",
        renderer.index("static bool initVertexProgram"),
    )
    generator_source_hash = hashlib.sha256(
        renderer[generator_source_start:generator_source_end].encode("utf-8")
    ).hexdigest()
    if generator_source_hash != GENERATOR_SOURCE_SHA256:
        print("vs-dense: FAIL: host generator extraction is stale")
        print(f"expected={GENERATOR_SOURCE_SHA256} actual={generator_source_hash}")
        return 1
    if GENERATOR_SOURCE_SHA256 not in generator:
        print("vs-dense: FAIL: missing extracted-generator source hash")
        return 1
    marker_contract = (
        "static bool s_vsDense",
        's_vsDense = sdMarkerPresent("vsdense")',
        'SMG Wii U: [vsdense] marker=%s',
    )
    missing_marker = [needle for needle in marker_contract if needle not in renderer]
    if missing_marker:
        print("vs-dense: FAIL: missing SMG-only vsdense marker contract: " + ", ".join(missing_marker))
        return 1
    dense_boundary = (
        "static bool compactGeneratedVertexProgram",
        "if (s_vsDense)",
        "smg_vs_dense::measureDenseProgram",
        'result.reason = "runtime-self-check"',
        "shader.vertex.regs.sq_pgm_resources_vs = denseDeclaredCount | (1u << 8)",
        "shader.vertex.regs.sq_pgm_resources_vs = 64 | (1u << 8);",
    )
    missing_boundary = [needle for needle in dense_boundary if needle not in renderer]
    if missing_boundary:
        print("vs-dense: FAIL: missing generated-VS dense boundary: " + ", ".join(missing_boundary))
        return 1
    shader_mode_calls = re.findall(r"GX2SetShaderModeEx\((?:.|\n)*?\);", renderer)
    if len(shader_mode_calls) != 2:
        print(f"vs-dense: FAIL: expected two unchanged GX2SetShaderModeEx calls, found {len(shader_mode_calls)}")
        return 1
    for call in shader_mode_calls:
        if "s_vsGprPool" not in call or "s_psGprPool" not in call or "s_vsDense" in call:
            print("vs-dense: FAIL: vsdense changes a shader-mode pool argument")
            return 1
    if re.search(r"(?:resolveDenseVsPool|denseVsPool|s_vsDensePool|psRequirement)", renderer):
        print("vs-dense: FAIL: dense VS pool resolver remains despite the review amendment")
        return 1
    guard_depth = 0
    dense_symbols = ("gx2_vs_dense.h", "s_vsDense", "smg_vs_dense::", "compactGeneratedVertexProgram")
    for line_number, line in enumerate(renderer.splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("#if defined(PORT_GAME_SMG)"):
            guard_depth += 1
        if any(symbol in line for symbol in dense_symbols) and guard_depth == 0:
            print(f"vs-dense: FAIL: dense symbol escapes PORT_GAME_SMG at renderer:{line_number}")
            return 1
        if stripped.startswith("#endif") and guard_depth:
            guard_depth -= 1
    if not HEADER.is_file():
        print("vs-dense: FAIL: missing dense-assembly helper contract (gx2_vs_dense.h)")
        return 1

    if not HARNESS.is_file():
        print("vs-dense: FAIL: missing real-generator equivalence harness")
        return 1

    with tempfile.TemporaryDirectory(prefix="smg-vs-dense-") as directory:
        directory = Path(directory)
        source = HARNESS
        executable = directory / "driver"
        build = subprocess.run(
            ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(HEADER.parent),
             "-I", str(GENERATOR.parent),
             str(source), "-o", str(executable)],
            capture_output=True, text=True,
        )
        if build.returncode:
            print("vs-dense: FAIL: host compactor compile failed")
            print(build.stderr, end="")
            return 1
        run = subprocess.run([str(executable)], capture_output=True, text=True)
        print(run.stdout, end="")
        if run.returncode:
            print("vs-dense: FAIL: host compactor contract failed", file=sys.stderr)
            print(run.stderr, end="", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
