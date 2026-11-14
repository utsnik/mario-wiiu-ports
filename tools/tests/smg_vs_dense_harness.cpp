#include "gx2_vs_dense.h"
#include "smg_vs_dense_generator.h"

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

using smg_vs_dense::DenseInput;
using smg_vs_dense::DenseResult;
using smg_vs_dense_test_generator::GeneratedProgram;
using smg_vs_dense_test_generator::VertexKey;

static uint64_t packInstruction(uint32_t word0, uint32_t word1) {
    return (static_cast<uint64_t>(__builtin_bswap32(word0)) << 32) |
           __builtin_bswap32(word1);
}

static void dumpProgram(uint32_t key, const GeneratedProgram& program) {
    std::printf("key=%u alu=%zu exports=%zu\n", key, program.alu.size(), program.exports.size());
    for (uint64_t packed : program.alu)
        std::printf("  ALU %08x %08x\n",
                    __builtin_bswap32(static_cast<uint32_t>(packed >> 32)),
                    __builtin_bswap32(static_cast<uint32_t>(packed)));
    for (uint64_t packed : program.exports)
        std::printf("  EXP %08x %08x\n",
                    __builtin_bswap32(static_cast<uint32_t>(packed >> 32)),
                    __builtin_bswap32(static_cast<uint32_t>(packed)));
}

static void splitAlu(const std::vector<uint64_t>& packed, std::vector<uint32_t>& words,
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
        if (!ended) std::fprintf(stderr, "unterminated ALU group\n");
    }
}

static DenseResult compact(GeneratedProgram& program, uint32_t& originalMaximum) {
    std::vector<uint32_t> aluWords, groups, exportWords;
    splitAlu(program.alu, aluWords, groups);
    for (uint64_t op : program.exports) {
        exportWords.push_back(__builtin_bswap32(static_cast<uint32_t>(op >> 32)));
        exportWords.push_back(__builtin_bswap32(static_cast<uint32_t>(op)));
    }
    DenseInput input = {aluWords.data(), aluWords.size(), groups.data(), groups.size(),
                        exportWords.data(), program.exports.size(), 14};
    DenseResult original = {};
    smg_vs_dense::denseResetResult(original);
    if (!smg_vs_dense::denseValidateGroups(input, original)) return original;
    originalMaximum = original.logicalMaximum;
    DenseResult result = {};
    if (!smg_vs_dense::compactGeneratedVertexGprs(input, result)) return result;
    program.alu.resize(aluWords.size() / 2);
    for (size_t i = 0; i < program.alu.size(); ++i)
        program.alu[i] = packInstruction(aluWords[2 * i], aluWords[2 * i + 1]);
    program.exports.resize(exportWords.size() / 2);
    for (size_t i = 0; i < program.exports.size(); ++i)
        program.exports[i] = packInstruction(exportWords[2 * i], exportWords[2 * i + 1]);
    return result;
}

struct State {
    float gpr[128][4];
    float constants[256][4];
    int32_t ar;
    float pv[4];
    float ps[4];
};

static State makeState(uint32_t seed) {
    State state = {};
    for (uint32_t r = 0; r < 128; ++r)
        for (uint32_t ch = 0; ch < 4; ++ch)
            state.gpr[r][ch] = std::sin(float((seed + 17 * r + 3 * ch) % 97)) * 0.7f;
    for (uint32_t r = 0; r < 256; ++r)
        for (uint32_t ch = 0; ch < 4; ++ch)
            state.constants[r][ch] = 0.2f + std::cos(float((seed + 11 * r + ch) % 89)) * 0.4f;
    state.constants[0][0] = 1.0f;
    state.constants[0][1] = 0.0f;
    state.constants[0][2] = 0.0f;
    state.constants[0][3] = 1.0f;
    for (uint32_t r = 10; r <= 12; ++r)
        for (uint32_t ch = 0; ch < 4; ++ch)
            state.gpr[r][ch] = float((seed + r + ch) % 4);
    state.ar = 0;
    return state;
}

static bool isOp3(uint32_t word1) {
    return ((word1 >> 15) & 7u) != 0;
}

static float source(const State& state, uint32_t selector, uint32_t channel,
                    bool relative, bool negated) {
    float value = 0.0f;
    if (selector <= 0x7f) value = state.gpr[selector][channel];
    else if (selector >= ALU_SRC_CONST_FILE_BASE && selector < ALU_SRC_CONST_FILE_BASE + 256) {
        int32_t index = static_cast<int32_t>(selector - ALU_SRC_CONST_FILE_BASE);
        if (relative) index += state.ar;
        if (index < 0 || index >= 256) std::abort();
        value = state.constants[index][channel];
    } else if (selector == ALU_SRC_0) value = 0.0f;
    else if (selector == ALU_SRC_1) value = 1.0f;
    else if (selector == ALU_SRC_0_5) value = 0.5f;
    else if (selector == ALU_SRC_PV) value = state.pv[channel];
    else if (selector == ALU_SRC_PS) value = state.ps[channel];
    if (negated) value = -value;
    return value;
}

