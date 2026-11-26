/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 utking */
/* Written from AMD's R700-Family Instruction Set Architecture document. */

#ifndef LATTE_ASM_H
#define LATTE_ASM_H

#include <stdint.h>

#define to_LE(x) (__builtin_bswap32((uint32_t)(x)))
#define to_QWORD(w0, w1) \
    ((((uint64_t)(w0)) << 32) | ((uint64_t)(w1)))

/* Channel and destination-select values. */
#define _x UINT32_C(0)
#define _y UINT32_C(1)
#define _z UINT32_C(2)
#define _w UINT32_C(3)
#define _0 UINT32_C(4)
#define _1 UINT32_C(5)
#define __ UINT32_C(0x80)
#define _R(x) ((uint32_t)(x))

/* ALU source-select values. */
#define ALU_SRC_0 UINT32_C(248)
#define ALU_SRC_1 UINT32_C(249)
#define ALU_SRC_0_5 UINT32_C(252)
#define ALU_SRC_LITERAL UINT32_C(253)
#define ALU_SRC_PV UINT32_C(254)
#define ALU_SRC_PS UINT32_C(255)
#define ALU_SRC_CONST_FILE_BASE UINT32_C(256)
#define _GX2_C(x) (ALU_SRC_CONST_FILE_BASE + (uint32_t)(x))

/* Source-select modifier suffix.  ABS is carried in bit 13 by callers. */
#define _NEG | UINT32_C(0x1000)

/* ALU output modifiers and index modes. */
#define ALU_OMOD_OFF UINT32_C(0)
#define ALU_OMOD_M2 UINT32_C(1)
#define ALU_OMOD_M4 UINT32_C(2)
#define ALU_OMOD_D2 UINT32_C(3)
#define ALU_INDEX_AR_X UINT32_C(0)

/* ALU instruction opcodes. */
#define ALU_OP2_INST_ADD UINT32_C(0)
#define ALU_OP2_INST_MUL UINT32_C(1)
#define ALU_OP2_INST_MUL_IEEE UINT32_C(2)
#define ALU_OP2_INST_FRACT UINT32_C(16)
#define ALU_OP2_INST_RNDNE UINT32_C(19)
#define ALU_OP2_INST_FLOOR UINT32_C(20)
#define ALU_OP2_INST_MOVA_INT UINT32_C(24)
#define ALU_OP2_INST_MOV UINT32_C(25)
#define ALU_OP2_INST_ADD_INT UINT32_C(52)
#define ALU_OP2_INST_AND_INT UINT32_C(48)
#define ALU_OP2_INST_SETGE_INT UINT32_C(60)
#define ALU_OP2_INST_LSHR_INT UINT32_C(113)
#define ALU_OP2_INST_LSHL_INT UINT32_C(114)
#define ALU_OP2_INST_MULLO_INT UINT32_C(115)
#define ALU_OP2_INST_INT_TO_FLT UINT32_C(108)
#define ALU_OP2_INST_FLT_TO_INT UINT32_C(107)
#define ALU_OP2_INST_RECIP_IEEE UINT32_C(102)
#define ALU_OP2_INST_RECIPSQRT_IEEE UINT32_C(105)
#define ALU_OP2_INST_SQRT_IEEE UINT32_C(106)
#define ALU_OP2_INST_DOT4 UINT32_C(80)
#define ALU_OP2_INST_DOT4_IEEE UINT32_C(81)
#define ALU_OP2_INST_MIN UINT32_C(4)
#define ALU_OP2_INST_MAX UINT32_C(3)
#define ALU_OP2_INST_SETE UINT32_C(8)
#define ALU_OP2_INST_SETGT UINT32_C(9)
#define ALU_OP2_INST_SETGE UINT32_C(10)
#define ALU_OP2_INST_SETNE UINT32_C(11)
#define ALU_OP2_INST_PRED_SETGT UINT32_C(33)
#define ALU_OP2_INST_KILLGT UINT32_C(45)

#define ALU_OP3_INST_MULADD UINT32_C(16)
#define ALU_OP3_INST_MULADD_D2 UINT32_C(19)
#define ALU_OP3_INST_CNDGT UINT32_C(25)

