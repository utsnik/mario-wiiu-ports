/*
 * Host extraction of the generated-VS ALU path in gx2_renderer.cpp.
 *
 * This intentionally uses latte_asm.h's production packer macros.  The
 * Python test checks the source snapshot hash and the source anchors below so
 * that this dependency-free extraction cannot silently drift from the Wii U
 * generator.
 */
#ifndef SMS_TEST_VS_DENSE_GENERATOR_H
#define SMS_TEST_VS_DENSE_GENERATOR_H

#include "latte_asm.h"

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace smg_vs_dense_test_generator {

// SHA-256 of gx2_renderer.cpp's vertexEmit..vertex generator ALU source
// extraction, checked by test_smg_wiiu_vs_dense.py.
static constexpr const char* kRendererGeneratorSourceSha256 =
    "b4372dcf525f46d3097c6aa3493b76d3bc3ff400baf4351758e0154bb232ab9a";

static constexpr uint32_t kVtxLiveBase = 192;
static constexpr uint32_t kXfNormalBase = 64;
static constexpr uint32_t kXfPostTexBase = 96;
static constexpr uint32_t kXfLightBase = 160;

enum RawAttrib : uint32_t {
    RAW_ATTR_POS = 0, RAW_ATTR_NRM, RAW_ATTR_BIN, RAW_ATTR_TAN,
    RAW_ATTR_CLR0, RAW_ATTR_CLR1, RAW_ATTR_TEX01, RAW_ATTR_TEX23,
    RAW_ATTR_TEX45, RAW_ATTR_TEX67, RAW_ATTR_MTX0, RAW_ATTR_MTX1, RAW_ATTR_MTX2,
};

struct VertexKey {
    uint32_t outputMode = 0;
    uint32_t fixedCoord = 0;
    uint32_t fixedTextured = 0;
    uint32_t numChans = 0;
    uint32_t colorCtrl[2] = {};
    uint32_t alphaCtrl[2] = {};
    uint32_t numTexGens = 0;
    uint32_t texgen[8] = {};
    uint32_t dualTex = 0;
    uint32_t postTex[8] = {};
    uint32_t projectionOrtho = 0;
};

struct GeneratedProgram {
    std::vector<uint64_t> alu;
    std::vector<uint64_t> exports;
};

inline void vertexEmit(std::vector<uint64_t>& ops, std::initializer_list<uint64_t> group) {
    for (uint64_t op : group) ops.push_back(op);
    if (!ops.empty()) ops.back() |= to_QWORD(to_LE(1ull << 31), 0);
}

inline void vertexEmitDot4(std::vector<uint64_t>& ops, uint32_t dst, uint32_t dstChan,
                           uint32_t src0, uint32_t src1) {
    vertexEmit(ops, {ALU_MUL(_R(dst), dstChan, src0, _x, src1, _x)});
    vertexEmit(ops, {ALU_MULADD(_R(dst), dstChan, src0, _y, src1, _y, _R(dst), dstChan)});
    vertexEmit(ops, {ALU_MULADD(_R(dst), dstChan, src0, _z, src1, _z, _R(dst), dstChan)});
    vertexEmit(ops, {ALU_MULADD(_R(dst), dstChan, src0, _w, src1, _w, _R(dst), dstChan)});
}

inline void vertexCopySource(std::vector<uint64_t>& ops, uint32_t source, uint32_t dst,
                             bool colour) {
    vertexEmit(ops, {ALU_MOV(_R(dst), _x, _R(source), _x)});
    vertexEmit(ops, {ALU_MOV(_R(dst), _y, _R(source), _y)});
    vertexEmit(ops, {ALU_MOV(_R(dst), _z, _R(source), _z)});
    vertexEmit(ops, {ALU_MOV(_R(dst), _w, colour ? _R(source) : ALU_SRC_1,
                              colour ? _w : _x)});
}

inline void vertexCopyTexSource(std::vector<uint64_t>& ops, uint32_t coord, uint32_t dst) {
    const uint32_t source = RAW_ATTR_TEX01 + coord / 2 + 1;
    const uint32_t chan = (coord & 1) * 2;
    vertexEmit(ops, {ALU_MOV(_R(dst), _x, _R(source), chan)});
    vertexEmit(ops, {ALU_MOV(_R(dst), _y, _R(source), chan + 1)});
    vertexEmit(ops, {ALU_MOV(_R(dst), _z, ALU_SRC_1, _x)});
    vertexEmit(ops, {ALU_MOV(_R(dst), _w, ALU_SRC_1, _x)});
}

inline void vertexLoadRelativeMatrix(std::vector<uint64_t>& ops, uint32_t indexReg,
                                     uint32_t indexChan, uint32_t dst, uint32_t base,
                                     uint32_t rows = 4) {
    const size_t run = 3 + rows * 4;
    while (ops.size() % 128 + run > 128)
        vertexEmit(ops, {ALU_MOV(_R(57), _x, ALU_SRC_0, _x)});
    vertexEmit(ops, {ALU_FLT_TO_INT(__, _x, _R(indexReg), indexChan)});
    vertexEmit(ops, {ALU_MOV(__, _x, ALU_SRC_PS, _x)});
    vertexEmit(ops, {ALU_MOVA_INT(__, _x, ALU_SRC_PV, _x)});
    for (uint32_t row = 0; row < rows; ++row)
        for (uint32_t ch = 0; ch < 4; ++ch)
            vertexEmit(ops, {ALU_MOV_REL_CONST(_R(dst + row), ch, _GX2_C(base + row), ch)});
}

inline void vertexLoadAbsoluteMatrix(std::vector<uint64_t>& ops, uint32_t dst, uint32_t base,
                                     uint32_t rows = 4) {
    for (uint32_t row = 0; row < rows; ++row)
        for (uint32_t ch = 0; ch < 4; ++ch)
            vertexEmit(ops, {ALU_MOV(_R(dst + row), ch, _GX2_C(base + row), ch)});
}

inline void vertexMatrixTransform(std::vector<uint64_t>& ops, uint32_t source,
                                  uint32_t indexReg, uint32_t indexChan, uint32_t dst,
                                  uint32_t base, bool relative) {
    if (relative) vertexLoadRelativeMatrix(ops, indexReg, indexChan, 58, base, 3);
    else vertexLoadAbsoluteMatrix(ops, 58, base, 3);
    for (uint32_t ch = 0; ch < 3; ++ch)
        vertexEmitDot4(ops, dst, ch, _R(source), _R(58 + ch));
}

inline void vertexNormalTransform(std::vector<uint64_t>& ops) {
    vertexLoadRelativeMatrix(ops, RAW_ATTR_MTX0 + 1, _x, 53, kXfNormalBase, 3);
    for (uint32_t ch = 0; ch < 3; ++ch)
        vertexEmitDot4(ops, 41, ch, _R(2), _R(53 + ch));
    vertexEmit(ops, {ALU_MUL(_R(45), _x, _R(41), _x, _R(41), _x)});
    vertexEmit(ops, {ALU_MULADD(_R(45), _x, _R(41), _y, _R(41), _y, _R(45), _x)});
    vertexEmit(ops, {ALU_MULADD(_R(45), _x, _R(41), _z, _R(41), _z, _R(45), _x)});
    vertexEmit(ops, {ALU_SETGT(_R(47), _x, _R(45), _x, ALU_SRC_0, _x)});
    vertexEmit(ops, {ALU_RECIPSQRT_IEEE(_R(46), _x, _R(45), _x)});
    for (uint32_t ch = 0; ch < 3; ++ch) {
        vertexEmit(ops, {ALU_MUL(_R(41), ch, _R(41), ch, _R(46), _x)});
        vertexEmit(ops, {ALU_MUL(_R(41), ch, _R(41), ch, _R(47), _x)});
    }
    vertexEmit(ops, {ALU_CNDGT(_R(41), _z, _R(47), _x, _R(41), _z, ALU_SRC_1, _x)});
}

inline void vertexEmitLighting(std::vector<uint64_t>& ops, uint32_t ctrl, uint32_t channel,
                               uint32_t dst, uint32_t first, uint32_t count) {
    const uint32_t input = 5 + channel;
    const uint32_t mat = kVtxLiveBase + 5 + channel * 2;
    const uint32_t amb = mat + 1;
    for (uint32_t ch = 0; ch < 4; ++ch) {
        if (ctrl & 1) vertexEmit(ops, {ALU_MOV(_R(44), ch, _R(input), ch)});
        else vertexEmit(ops, {ALU_MOV(_R(44), ch, _GX2_C(mat), ch)});
        if (ctrl & 64) vertexEmit(ops, {ALU_MOV(_R(43), ch, _R(input), ch)});
        else vertexEmit(ops, {ALU_MOV(_R(43), ch, _GX2_C(amb), ch)});
    }
    auto dot3 = [&](uint32_t d, uint32_t dch, uint32_t a, uint32_t b, uint32_t b0) {
        vertexEmit(ops, {ALU_MUL(_R(d), dch, _R(a), _x, b, b0)});
        vertexEmit(ops, {ALU_MULADD(_R(d), dch, _R(a), _y, b, b0 + 1, _R(d), dch)});
        vertexEmit(ops, {ALU_MULADD(_R(d), dch, _R(a), _z, b, b0 + 2, _R(d), dch)});
    };
    if (!(ctrl & 2)) {
        for (uint32_t ch = 0; ch < 4; ++ch) vertexEmit(ops, {ALU_MOV(_R(43), ch, ALU_SRC_1, _x)});
    } else {
        const uint32_t mask = ((ctrl >> 2) & 15) | (((ctrl >> 11) & 15) << 4);
        const uint32_t diff = (ctrl >> 7) & 3;
        const bool atten = (ctrl >> 9) & 1;
        const bool spot = (ctrl >> 10) & 1;
        for (uint32_t light = 0; light < 8; ++light) {
            if (!(mask & (1u << light))) continue;
            const uint32_t c = kXfLightBase + light * 4;
            if (!atten || spot) {
                vertexEmit(ops, {ALU_ADD(_R(42), _x, _GX2_C(c + 2), _z, _R(40) _NEG, _x)});
                vertexEmit(ops, {ALU_ADD(_R(42), _y, _GX2_C(c + 2), _w, _R(40) _NEG, _y)});
                vertexEmit(ops, {ALU_ADD(_R(42), _z, _GX2_C(c + 3), _x, _R(40) _NEG, _z)});
            } else {
                vertexEmit(ops, {ALU_MOV(_R(42), _x, _GX2_C(c + 2), _z)});
                vertexEmit(ops, {ALU_MOV(_R(42), _y, _GX2_C(c + 2), _w)});
                vertexEmit(ops, {ALU_MOV(_R(42), _z, _GX2_C(c + 3), _x)});
            }
            dot3(45, _x, 42, _R(42), _x);
            vertexEmit(ops, {ALU_SETGT(_R(47), _x, _R(45), _x, ALU_SRC_0, _x)});
            vertexEmit(ops, {ALU_RECIPSQRT_IEEE(_R(46), _y, _R(45), _x)});
            vertexEmit(ops, {ALU_MUL(_R(46), _y, _R(46), _y, _R(47), _x)});
            vertexEmit(ops, {ALU_MUL(_R(48), _x, _R(45), _x, _R(46), _y)});
            for (uint32_t ch = 0; ch < 3; ++ch)
                vertexEmit(ops, {ALU_MUL(_R(42), ch, _R(42), ch, _R(46), _y)});
            if (atten) {
                if (spot) {
                    dot3(45, _y, 42, _GX2_C(c + 3), _y);
                    vertexEmit(ops, {ALU_MAX(_R(45), _y, _R(45), _y, ALU_SRC_0, _x)});
                    vertexEmit(ops, {ALU_MOV(_R(48), _y, _R(48), _x)});
                } else {
                    dot3(45, _z, 41, _R(42), _x);
                    vertexEmit(ops, {ALU_SETGT(_R(47), _z, _R(45), _z, ALU_SRC_0, _x)});
                    dot3(45, _y, 41, _GX2_C(c + 3), _y);
                    vertexEmit(ops, {ALU_MAX(_R(45), _y, _R(45), _y, ALU_SRC_0, _x)});
                    vertexEmit(ops, {ALU_MUL(_R(45), _y, _R(45), _y, _R(47), _z)});
                    vertexEmit(ops, {ALU_MOV(_R(48), _y, _R(45), _y)});
                }
                vertexEmit(ops, {ALU_MULADD(_R(46), _x, _GX2_C(c + 1), _z, _R(45), _y, _GX2_C(c + 1), _y)});
                vertexEmit(ops, {ALU_MULADD(_R(46), _x, _R(46), _x, _R(45), _y, _GX2_C(c + 1), _x)});
                vertexEmit(ops, {ALU_MAX(_R(46), _x, _R(46), _x, ALU_SRC_0, _x)});
                vertexEmit(ops, {ALU_MULADD(_R(46), _z, _GX2_C(c + 2), _y, _R(48), _y, _GX2_C(c + 2), _x)});
                vertexEmit(ops, {ALU_MULADD(_R(46), _z, _R(46), _z, _R(48), _y, _GX2_C(c + 1), _w)});
                vertexEmit(ops, {ALU_SETGT(_R(47), _y, _R(46), _z, ALU_SRC_0, _x)});
                vertexEmit(ops, {ALU_RECIP_IEEE(_R(46), _z, _R(46), _z)});
                vertexEmit(ops, {ALU_MUL(_R(46), _x, _R(46), _x, _R(46), _z)});
                vertexEmit(ops, {ALU_MUL(_R(46), _x, _R(46), _x, _R(47), _y)});
            } else vertexEmit(ops, {ALU_MOV(_R(46), _x, ALU_SRC_1, _x)});
            if (diff != 0) {
                dot3(45, _x, 41, _R(42), _x);
                if (diff >= 2) vertexEmit(ops, {ALU_MAX(_R(45), _x, _R(45), _x, ALU_SRC_0, _x)});
                vertexEmit(ops, {ALU_MUL(_R(46), _x, _R(46), _x, _R(45), _x)});
            }
            for (uint32_t ch = 0; ch < 4; ++ch)
                vertexEmit(ops, {ALU_MULADD(_R(43), ch, _R(46), _x, _GX2_C(c), ch, _R(43), ch)});
        }
    }
    for (uint32_t ch = first; ch < first + count; ++ch) {
        vertexEmit(ops, {ALU_MAX(_R(43), ch, _R(43), ch, ALU_SRC_0, _x)});
        vertexEmit(ops, {ALU_MIN(_R(43), ch, _R(43), ch, ALU_SRC_1, _x)});
        vertexEmit(ops, {ALU_MUL(_R(dst), ch, _R(44), ch, _R(43), ch)});
    }
}

inline void vertexEmitColour(std::vector<uint64_t>& ops, const VertexKey& key,
                             uint32_t channel, uint32_t dst) {
    if (channel >= key.numChans) {
        for (uint32_t ch = 0; ch < 4; ++ch) vertexEmit(ops, {ALU_MOV(_R(dst), ch, ALU_SRC_0, _x)});
        return;
    }
    const uint32_t ctrl = key.colorCtrl[channel];
    const uint32_t alphaCtrl = key.alphaCtrl[channel];
    if (ctrl == alphaCtrl) vertexEmitLighting(ops, ctrl, channel, dst, 0, 4);
    else {
        vertexEmitLighting(ops, ctrl, channel, dst, 0, 3);
        vertexEmitLighting(ops, alphaCtrl, channel, dst, 3, 1);
    }
}

inline uint32_t vertexInputForRow(uint32_t row) {
    if (row == 0) return 1;
    if (row == 1) return 2;
    if (row == 3) return 3;
    if (row == 4) return 4;
    if (row >= 5) return RAW_ATTR_TEX01 + (row - 5) / 2 + 1;
    return 29;
}

inline GeneratedProgram generateVertexProgram(const VertexKey& key) {
    GeneratedProgram program;
    std::vector<uint64_t>& alu = program.alu;
    alu.reserve(512);
    vertexMatrixTransform(alu, 1, RAW_ATTR_MTX0 + 1, _x, 40, 0, true);
    if (key.projectionOrtho) {
        vertexEmit(alu, {ALU_MULADD(_R(20), _x, _R(40), _x, _GX2_C(kVtxLiveBase), _x,
                                    _GX2_C(kVtxLiveBase), _y)});
        vertexEmit(alu, {ALU_MULADD(_R(20), _y, _R(40), _y, _GX2_C(kVtxLiveBase), _z,
                                    _GX2_C(kVtxLiveBase), _w)});
        vertexEmit(alu, {ALU_MULADD(_R(20), _z, _R(40), _z, _GX2_C(kVtxLiveBase + 1), _x,
                                    _GX2_C(kVtxLiveBase + 1), _y)});
        vertexEmit(alu, {ALU_MOV(_R(20), _w, ALU_SRC_1, _x)});
    } else {
        vertexEmit(alu, {ALU_MUL(_R(20), _x, _R(40), _x, _GX2_C(kVtxLiveBase), _x)});
        vertexEmit(alu, {ALU_MULADD(_R(20), _x, _R(40), _z, _GX2_C(kVtxLiveBase), _y, _R(20), _x)});
        vertexEmit(alu, {ALU_MUL(_R(20), _y, _R(40), _y, _GX2_C(kVtxLiveBase), _z)});
        vertexEmit(alu, {ALU_MULADD(_R(20), _y, _R(40), _z, _GX2_C(kVtxLiveBase), _w, _R(20), _y)});
        vertexEmit(alu, {ALU_MULADD(_R(20), _z, _R(40), _z, _GX2_C(kVtxLiveBase + 1), _x,
                                    _GX2_C(kVtxLiveBase + 1), _y)});
        vertexEmit(alu, {ALU_ADD(_R(20), _w, ALU_SRC_0, _x, _R(40) _NEG, _z)});
    }
    vertexEmit(alu, {ALU_MUL(_R(54), _x, _GX2_C(kVtxLiveBase + 2), _z, _GX2_C(kVtxLiveBase + 3), _x)});
    vertexEmit(alu, {ALU_MUL(_R(54), _x, _R(54), _x, ALU_SRC_0_5, _x)});
    vertexEmit(alu, {ALU_MUL(_R(54), _y, _GX2_C(kVtxLiveBase + 2), _w, _GX2_C(kVtxLiveBase + 3), _y)});
    vertexEmit(alu, {ALU_MUL(_R(54), _y, _R(54), _y, ALU_SRC_0_5, _x)});
    vertexEmit(alu, {ALU_MULADD(_R(55), _x, _GX2_C(kVtxLiveBase + 2), _x, _GX2_C(kVtxLiveBase + 3), _x, _R(54), _x)});
    vertexEmit(alu, {ALU_ADD(_R(55), _x, _R(55), _x, _GX2_C(kVtxLiveBase + 3), _z)});
    vertexEmit(alu, {ALU_MULADD(_R(55), _y, _GX2_C(kVtxLiveBase + 2), _y, _GX2_C(kVtxLiveBase + 3), _y, _R(54), _y)});
    vertexEmit(alu, {ALU_MUL(_R(55), _x, _R(55), _x, _R(20), _w)});
    vertexEmit(alu, {ALU_MULADD(_R(55), _x, _R(54), _x, _R(20), _x, _R(55), _x)});
    vertexEmit(alu, {ALU_MUL(_R(55), _y, _R(55), _y, _R(20), _w)});
    vertexEmit(alu, {ALU_MULADD(_R(55), _y, _R(54), _y, _R(20) _NEG, _y, _R(55), _y)});
    vertexEmit(alu, {ALU_RECIP_IEEE(_R(56), _x, _GX2_C(kVtxLiveBase + 3), _w)});
    vertexEmit(alu, {ALU_MUL_x2(_R(55), _x, _R(55), _x, _R(56), _x)});
    vertexEmit(alu, {ALU_RECIP_IEEE(_R(56), _x, _GX2_C(kVtxLiveBase + 4), _z)});
    vertexEmit(alu, {ALU_MUL_x2(_R(55), _y, _R(55), _y, _R(56), _x)});
    vertexEmit(alu, {ALU_ADD(_R(20), _x, _R(55), _x, _R(20) _NEG, _w)});
    vertexEmit(alu, {ALU_ADD(_R(20), _y, _R(20), _w, _R(55) _NEG, _y)});
    vertexEmit(alu, {ALU_MUL(_R(56), _y, _GX2_C(kVtxLiveBase + 4), _y, _R(20), _w)});
    vertexEmit(alu, {ALU_MULADD(_R(20), _z, _GX2_C(kVtxLiveBase + 4), _x, _R(20), _z, _R(56), _y)});

    if (key.outputMode) {
        if (key.fixedTextured) vertexCopyTexSource(alu, key.fixedCoord, 21);
        else {
            vertexEmit(alu, {ALU_MOV(_R(21), _x, ALU_SRC_0, _x)});
            vertexEmit(alu, {ALU_MOV(_R(21), _y, ALU_SRC_0, _x)});
            vertexEmit(alu, {ALU_MOV(_R(21), _z, ALU_SRC_1, _x)});
            vertexEmit(alu, {ALU_MOV(_R(21), _w, ALU_SRC_1, _x)});
        }
        for (uint32_t reg = 22; reg <= 24; ++reg) {
            vertexEmit(alu, {ALU_MOV(_R(reg), _x, ALU_SRC_0, _x)});
            vertexEmit(alu, {ALU_MOV(_R(reg), _y, ALU_SRC_0, _x)});
            vertexEmit(alu, {ALU_MOV(_R(reg), _z, ALU_SRC_0, _x)});
            vertexEmit(alu, {ALU_MOV(_R(reg), _w, ALU_SRC_1, _x)});
        }
        vertexCopySource(alu, 5, 29, true);
    } else {
        bool needsNormal = false;
        for (uint32_t c = 0; c < 2; ++c)
            needsNormal = needsNormal || (c < key.numChans &&
                                          ((key.colorCtrl[c] | key.alphaCtrl[c]) & 2));
        if (needsNormal) vertexNormalTransform(alu);
        vertexEmitColour(alu, key, 0, 29);
        vertexEmitColour(alu, key, 1, 30);
        const uint32_t matrixIndexBase = RAW_ATTR_MTX0 + 1;
        for (uint32_t coord = 0; coord < 8; ++coord) {
            const uint32_t dst = 21 + coord;
            if (coord >= key.numTexGens) {
                vertexEmit(alu, {ALU_MOV(_R(dst), _x, ALU_SRC_0, _x)});
                vertexEmit(alu, {ALU_MOV(_R(dst), _y, ALU_SRC_0, _x)});
                vertexEmit(alu, {ALU_MOV(_R(dst), _z, ALU_SRC_0, _x)});
                vertexEmit(alu, {ALU_MOV(_R(dst), _w, ALU_SRC_1, _x)});
                continue;
            }
            const uint32_t tg = key.texgen[coord];
            const uint32_t type = (tg >> 4) & 7;
            const uint32_t row = (tg >> 7) & 31;
            const uint32_t form = (tg >> 2) & 1;
            if (type == 0) {
                if (row >= 5) vertexCopyTexSource(alu, row - 5, 52);
                else {
                    const uint32_t source = row == 2 ? 29 : vertexInputForRow(row);
                    vertexCopySource(alu, source, 52, row == 2);
                }
                const uint32_t index = matrixIndexBase + (coord + 1) / 4;
                const uint32_t indexChan = (coord + 1) & 3;
                vertexMatrixTransform(alu, 52, index, indexChan, 55, 0, true);
                if ((key.dualTex & 1) != 0) {
                    const uint32_t post = key.postTex[coord];
                    if ((post >> 8) & 1) {
                        vertexEmit(alu, {ALU_MOV(_R(55), _w, ALU_SRC_0, _x)});
                        vertexEmitDot4(alu, 56, _x, _R(55), _R(55));
                        vertexEmit(alu, {ALU_SETGT(_R(56), _y, _R(56), _x, ALU_SRC_0, _x)});
                        vertexEmit(alu, {ALU_CNDGT(_R(56), _z, _R(56), _y, _R(56), _x, ALU_SRC_1, _x)});
                        vertexEmit(alu, {ALU_RECIPSQRT_IEEE(_R(56), _x, _R(56), _z)});
                        for (uint32_t ch = 0; ch < 3; ++ch)
                            vertexEmit(alu, {ALU_MUL(_R(55), ch, _R(55), ch, _R(56), _x)});
                        vertexEmit(alu, {ALU_MOV(_R(55), _w, ALU_SRC_1, _x)});
                    }
                    vertexEmit(alu, {ALU_MOV(_R(55), _w, ALU_SRC_1, _x)});
                    vertexLoadAbsoluteMatrix(alu, 58, kXfPostTexBase + (post & 63), 3);
                    for (uint32_t ch = 0; ch < 3; ++ch)
                        vertexEmitDot4(alu, 57, ch, _R(55), _R(58 + ch));
                    for (uint32_t ch = 0; ch < 3; ++ch)
                        vertexEmit(alu, {ALU_MOV(_R(55), ch, _R(57), ch)});
                }
            } else if (type == 1) {
                const uint32_t emboss = (tg >> 12) & 7;
                const uint32_t source = 31 + (emboss < coord ? emboss : 0);
                vertexEmit(alu, {ALU_MOV(_R(55), _x, _R(source), _x)});
                vertexEmit(alu, {ALU_MOV(_R(55), _y, _R(source), _y)});
                vertexEmit(alu, {ALU_MOV(_R(55), _z, _R(source), _z)});
                vertexEmit(alu, {ALU_MOV(_R(55), _w, ALU_SRC_1, _x)});
            } else vertexCopySource(alu, type == 3 ? 30 : 29, 55, true);
            if (form == 0 && row < 5) {
                vertexEmit(alu, {ALU_MOV(_R(55), _z, ALU_SRC_1, _x)});
                vertexEmit(alu, {ALU_MOV(_R(55), _w, ALU_SRC_1, _x)});
            }
            if ((tg >> 1) & 1) {
                vertexEmit(alu, {ALU_SETGT(_R(56), _x, _R(55), _z, ALU_SRC_0, _x)});
                vertexEmit(alu, {ALU_SETGT(_R(56), _y, _R(55) _NEG, _z, ALU_SRC_0, _x)});
                vertexEmit(alu, {ALU_MAX(_R(56), _x, _R(56), _x, _R(56), _y)});
                vertexEmit(alu, {ALU_CNDGT(_R(56), _z, _R(56), _x, _R(55), _z, ALU_SRC_1, _x)});
                vertexEmit(alu, {ALU_RECIP_IEEE(_R(56), _x, _R(56), _z)});
                vertexEmit(alu, {ALU_MUL(_R(55), _x, _R(55), _x, _R(56), _x)});
                vertexEmit(alu, {ALU_MUL(_R(55), _y, _R(55), _y, _R(56), _x)});
            }
            vertexEmit(alu, {ALU_MOV(_R(31 + coord), _x, _R(55), _x)});
            vertexEmit(alu, {ALU_MOV(_R(31 + coord), _y, _R(55), _y)});
            vertexEmit(alu, {ALU_MOV(_R(31 + coord), _z, _R(55), _z)});
            vertexEmit(alu, {ALU_MOV(_R(31 + coord), _w, ALU_SRC_1, _x)});
            const uint32_t scale = kVtxLiveBase + 9 + coord / 2;
            const uint32_t scaleChan = (coord & 1) * 2;
            const uint32_t copy = kVtxLiveBase + 13 + coord;
            vertexEmit(alu, {ALU_MUL(_R(56), _x, _R(55), _x, _GX2_C(scale), scaleChan)});
            vertexEmit(alu, {ALU_MUL(_R(56), _y, _R(55), _y, _GX2_C(scale), scaleChan + 1)});
            vertexEmit(alu, {ALU_MULADD(_R(dst), _x, _R(56), _x, _GX2_C(copy), _x,
                                        _GX2_C(copy), _z)});
            vertexEmit(alu, {ALU_MULADD(_R(dst), _y, _R(56), _y, _GX2_C(copy), _y,
                                        _GX2_C(copy), _w)});
            vertexEmit(alu, {ALU_MOV(_R(dst), _z, ALU_SRC_0, _x)});
            vertexEmit(alu, {ALU_MOV(_R(dst), _w, ALU_SRC_1, _x)});
        }
    }
    program.exports.push_back(EXP_DONE(POS0, _R(20), _x, _y, _z, _w));
    if (key.outputMode) {
        for (uint32_t i = 0; i < 4; ++i)
            program.exports.push_back(EXP(PARAM(i), _R(21 + i), _x, _y, _z, _w) NO_BARRIER);
        program.exports.push_back(EXP_DONE(PARAM(4), _R(29), _x, _y, _z, _w) END_OF_PROGRAM);
    } else {
        for (uint32_t coord = 0; coord < 8; ++coord)
            program.exports.push_back(EXP(PARAM(coord), _R(21 + coord), _x, _y, _z, _w) NO_BARRIER);
        program.exports.push_back(EXP(PARAM(8), _R(29), _x, _y, _z, _w) NO_BARRIER);
        program.exports.push_back(EXP_DONE(PARAM(9), _R(30), _x, _y, _z, _w) END_OF_PROGRAM);
    }
    return program;
}

} // namespace smg_vs_dense_test_generator

#endif