static float applyOmod(float value, uint32_t omod) {
    if (omod == ALU_OMOD_M2) return value * 2.0f;
    if (omod == ALU_OMOD_M4) return value * 4.0f;
    if (omod == ALU_OMOD_D2) return value * 0.5f;
    return value;
}

static float executeOp2(State& state, uint32_t inst, float a, float b, uint32_t channel) {
    switch (inst) {
    case 0x00: return a + b;
    case 0x01: return a * b;
    case 0x02: return a * b;
    case 0x03: return a > b ? a : b;
    case 0x04: return a < b ? a : b;
    case 0x08: return a == b ? 1.0f : 0.0f;
    case 0x09: return a > b ? 1.0f : 0.0f;
    case 0x0a: return a >= b ? 1.0f : 0.0f;
    case 0x0b: return a != b ? 1.0f : 0.0f;
    case 0x13: return std::nearbyint(a);
    case 0x14: return std::floor(a);
    case 0x18: state.ar = static_cast<int32_t>(a); return a;
    case 0x19: return a;
    case 0x21: return a > b ? 1.0f : 0.0f;
    case 0x2d: return a > b ? 1.0f : 0.0f;
    case 0x34: return float(static_cast<int32_t>(a) + static_cast<int32_t>(b));
    case 0x50:
    case 0x51: return a * b; // The extracted scalar generator does not emit DOT4.
    case 0x66: return 1.0f / a;
    case 0x69: return 1.0f / std::sqrt(a);
    case 0x6a: return std::sqrt(a);
    case 0x6b: return float(static_cast<int32_t>(a));
    case 0x6c: return a;
    case 0x71: return float(static_cast<uint32_t>(static_cast<int32_t>(a)) >>
                            static_cast<uint32_t>(static_cast<int32_t>(b)));
    case 0x72: return float(static_cast<uint32_t>(static_cast<int32_t>(a)) <<
                            static_cast<uint32_t>(static_cast<int32_t>(b)));
    case 0x73: return float(static_cast<int32_t>(a) * static_cast<int32_t>(b));
    default:
        std::fprintf(stderr, "unsupported OP2 0x%x at channel %u\n", inst, channel);
        std::abort();
        return 0.0f;
    }
}

static void executeAlu(State& state, const std::vector<uint64_t>& alu) {
    for (uint64_t packed : alu) {
        const uint32_t word0 = __builtin_bswap32(static_cast<uint32_t>(packed >> 32));
        const uint32_t word1 = __builtin_bswap32(static_cast<uint32_t>(packed));
        const bool op3 = isOp3(word1);
        const uint32_t s0 = word0 & 0x1ffu;
        const uint32_t s1 = (word0 >> 13) & 0x1ffu;
        const uint32_t c0 = (word0 >> 10) & 3u;
        const uint32_t c1 = (word0 >> 23) & 3u;
        const float a = source(state, s0, c0, ((word0 >> 9) & 1u) != 0,
                               ((word0 >> 12) & 1u) != 0);
        const float b = source(state, s1, c1, ((word0 >> 22) & 1u) != 0,
                               ((word0 >> 25) & 1u) != 0);
        const uint32_t dst = (word1 >> 21) & 0x7fu;
        const uint32_t dstChan = (word1 >> 29) & 3u;
        const float value = [&]() {
            if (op3) {
                const uint32_t s2 = word1 & 0x1ffu;
                const uint32_t c2 = (word1 >> 10) & 3u;
                const float c = source(state, s2, c2, ((word1 >> 9) & 1u) != 0,
                                       ((word1 >> 12) & 1u) != 0);
                const uint32_t inst = (word1 >> 13) & 0x3fu;
                if (inst == ALU_OP3_INST_MULADD) return a * b + c;
                if (inst == ALU_OP3_INST_MULADD_D2) return (a * b + c) * 0.5f;
                if (inst == ALU_OP3_INST_CNDGT) return a > 0.0f ? b : c;
                std::fprintf(stderr, "unsupported OP3 0x%x\n", inst);
                std::abort();
            }
            return applyOmod(executeOp2(state, (word1 >> 7) & 0xffu, a, b, dstChan),
                             (word1 >> 5) & 3u);
        }();
        for (uint32_t ch = 0; ch < 4; ++ch) state.pv[ch] = value;
        for (uint32_t ch = 0; ch < 4; ++ch) state.ps[ch] = value;
        if (!op3 && ((word1 >> 7) & 0xffu) == ALU_OP2_INST_MOVA_INT) state.ar = static_cast<int32_t>(value);
        const bool writes = op3 || ((word1 >> 4) & 1u) != 0;
        if (writes) state.gpr[dst][dstChan] = value;
    }
}