/* Control-flow and texture instruction values. */
#define CF_INST_TEX UINT32_C(1)
#define CF_INST_ALU UINT32_C(8)
#define CF_INST_ALU_EXT UINT32_C(12)
#define CF_INST_CALL_FS UINT32_C(19)
#define CF_INST_EXP_DONE UINT32_C(40)
#define CF_INST_EXP UINT32_C(39)
#define CF_COND_ACTIVE UINT32_C(0)
#define TEX_INST_SAMPLE UINT32_C(16)
#define TEX_NORMALIZED UINT32_C(1)

/* Export types. */
#define EXPORT_TYPE_PIXEL UINT32_C(0)
#define EXPORT_TYPE_POS UINT32_C(1)
#define EXPORT_TYPE_PARAM UINT32_C(2)

/* Suffix operators for complete instruction words. */
/* These values address bits in the qword made from already-swapped dwords. */
#define ALU_LAST | to_QWORD(to_LE(UINT32_C(0x80000000)), to_LE(UINT32_C(0)))
#define CLAMP | to_QWORD(to_LE(UINT32_C(0)), to_LE(UINT32_C(0x80000000)))
#define NO_BARRIER & ~to_QWORD(to_LE(UINT32_C(0)), to_LE(UINT32_C(0x80000000)))
#define VALID_PIX | (UINT64_C(1) << 14)
#define END_OF_PROGRAM | (UINT64_C(1) << 13)

/* Raw CF formats.  CF instructions default to a synchronization barrier. */
#define CF_DWORD0(addr) \
    to_LE((uint32_t)(addr))
#define CF_DWORD1(popCount, cfConst, cond, count, callCount, inst) \
    to_LE(((((uint32_t)(popCount)) & UINT32_C(7)) << 0) | \
          ((((uint32_t)(cfConst)) & UINT32_C(31)) << 3) | \
          ((((uint32_t)(cond)) & UINT32_C(3)) << 8) | \
          ((((uint32_t)(count)) & UINT32_C(7)) << 10) | \
          (((((uint32_t)(count)) >> 3) & UINT32_C(1)) << 19) | \
          ((((uint32_t)(callCount)) & UINT32_C(63)) << 13) | \
          ((((uint32_t)(inst)) & UINT32_C(127)) << 23) | \
          UINT32_C(0x80000000))

#define CF_ALU_WORD0(addr, kcacheBank0, kcacheBank1, kcacheMode0) \
    to_LE(((((uint32_t)(addr)) & UINT32_C(0x3fffff)) << 0) | \
          ((((uint32_t)(kcacheBank0)) & UINT32_C(15)) << 22) | \
          ((((uint32_t)(kcacheBank1)) & UINT32_C(15)) << 26) | \
          ((((uint32_t)(kcacheMode0)) & UINT32_C(3)) << 30))
#define CF_ALU_WORD1(kcacheMode1, kcacheAddr0, kcacheAddr1, count, altConst, inst) \
    to_LE(((((uint32_t)(kcacheMode1)) & UINT32_C(3)) << 0) | \
          ((((uint32_t)(kcacheAddr0)) & UINT32_C(255)) << 2) | \
          ((((uint32_t)(kcacheAddr1)) & UINT32_C(255)) << 10) | \
          ((((uint32_t)(count)) & UINT32_C(127)) << 18) | \
          ((((uint32_t)(altConst)) & UINT32_C(1)) << 25) | \
          ((((uint32_t)(inst)) & UINT32_C(15)) << 26) | \
          UINT32_C(0x80000000))
#define CF_ALU_EXT_WORD0(index0, index1, index2, index3, bank2, bank3, mode2) \
    to_LE(((((uint32_t)(index0)) & UINT32_C(3)) << 4) | \
          ((((uint32_t)(index1)) & UINT32_C(3)) << 6) | \
          ((((uint32_t)(index2)) & UINT32_C(3)) << 8) | \
          ((((uint32_t)(index3)) & UINT32_C(3)) << 10) | \
          ((((uint32_t)(bank2)) & UINT32_C(15)) << 22) | \
          ((((uint32_t)(bank3)) & UINT32_C(15)) << 26) | \
          ((((uint32_t)(mode2)) & UINT32_C(3)) << 30))
#define CF_ALU_EXT_WORD1(mode3, addr2, addr3, inst) \
    to_LE(((((uint32_t)(mode3)) & UINT32_C(3)) << 0) | \
          ((((uint32_t)(addr2)) & UINT32_C(255)) << 2) | \
          ((((uint32_t)(addr3)) & UINT32_C(255)) << 10) | \
          ((((uint32_t)(inst)) & UINT32_C(15)) << 26) | \
          UINT32_C(0x80000000))

