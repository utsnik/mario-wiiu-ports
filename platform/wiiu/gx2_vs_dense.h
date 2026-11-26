/*
 * Dependency-free dense allocator for generated Latte vertex programs.
 *
 * The renderer converts its packed uint64_t instructions into the canonical
 * uint32_t words described by latte_asm.h before calling this header.
 * Keeping the allocator on those words makes the host test exercise exactly
 * the same decoder and rewriter as the Wii U build, without pulling GX2 types
 * or libc allocation into the test executable.
 */
#ifndef SMS_GX2_VS_DENSE_H
#define SMS_GX2_VS_DENSE_H

#include <stddef.h>
#include <stdint.h>

namespace smg_vs_dense {

static constexpr uint32_t kVsDenseTargetCount = 32;
static constexpr uint32_t kVsDenseHardMaxCount = 42;
static constexpr uint32_t kNoPhysicalRegister = 0xffffffffu;

struct DenseInput {
    uint32_t* aluWords;              // word0, word1 for each ALU instruction
    size_t aluWordCount;
    const uint32_t* groupOffsets;    // instruction offsets, one per ALU group
    size_t groupCount;
    uint32_t* exportWords;           // word0, word1 for each CF_EXP
    size_t exportCount;
    uint32_t reservedRegisterCount;  // fixed fetch GPRs are [0, count)
};

struct DenseResult {
    bool accepted;
    uint32_t logicalMaximum;
    uint32_t physicalMaximum;
    uint32_t declaredCount;
    const char* reason;
};

inline void denseResetResult(DenseResult& result) {
    result.accepted = false;
    result.logicalMaximum = 0;
    result.physicalMaximum = 0;
    result.declaredCount = 0;
    result.reason = "unknown";
}

inline bool denseFailure(DenseResult& result, const char* reason) {
    result.accepted = false;
    result.reason = reason;
    return false;
}

inline bool denseIsGprSelector(uint32_t selector) {
    return selector <= 0x7fu;
}

inline bool denseIsOp3(uint32_t word1) {
    // OP2's encoding field is zero in latte_asm.h.  OP3 puts its opcode
    // in bits 13..17, which makes the three-bit OP3 encoding field nonzero.
    return ((word1 >> 15) & 7u) != 0;
}

inline bool denseCheckSource(uint32_t selector, bool relative, DenseResult& result) {
    if (selector == 0xfdu) return denseFailure(result, "literal");
    if (selector <= 0x7fu) {
        if (selector >= 124u) return denseFailure(result, "clause-selector");
        if (relative) return denseFailure(result, "relative-gpr-source");
        if (selector > result.logicalMaximum) result.logicalMaximum = selector;
        return true;
    }
    // 0x80..0xf7 are not a valid source selector in the generator's Latte
    // subset.  0xf8..0xff are the documented zero/one/literal/PV/PS values;
    // 0x100+ is the constant file (including its relative form).
    if (selector < 0xf8u) return denseFailure(result, "malformed-selector");
    return true;
}

inline bool denseCheckDestination(uint32_t word1, bool op3, DenseResult& result,
                                  uint32_t& logical) {
    // OP2 uses bit 4 as write-mask.  OP3 puts src2Sel there instead and every
    // generated OP3 has a real destination; reading it as an OP2 mask leaves
    // MULADD/CNDGT destinations in their logical registers after rewriting.
    if (!op3 && ((word1 >> 4) & 1u) == 0) return true; // disabled destination (__)
    if (((word1 >> 28) & 1u) != 0) return denseFailure(result, "relative-gpr-destination");
    logical = (word1 >> 21) & 0x7fu;
    if (logical >= 124u) return denseFailure(result, "clause-selector");
    if (logical > result.logicalMaximum) result.logicalMaximum = logical;
    return true;
}

inline bool denseCheckInstruction(uint32_t word0, uint32_t word1,
                                  DenseResult& result) {
    const bool op3 = denseIsOp3(word1);
    const uint32_t src0 = word0 & 0x1ffu;
    const uint32_t src1 = (word0 >> 13) & 0x1ffu;
    if (!denseCheckSource(src0, ((word0 >> 9) & 1u) != 0, result) ||
        !denseCheckSource(src1, ((word0 >> 22) & 1u) != 0, result)) return false;
    if (op3 &&
        !denseCheckSource(word1 & 0x1ffu, ((word1 >> 9) & 1u) != 0, result)) return false;
    uint32_t ignored = 0;
    return denseCheckDestination(word1, op3, result, ignored);
}

inline bool denseCheckExport(uint32_t word0, DenseResult& result,
                             uint32_t& logical) {
    if (((word0 >> 22) & 1u) != 0) return denseFailure(result, "relative-export-source");
    logical = (word0 >> 15) & 0x7fu;
    if (logical >= 124u) return denseFailure(result, "clause-selector");
    if (logical > result.logicalMaximum) result.logicalMaximum = logical;
    return true;
}

inline bool denseValidateGroups(const DenseInput& input, DenseResult& result) {
    if (!input.aluWords || (input.aluWordCount & 1u) != 0 ||
        (!input.groupOffsets && input.groupCount) ||
        input.reservedRegisterCount > 124u) return denseFailure(result, "malformed-input");
    const size_t instructionCount = input.aluWordCount / 2;
    size_t previous = 0;
    for (size_t group = 0; group < input.groupCount; ++group) {
        const size_t offset = input.groupOffsets[group];
        if (offset < previous || offset >= instructionCount) return denseFailure(result, "malformed-groups");
        previous = offset;
    }
    for (size_t instruction = 0; instruction < instructionCount; ++instruction) {
        if (!denseCheckInstruction(input.aluWords[2 * instruction],
                                   input.aluWords[2 * instruction + 1], result)) return false;
    }
    if (input.exportCount && !input.exportWords) return denseFailure(result, "malformed-input");
    for (size_t exportIndex = 0; exportIndex < input.exportCount; ++exportIndex) {
        uint32_t ignored = 0;
        if (!denseCheckExport(input.exportWords[2 * exportIndex], result, ignored)) return false;
    }
    return true;
}

inline void denseUpdatePhysical(DenseResult& result, uint32_t physical, bool& hasPhysical) {
    hasPhysical = true;
    if (physical > result.physicalMaximum) result.physicalMaximum = physical;
}

inline bool denseAcquire(uint32_t map[128], bool used[124], uint32_t logical,
                         const DenseInput& input, DenseResult& result,
                         bool& hasPhysical, uint32_t& physical) {
    if (logical < input.reservedRegisterCount) {
        physical = logical;
        denseUpdatePhysical(result, physical, hasPhysical);
        return true;
    }
    if (map[logical] != kNoPhysicalRegister) {
        physical = map[logical];
        denseUpdatePhysical(result, physical, hasPhysical);
        return true;
    }
    for (uint32_t candidate = input.reservedRegisterCount; candidate < 124u; ++candidate) {
        if (used[candidate]) continue;
        used[candidate] = true;
        map[logical] = candidate;
        physical = candidate;
        denseUpdatePhysical(result, physical, hasPhysical);
        return true;
    }
    return denseFailure(result, "allocation-failure");
}

inline void denseRelease(uint32_t map[128], bool used[124], uint32_t logical,
                         const DenseInput& input) {
    if (logical < input.reservedRegisterCount || map[logical] == kNoPhysicalRegister) return;
    used[map[logical]] = false;
    map[logical] = kNoPhysicalRegister;
}

inline bool denseRewriteSource(uint32_t& word, uint32_t shift, uint32_t physical,
                               DenseResult& result) {
    if (physical > 0x7fu) return denseFailure(result, "rewrite-failure");
    word = (word & ~(0x1ffu << shift)) | (physical << shift);
    return true;
}

inline bool denseRunAllocation(const DenseInput& input, DenseResult& result,
                               bool rewrite) {
    uint32_t lastRead[128];
    bool hasRead[128];
    uint32_t map[128];
    bool used[124] = {};
    for (uint32_t logical = 0; logical < 128; ++logical) {
        lastRead[logical] = 0;
        hasRead[logical] = false;
        map[logical] = kNoPhysicalRegister;
    }
    const size_t instructionCount = input.aluWordCount / 2;

    // First pass: a value is live until the group containing its last source
    // read.  Exports are a read after the final ALU group.
    for (size_t group = 0; group < input.groupCount; ++group) {
        const size_t begin = input.groupOffsets[group];
        const size_t end = group + 1 < input.groupCount ? input.groupOffsets[group + 1] : instructionCount;
        if (begin >= end || end > instructionCount) return denseFailure(result, "malformed-groups");
        for (size_t instruction = begin; instruction < end; ++instruction) {
            const uint32_t word0 = input.aluWords[2 * instruction];
            const uint32_t word1 = input.aluWords[2 * instruction + 1];
            const uint32_t selectors[3] = {
                word0 & 0x1ffu, (word0 >> 13) & 0x1ffu,
                denseIsOp3(word1) ? word1 & 0x1ffu : 0,
            };
            const uint32_t relatives[3] = {
                (word0 >> 9) & 1u, (word0 >> 22) & 1u,
                denseIsOp3(word1) ? (word1 >> 9) & 1u : 0,
            };
            for (uint32_t source = 0; source < (denseIsOp3(word1) ? 3u : 2u); ++source) {
                if (denseIsGprSelector(selectors[source])) {
                    if (relatives[source]) return denseFailure(result, "relative-gpr-source");
                    hasRead[selectors[source]] = true;
                    lastRead[selectors[source]] = uint32_t(group);
                }
            }
        }
    }
    for (size_t exportIndex = 0; exportIndex < input.exportCount; ++exportIndex) {
        const uint32_t logical = (input.exportWords[2 * exportIndex] >> 15) & 0x7fu;
        hasRead[logical] = true;
        lastRead[logical] = uint32_t(input.groupCount);
    }

    bool hasPhysical = false;
    for (size_t group = 0; group < input.groupCount; ++group) {
        for (uint32_t logical = input.reservedRegisterCount; logical < 124u; ++logical) {
            if (map[logical] != kNoPhysicalRegister &&
                (!hasRead[logical] || lastRead[logical] < group))
                denseRelease(map, used, logical, input);
        }
        const size_t begin = input.groupOffsets[group];
        const size_t end = group + 1 < input.groupCount ? input.groupOffsets[group + 1] : instructionCount;
        for (size_t instruction = begin; instruction < end; ++instruction) {
            uint32_t word0 = input.aluWords[2 * instruction];
            uint32_t word1 = input.aluWords[2 * instruction + 1];
            const bool op3 = denseIsOp3(word1);
            const uint32_t selectors[3] = {
                word0 & 0x1ffu, (word0 >> 13) & 0x1ffu, op3 ? word1 & 0x1ffu : 0,
            };
            const uint32_t shifts[3] = {0, 13, 0};
            const bool relative[3] = {
                ((word0 >> 9) & 1u) != 0, ((word0 >> 22) & 1u) != 0,
                op3 && ((word1 >> 9) & 1u) != 0,
            };
            for (uint32_t source = 0; source < (op3 ? 3u : 2u); ++source) {
                if (!denseIsGprSelector(selectors[source])) continue;
                uint32_t physical = 0;
                if (relative[source] || !denseAcquire(map, used, selectors[source], input,
                                                       result, hasPhysical, physical)) return false;
                if (rewrite) {
                    if (source == 2) {
                        if (!denseRewriteSource(word1, 0, physical, result)) return false;
                    } else if (!denseRewriteSource(word0, shifts[source], physical, result)) {
                        return false;
                    }
                }
            }
            if (op3 || ((word1 >> 4) & 1u) != 0) {
                const uint32_t logical = (word1 >> 21) & 0x7fu;
                uint32_t physical = 0;
                if (!denseAcquire(map, used, logical, input, result, hasPhysical, physical)) return false;
                if (rewrite) {
                    word1 = (word1 & ~(0x7fu << 21)) | (physical << 21);
                }
            }
            if (rewrite) {
                input.aluWords[2 * instruction] = word0;
                input.aluWords[2 * instruction + 1] = word1;
            }
        }
        for (uint32_t logical = input.reservedRegisterCount; logical < 124u; ++logical) {
            if (map[logical] != kNoPhysicalRegister &&
                (!hasRead[logical] || lastRead[logical] <= group))
                denseRelease(map, used, logical, input);
        }
    }

    for (size_t exportIndex = 0; exportIndex < input.exportCount; ++exportIndex) {
        uint32_t word0 = input.exportWords[2 * exportIndex];
        const uint32_t logical = (word0 >> 15) & 0x7fu;
        uint32_t physical = 0;
        if (!denseAcquire(map, used, logical, input, result, hasPhysical, physical)) return false;
        if (rewrite) {
            word0 = (word0 & ~(0x7fu << 15)) | (physical << 15);
            input.exportWords[2 * exportIndex] = word0;
        }
    }
    result.physicalMaximum = hasPhysical ? result.physicalMaximum : 0;
    result.declaredCount = hasPhysical ? result.physicalMaximum + 1 : 0;
    if (result.declaredCount > kVsDenseHardMaxCount)
        return denseFailure(result, "hard-ceiling");
    result.accepted = true;
    result.reason = result.declaredCount <= kVsDenseTargetCount ? "target" : "hard-ceiling-path";
    return true;
}

inline bool measureDenseProgram(const DenseInput& input, DenseResult& result) {
    denseResetResult(result);
    if (!denseValidateGroups(input, result)) return false;
    bool hasPhysical = false;
    result.physicalMaximum = 0;
    for (size_t instruction = 0; instruction < input.aluWordCount / 2; ++instruction) {
        const uint32_t word0 = input.aluWords[2 * instruction];
        const uint32_t word1 = input.aluWords[2 * instruction + 1];
        const uint32_t selectors[3] = {
            word0 & 0x1ffu, (word0 >> 13) & 0x1ffu,
            denseIsOp3(word1) ? word1 & 0x1ffu : 0,
        };
        const uint32_t count = denseIsOp3(word1) ? 3u : 2u;
        for (uint32_t source = 0; source < count; ++source)
            if (denseIsGprSelector(selectors[source])) denseUpdatePhysical(result, selectors[source], hasPhysical);
        if (denseIsOp3(word1) || ((word1 >> 4) & 1u) != 0)
            denseUpdatePhysical(result, (word1 >> 21) & 0x7fu, hasPhysical);
    }
    for (size_t exportIndex = 0; exportIndex < input.exportCount; ++exportIndex)
        denseUpdatePhysical(result, (input.exportWords[2 * exportIndex] >> 15) & 0x7fu, hasPhysical);
    result.declaredCount = hasPhysical ? result.physicalMaximum + 1 : 0;
    if (result.declaredCount > kVsDenseHardMaxCount) return denseFailure(result, "hard-ceiling");
    result.accepted = true;
    result.reason = result.declaredCount <= kVsDenseTargetCount ? "target" : "hard-ceiling-path";
    return true;
}

inline bool compactGeneratedVertexGprs(const DenseInput& input, DenseResult& result) {
    denseResetResult(result);
    if (!denseValidateGroups(input, result)) return false;
    const uint32_t logicalMaximum = result.logicalMaximum;
    DenseResult planned = {};
    denseResetResult(planned);
    if (!denseRunAllocation(input, planned, false)) {
        result = planned;
        return false;
    }
    DenseResult rewritten = {};
    denseResetResult(rewritten);
    if (!denseRunAllocation(input, rewritten, true)) {
        result = rewritten;
        return false;
    }
    DenseResult checked = {};
    if (!measureDenseProgram(input, checked) ||
        checked.declaredCount != planned.declaredCount ||
        checked.physicalMaximum > kVsDenseHardMaxCount - 1) {
        checked.accepted = false;
        checked.reason = "rewrite-self-check";
        result = checked;
        return false;
    }
    checked.logicalMaximum = logicalMaximum;
    result = checked;
    return true;
}

} // namespace smg_vs_dense

#endif