static void executeExports(const State& state, const std::vector<uint64_t>& exports,
                           std::vector<float>& output) {
    output.clear();
    for (uint64_t packed : exports) {
        const uint32_t word0 = __builtin_bswap32(static_cast<uint32_t>(packed >> 32));
        const uint32_t word1 = __builtin_bswap32(static_cast<uint32_t>(packed));
        const uint32_t reg = (word0 >> 15) & 0x7fu;
        const uint32_t selectors[4] = {word1 & 7u, (word1 >> 3) & 7u,
                                       (word1 >> 6) & 7u, (word1 >> 9) & 7u};
        for (uint32_t selector : selectors) output.push_back(state.gpr[reg][selector]);
    }
}

static bool equivalent(const GeneratedProgram& before, const GeneratedProgram& after,
                       uint32_t seed) {
    State left = makeState(seed), right = left;
    executeAlu(left, before.alu);
    executeAlu(right, after.alu);
    std::vector<float> leftExports, rightExports;
    executeExports(left, before.exports, leftExports);
    executeExports(right, after.exports, rightExports);
    return leftExports.size() == rightExports.size() &&
           std::memcmp(leftExports.data(), rightExports.data(),
                       leftExports.size() * sizeof(float)) == 0;
}

static VertexKey makeKey(uint32_t i) {
    VertexKey key;
    key.outputMode = i & 1;
    key.fixedCoord = (i * 3) & 7;
    key.fixedTextured = (i >> 1) & 1;
    key.numChans = key.outputMode ? 0 : (i % 3);
    for (uint32_t c = 0; c < 2; ++c) {
        const uint32_t light = (i + c) % 5;
        key.colorCtrl[c] = (i + c) % 4 == 0 ? 0 : 2 | (1u << (2 + light));
        if ((i + c) % 7 == 0) key.colorCtrl[c] |= 1;
        key.alphaCtrl[c] = (i + c) % 5 == 0 ? key.colorCtrl[c] ^ 2 : key.colorCtrl[c];
        if ((i + c) % 6 == 0) key.colorCtrl[c] |= 1u << 9;
    }
    key.numTexGens = key.outputMode ? 0 : i % 9;
    key.dualTex = (!key.outputMode && i % 3 == 0) ? 1 : 0;
    key.projectionOrtho = (i >> 2) & 1;
    for (uint32_t coord = 0; coord < 8; ++coord) {
        const uint32_t type = (i + coord) % 4;
        const uint32_t row = (i * 3 + coord) % 13;
        key.texgen[coord] = (type << 4) | (((i + coord) & 1) << 2) |
                            ((row & 31) << 7) | (((i + coord) % 3 == 0) << 1) |
                            (((i + coord) % 5) << 12);
        key.postTex[coord] = ((i + coord) % 3 == 0 ? 1u << 8 : 0) |
                             ((i * 5 + coord) & 63);
    }
    return key;
}

int main(int argc, char** argv) {
    const bool dump = argc > 1 && std::strcmp(argv[1], "--dump") == 0;
    std::map<uint32_t, uint32_t> histogram;
    uint32_t maxLogical = 0, maxPhysical = 0;
    for (uint32_t i = 0; i < 70; ++i) {
        const VertexKey key = makeKey(i);
        GeneratedProgram original = smg_vs_dense_test_generator::generateVertexProgram(key);
        if (dump) dumpProgram(i, original);
        GeneratedProgram compacted = original;
        uint32_t originalMaximum = 0;
        DenseResult result = compact(compacted, originalMaximum);
        if (!result.accepted) {
            std::fprintf(stderr, "key=%u compaction rejected reason=%s logical=%u declared=%u\n",
                         i, result.reason ? result.reason : "unknown", originalMaximum,
                         result.declaredCount);
            return 1;
        }
        if (originalMaximum > maxLogical) maxLogical = originalMaximum;
        if (result.physicalMaximum > maxPhysical) maxPhysical = result.physicalMaximum;
        ++histogram[result.declaredCount];
        for (uint32_t seed = 1; seed <= 8; ++seed) {
            if (!equivalent(original, compacted, i * 101 + seed)) {
                std::fprintf(stderr, "key=%u seed=%u equivalence failure declared=%u\n",
                             i, seed, result.declaredCount);
                return 1;
            }
        }
    }
    if (maxLogical < 60) {
        std::fprintf(stderr, "corpus did not exercise R53-R60: max logical=%u\n", maxLogical);
        return 1;
    }
    std::printf("equivalence: PASS corpus=70 inputs_per_key=8 max_logical=%u max_physical=%u histogram=",
                maxLogical, maxPhysical);
    bool first = true;
    for (const auto& entry : histogram) {
        if (!first) std::printf(",");
        first = false;
        std::printf("%u:%u", entry.first, entry.second);
    }
    std::printf("\n");
    return 0;
}