#define CF_EXP_WORD0(dstReg_and_type, srcReg, srcRel, indexGpr, elemSize) \
    to_LE(((((uint32_t)(dstReg_and_type)) & UINT32_C(0x7fff)) << 0) | \
          ((((uint32_t)(srcReg)) & UINT32_C(127)) << 15) | \
          ((((uint32_t)(srcRel)) & UINT32_C(1)) << 22) | \
          ((((uint32_t)(indexGpr)) & UINT32_C(127)) << 23) | \
          ((((uint32_t)(elemSize)) & UINT32_C(3)) << 30))
#define CF_EXP_WORD1(srcSelX, srcSelY, srcSelZ, srcSelW, validPixelMode, inst) \
    to_LE(((((uint32_t)(srcSelX)) & UINT32_C(7)) << 0) | \
          ((((uint32_t)(srcSelY)) & UINT32_C(7)) << 3) | \
          ((((uint32_t)(srcSelZ)) & UINT32_C(7)) << 6) | \
          ((((uint32_t)(srcSelW)) & UINT32_C(7)) << 9) | \
          ((((uint32_t)(validPixelMode)) & UINT32_C(1)) << 22) | \
          ((((uint32_t)(inst)) & UINT32_C(127)) << 23) | \
          UINT32_C(0x80000000))

/* Raw ALU formats. */
#define ALU_WORD0(src0Sel, src0Rel, src0Chan, src0Neg, src1Sel, src1Rel, src1Chan, src1Neg, indexMode, predSel) \
    to_LE(((((uint32_t)(src0Sel)) & UINT32_C(0x1ff)) << 0) | \
          ((((uint32_t)(src0Rel)) & UINT32_C(1)) << 9) | \
          ((((uint32_t)(src0Chan)) & UINT32_C(3)) << 10) | \
          (((((uint32_t)(src0Sel)) >> 12) & UINT32_C(1)) << 12) | \
          ((((uint32_t)(src0Neg)) & UINT32_C(1)) << 12) | \
          ((((uint32_t)(src1Sel)) & UINT32_C(0x1ff)) << 13) | \
          ((((uint32_t)(src1Rel)) & UINT32_C(1)) << 22) | \
          ((((uint32_t)(src1Chan)) & UINT32_C(3)) << 23) | \
          (((((uint32_t)(src1Sel)) >> 12) & UINT32_C(1)) << 25) | \
          ((((uint32_t)(src1Neg)) & UINT32_C(1)) << 25) | \
          ((((uint32_t)(indexMode)) & UINT32_C(7)) << 26) | \
          ((((uint32_t)(predSel)) & UINT32_C(3)) << 29))
#define ALU_WORD1_OP2(src0Abs, src1Abs, updateExecuteMask, updatePred, writeMask, omod, inst, encoding, bankSwizzle, dstGpr, dstRel, dstChan, clamp) \
    to_LE(((((uint32_t)(src0Abs)) & UINT32_C(1)) << 0) | \
          ((((uint32_t)(src1Abs)) & UINT32_C(1)) << 1) | \
          ((((uint32_t)(updateExecuteMask)) & UINT32_C(1)) << 2) | \
          ((((uint32_t)(updatePred)) & UINT32_C(1)) << 3) | \
          ((((uint32_t)(writeMask)) & UINT32_C(1)) << 4) | \
          ((((uint32_t)(omod)) & UINT32_C(3)) << 5) | \
          ((((uint32_t)(encoding)) & UINT32_C(7)) << 15) | \
          ((((uint32_t)(inst)) & UINT32_C(127)) << 7) | \
          ((((uint32_t)(bankSwizzle)) & UINT32_C(7)) << 18) | \
          ((((uint32_t)(dstGpr)) & UINT32_C(127)) << 21) | \
          ((((uint32_t)(dstRel)) & UINT32_C(1)) << 28) | \
          ((((uint32_t)(dstChan)) & UINT32_C(3)) << 29) | \
          ((((uint32_t)(clamp)) & UINT32_C(1)) << 31))
#define ALU_WORD1_OP3(src2Sel, src2Rel, src2Chan, src2Neg, inst, bankSwizzle, dstGpr, dstRel, dstChan, clamp) \
    to_LE(((((uint32_t)(src2Sel)) & UINT32_C(0x1ff)) << 0) | \
          ((((uint32_t)(src2Rel)) & UINT32_C(1)) << 9) | \
          ((((uint32_t)(src2Chan)) & UINT32_C(3)) << 10) | \
          (((((uint32_t)(src2Sel)) >> 12) & UINT32_C(1)) << 12) | \
          ((((uint32_t)(src2Neg)) & UINT32_C(1)) << 12) | \
          ((((uint32_t)(inst)) & UINT32_C(31)) << 13) | \
          ((((uint32_t)(bankSwizzle)) & UINT32_C(7)) << 18) | \
          ((((uint32_t)(dstGpr)) & UINT32_C(127)) << 21) | \
          ((((uint32_t)(dstRel)) & UINT32_C(1)) << 28) | \
          ((((uint32_t)(dstChan)) & UINT32_C(3)) << 29) | \
          ((((uint32_t)(clamp)) & UINT32_C(1)) << 31))

/* Raw texture formats. */
#define TEX_WORD0(inst, bcFracMode, fetchWholeQuad, resourceID, srcReg, srcRel, altConst) \
    to_LE(((((uint32_t)(inst)) & UINT32_C(31)) << 0) | \
          ((((uint32_t)(bcFracMode)) & UINT32_C(1)) << 5) | \
          ((((uint32_t)(fetchWholeQuad)) & UINT32_C(1)) << 7) | \
          ((((uint32_t)(resourceID)) & UINT32_C(255)) << 8) | \
          ((((uint32_t)(srcReg)) & UINT32_C(127)) << 16) | \
          ((((uint32_t)(srcRel)) & UINT32_C(1)) << 23) | \
          ((((uint32_t)(altConst)) & UINT32_C(1)) << 24))
#define TEX_WORD1(dstReg, dstRel, dstSelX, dstSelY, dstSelZ, dstSelW, lodBias, coordTypeX, coordTypeY, coordTypeZ, coordTypeW) \
    to_LE(((((uint32_t)(dstReg)) & UINT32_C(127)) << 0) | \
          ((((uint32_t)(dstRel)) & UINT32_C(1)) << 7) | \
          ((((uint32_t)(dstSelX)) & UINT32_C(7)) << 9) | \
          ((((uint32_t)(dstSelY)) & UINT32_C(7)) << 12) | \
          ((((uint32_t)(dstSelZ)) & UINT32_C(7)) << 15) | \
          ((((uint32_t)(dstSelW)) & UINT32_C(7)) << 18) | \
          ((((uint32_t)(lodBias)) & UINT32_C(127)) << 21) | \
          ((((uint32_t)(coordTypeX)) & UINT32_C(1)) << 28) | \
          ((((uint32_t)(coordTypeY)) & UINT32_C(1)) << 29) | \
          ((((uint32_t)(coordTypeZ)) & UINT32_C(1)) << 30) | \
          ((((uint32_t)(coordTypeW)) & UINT32_C(1)) << 31))
#define TEX_WORD2(offsetX, offsetY, offsetZ, samplerID, srcSelX, srcSelY, srcSelZ, srcSelW) \
    to_LE(((((uint32_t)(offsetX)) & UINT32_C(31)) << 0) | \
          ((((uint32_t)(offsetY)) & UINT32_C(31)) << 5) | \
          ((((uint32_t)(offsetZ)) & UINT32_C(31)) << 10) | \
          ((((uint32_t)(samplerID)) & UINT32_C(31)) << 15) | \
          ((((uint32_t)(srcSelX)) & UINT32_C(7)) << 20) | \
          ((((uint32_t)(srcSelY)) & UINT32_C(7)) << 23) | \
          ((((uint32_t)(srcSelZ)) & UINT32_C(7)) << 26) | \
          ((((uint32_t)(srcSelW)) & UINT32_C(7)) << 29))

/* Complete ALU instructions. */
#define ALU_OP2(inst, dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan, omod) \
    to_QWORD(ALU_WORD0((src0Sel), 0, (src0Chan), 0, (src1Sel), 0, (src1Chan), 0, 0, 0), \
             ALU_WORD1_OP2((((uint32_t)(src0Sel) >> 13) & 1), (((uint32_t)(src1Sel) >> 13) & 1), 0, 0, \
                            (((uint32_t)(dstGpr) & UINT32_C(0x80)) == 0), (omod), (inst), 0, 0, \
                            (dstGpr), 0, (dstChan), 0))
#define ALU_OP2_REL(inst, dstGpr, dstChan, src0Sel, src0Chan, src0Rel, src1Sel, src1Chan, src1Rel, omod) \
    to_QWORD(ALU_WORD0((src0Sel), (src0Rel), (src0Chan), 0, (src1Sel), (src1Rel), (src1Chan), 0, ALU_INDEX_AR_X, 0), \
             ALU_WORD1_OP2((((uint32_t)(src0Sel) >> 13) & 1), (((uint32_t)(src1Sel) >> 13) & 1), 0, 0, \
                            (((uint32_t)(dstGpr) & UINT32_C(0x80)) == 0), (omod), (inst), 0, 0, \
                            (dstGpr), 0, (dstChan), 0))
#define ALU_OP3(inst, dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan, src2Sel, src2Chan) \
    to_QWORD(ALU_WORD0((src0Sel), 0, (src0Chan), 0, (src1Sel), 0, (src1Chan), 0, 0, 0), \
             ALU_WORD1_OP3((src2Sel), 0, (src2Chan), 0, (inst), 0, (dstGpr), 0, (dstChan), 0))

#define ALU_ADD(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(0, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_ADD_D2(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(0, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_D2)
#define ALU_MUL(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_MUL, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_MUL_x2(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_MUL, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_M2)
#define ALU_MUL_x4(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_MUL, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_M4)
#define ALU_MUL_IEEE(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_MUL_IEEE, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_ADD_INT(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_ADD_INT, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_MULLO_INT(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_MULLO_INT, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_LSHL_INT(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_LSHL_INT, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_LSHR_INT(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_LSHR_INT, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_AND_INT(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_AND_INT, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_SETGE_INT(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_SETGE_INT, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_DOT4(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_DOT4, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_DOT4_IEEE(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_DOT4_IEEE, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_MIN(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_MIN, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_MAX(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_MAX, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_SETE(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_SETE, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_SETGT(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_SETGT, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_SETGE(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_SETGE, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_SETNE(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_SETNE, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_PRED_SETGT(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_PRED_SETGT, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)
#define ALU_KILLGT(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan) \
    ALU_OP2(ALU_OP2_INST_KILLGT, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), ALU_OMOD_OFF)

#define ALU_MOV(dstGpr, dstChan, src0Sel, src0Chan) \
    ALU_OP2(ALU_OP2_INST_MOV, (dstGpr), (dstChan), (src0Sel), (src0Chan), ALU_SRC_0, 0, ALU_OMOD_OFF)
#define ALU_FLOOR(dstGpr, dstChan, src0Sel, src0Chan) \
    ALU_OP2(ALU_OP2_INST_FLOOR, (dstGpr), (dstChan), (src0Sel), (src0Chan), ALU_SRC_0, 0, ALU_OMOD_OFF)
#define ALU_RNDNE(dstGpr, dstChan, src0Sel, src0Chan) \
    ALU_OP2(ALU_OP2_INST_RNDNE, (dstGpr), (dstChan), (src0Sel), (src0Chan), ALU_SRC_0, 0, ALU_OMOD_OFF)
#define ALU_MOVA_INT(dstGpr, dstChan, src0Sel, src0Chan) \
    ALU_OP2(ALU_OP2_INST_MOVA_INT, (dstGpr), (dstChan), (src0Sel), (src0Chan), ALU_SRC_0, 0, ALU_OMOD_OFF)
#define ALU_FLT_TO_INT(dstGpr, dstChan, src0Sel, src0Chan) \
    ALU_OP2(ALU_OP2_INST_FLT_TO_INT, (dstGpr), (dstChan), (src0Sel), (src0Chan), ALU_SRC_0, 0, ALU_OMOD_OFF)
#define ALU_INT_TO_FLT(dstGpr, dstChan, src0Sel, src0Chan) \
    ALU_OP2(ALU_OP2_INST_INT_TO_FLT, (dstGpr), (dstChan), (src0Sel), (src0Chan), ALU_SRC_0, 0, ALU_OMOD_OFF)
#define ALU_RECIP_IEEE(dstGpr, dstChan, src0Sel, src0Chan) \
    ALU_OP2(ALU_OP2_INST_RECIP_IEEE, (dstGpr), (dstChan), (src0Sel), (src0Chan), ALU_SRC_0, 0, ALU_OMOD_OFF)
#define ALU_RECIPSQRT_IEEE(dstGpr, dstChan, src0Sel, src0Chan) \
    ALU_OP2(ALU_OP2_INST_RECIPSQRT_IEEE, (dstGpr), (dstChan), (src0Sel), (src0Chan), ALU_SRC_0, 0, ALU_OMOD_OFF)
#define ALU_SQRT_IEEE(dstGpr, dstChan, src0Sel, src0Chan) \
    ALU_OP2(ALU_OP2_INST_SQRT_IEEE, (dstGpr), (dstChan), (src0Sel), (src0Chan), ALU_SRC_0, 0, ALU_OMOD_OFF)

#define ALU_MULADD(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan, src2Sel, src2Chan) \
    ALU_OP3(ALU_OP3_INST_MULADD, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), (src2Sel), (src2Chan))
#define ALU_MULADD_D2(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan, src2Sel, src2Chan) \
    ALU_OP3(ALU_OP3_INST_MULADD_D2, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), (src2Sel), (src2Chan))
#define ALU_CNDGT(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan, src2Sel, src2Chan) \
    ALU_OP3(ALU_OP3_INST_CNDGT, (dstGpr), (dstChan), (src0Sel), (src0Chan), (src1Sel), (src1Chan), (src2Sel), (src2Chan))

#define ALU_REL_CONST_OP2(inst, dstGpr, dstChan, constReg, srcChan) \
    ALU_OP2_REL((inst), (dstGpr), (dstChan), (constReg), (srcChan), 1, ALU_SRC_0, 0, 0, ALU_OMOD_OFF)
#define ALU_MOV_REL_CONST(dstGpr, dstChan, constReg, srcChan) \
    ALU_REL_CONST_OP2(ALU_OP2_INST_MOV, (dstGpr), (dstChan), (constReg), (srcChan))

/* Export destination encodings. */
#define POS(id) ((uint32_t)(60 + (uint32_t)(id)) | (EXPORT_TYPE_POS << 13))
#define PARAM(id) ((uint32_t)(id) | (EXPORT_TYPE_PARAM << 13))
#define PIX(id) ((uint32_t)(id) | (EXPORT_TYPE_PIXEL << 13))
#define POS0 POS(0)
#define PIX0 PIX(0)
#define PIX_Z PIX(61)

#define EXP(dstReg_and_type, srcReg, srcSelX, srcSelY, srcSelZ, srcSelW) \
    to_QWORD(CF_EXP_WORD0((dstReg_and_type), (srcReg), 0, 0, 0), \
             CF_EXP_WORD1((srcSelX), (srcSelY), (srcSelZ), (srcSelW), 0, CF_INST_EXP))
#define EXP_DONE(dstReg_and_type, srcReg, srcSelX, srcSelY, srcSelZ, srcSelW) \
    to_QWORD(CF_EXP_WORD0((dstReg_and_type), (srcReg), 0, 0, 0), \
             CF_EXP_WORD1((srcSelX), (srcSelY), (srcSelZ), (srcSelW), 0, CF_INST_EXP_DONE))

#define CALL_FS \
    to_QWORD(CF_DWORD0(0), CF_DWORD1(0, 0, CF_COND_ACTIVE, 0, 0, CF_INST_CALL_FS))
#define TEX(addr, cnt) \
    to_QWORD(CF_DWORD0(addr), CF_DWORD1(0, 0, CF_COND_ACTIVE, ((uint32_t)(cnt) - 1), 0, CF_INST_TEX))
#define ALU(addr, cnt) \
    to_QWORD(CF_ALU_WORD0((addr), 0, 0, 0), CF_ALU_WORD1(0, 0, 0, ((uint32_t)(cnt) - 1), 0, CF_INST_ALU))

/* The source swizzle is intentionally fixed to (x, y, 0, x), as existing
 * callers depend on it; the srcSel arguments are accepted but ignored. */
#define TEX_SAMPLE(dstReg, dstSelX, dstSelY, dstSelZ, dstSelW, srcReg, srcSelX, srcSelY, srcSelZ, srcSelW, resourceID, samplerID) \
    to_QWORD(TEX_WORD0(TEX_INST_SAMPLE, 0, 0, (resourceID), (srcReg), 0, 0), \
             TEX_WORD1((dstReg), 0, (dstSelX), (dstSelY), (dstSelZ), (dstSelW), 0, TEX_NORMALIZED, TEX_NORMALIZED, TEX_NORMALIZED, TEX_NORMALIZED)), \
    to_QWORD(TEX_WORD2(0, 0, 0, (samplerID), _x, _y, _0, _x), 0)

#endif /* LATTE_ASM_H */
