# Latte (R700) shader-instruction macros: clean-room specification

Write ONE C header, `latte_asm.h`, in this directory. It is used by a Wii U
renderer that hand-assembles small shader programs as arrays of `uint64_t`.

## Clean-room rules (read first)

- Your ONLY technical source is AMD's "R700-Family Instruction Set
  Architecture" document in `ref/` (`r700-isa.txt`, text of `r700-isa.pdf`).
  Field positions, widths and opcode numbers must come from that document.
- Do NOT read, search for or open any other shader-assembler code on this
  machine or the internet: no RetroArch, libultraship, Shipwright, 2ship,
  sm64ex, decaf-emu, Cemu, Mesa/r600, wut or any file named
  `gx2_shader_inl.h`, `gx2_shader_gen*`, `gfx_gx2*`. Do not run `find`/`grep`
  outside this directory. If you are unsure whether a source is allowed, it is
  not.
- Write the header in your own style. Add the file header:
  `SPDX-License-Identifier: MIT`, `Copyright (c) 2026 utking`, and one line
  saying it was written from AMD's R700-Family ISA document.

## Conventions the callers depend on

- Every instruction dword is built as a host `uint32_t` and then byte-swapped
  with `__builtin_bswap32` (the host is big-endian PowerPC; the GPU reads
  little-endian dwords). Provide `to_LE(x)` for that swap.
- `to_QWORD(w0, w1)` combines two (already swapped) dwords into a `uint64_t`
  with `w0` in the HIGH 32 bits, so that storing it on the big-endian host puts
  `w0` first in memory.
- Some tokens are written as SUFFIXES after a macro call or argument, without a
  comma, e.g. `ALU_MUL(...) ALU_LAST`, `EXP_DONE(...) END_OF_PROGRAM`,
  `CALL_FS NO_BARRIER`, `_R(2) _NEG`. They therefore expand to `| <value>` or
  `& <value>` expressions.
- Source-select arguments of the ALU convenience macros carry flags above the
  9-bit select: bit 12 means "negate this operand" and bit 13 means "absolute
  value of this operand" (OP2 forms only). The OP2 convenience macros put the
  low 13 bits of each select into ALU_WORD0 (so bit 12 lands on the operand's
  NEG field) and route bit 13 to that operand's ABS field in ALU_WORD1_OP2.
  The OP3 form passes selects unmasked.
- A destination GPR argument may carry the flag `__` (value 0x80) meaning
  "do not write": the OP2 macros then clear WRITE_MASK (otherwise WRITE_MASK
  is 1) and only the low 7 bits of the GPR number are encoded.

## Names to provide (exact spelling and parameter order)

Raw instruction words (each returns one swapped dword; every parameter goes
into the ISA field of the same meaning; fields not listed are zero; the
control-flow words set the BARRIER bit):

- `CF_DWORD0(addr)`; `CF_DWORD1(popCount, cfConst, cond, count, callCount, inst)` (CF_WORD0/1)
- `CF_ALU_WORD0(addr, kcacheBank0, kcacheBank1, kcacheMode0)`;
  `CF_ALU_WORD1(kcacheMode1, kcacheAddr0, kcacheAddr1, count, altConst, inst)`
- `CF_EXP_WORD0(dstReg_and_type, srcReg, srcRel, indexGpr, elemSize)`;
  `CF_EXP_WORD1(srcSelX, srcSelY, srcSelZ, srcSelW, validPixelMode, inst)`
  (CF_ALLOC_EXPORT_WORD0 / WORD1_SWIZ; `dstReg_and_type` is already the
  ARRAY_BASE and TYPE fields combined, see POS/PARAM/PIX)
- `ALU_WORD0(src0Sel, src0Rel, src0Chan, src0Neg, src1Sel, src1Rel, src1Chan, src1Neg, indexMode, predSel)`
- `ALU_WORD1_OP2(src0Abs, src1Abs, updateExecuteMask, updatePred, writeMask, omod, inst, encoding, bankSwizzle, dstGpr, dstRel, dstChan, clamp)`
- `ALU_WORD1_OP3(src2Sel, src2Rel, src2Chan, src2Neg, inst, bankSwizzle, dstGpr, dstRel, dstChan, clamp)`
- `TEX_WORD0(inst, bcFracMode, fetchWholeQuad, resourceID, srcReg, srcRel, altConst)`
- `TEX_WORD1(dstReg, dstRel, dstSelX, dstSelY, dstSelZ, dstSelW, lodBias, coordTypeX, coordTypeY, coordTypeZ, coordTypeW)`
- `TEX_WORD2(offsetX, offsetY, offsetZ, samplerID, srcSelX, srcSelY, srcSelZ, srcSelW)`

Constants:

- Channel/swizzle selects `_x _y _z _w _0 _1` (the ISA's X, Y, Z, W, 0, 1
  select codes), `__` (0x80, see above), `_R(x)` (GPR x, the number itself).
- ALU source selects: `ALU_SRC_0`, `ALU_SRC_1`, `ALU_SRC_0_5`,
  `ALU_SRC_LITERAL`, `ALU_SRC_PV`, `ALU_SRC_PS` (the ISA's inline-constant /
  literal / previous-vector / previous-scalar select codes),
  `ALU_SRC_CONST_FILE_BASE` (first constant-file select, 256) and `_GX2_C(x)`
  (constant x).
- `_NEG` suffix (sets the negate flag, bit 12, of the select it follows).
- Output modifiers `ALU_OMOD_OFF`, `ALU_OMOD_M2`, `ALU_OMOD_M4`, `ALU_OMOD_D2`.
- `ALU_INDEX_AR_X` (the AR.x index mode).
- Opcodes (OP2 unless named OP3), values from the ISA:
  `ALU_OP2_INST_MUL_IEEE FRACT MOVA_INT MULLO_INT LSHL_INT ADD_INT AND_INT SETGE_INT LSHR_INT INT_TO_FLT FLT_TO_INT RECIP_IEEE RECIPSQRT_IEEE SQRT_IEEE DOT4 DOT4_IEEE MIN MAX SETE SETGT SETGE SETNE PRED_SETGT KILLGT`
  (each spelled `ALU_OP2_INST_<NAME>`), `ALU_OP3_INST_MULADD`,
  `ALU_OP3_INST_MULADD_D2`, `ALU_OP3_INST_CNDGT`.
- CF: `CF_INST_TEX`, `CF_INST_ALU`, `CF_INST_CALL_FS`, `CF_INST_EXP_DONE`,
  `CF_COND_ACTIVE`. TEX: `TEX_INST_SAMPLE`, `TEX_NORMALIZED` (normalized
  coordinate type). Export types: `EXPORT_TYPE_PIXEL`, `EXPORT_TYPE_POS`,
  `EXPORT_TYPE_PARAM`.

Suffix tokens (see conventions):

- `ALU_LAST`: sets the LAST bit of the ALU instruction (in its first dword).
- `CLAMP`: sets the CLAMP bit (second dword).
- `NO_BARRIER`: clears the BARRIER bit of a CF instruction (second dword).
- `VALID_PIX`: sets VALID_PIXEL_MODE of an export (second dword).
- `END_OF_PROGRAM`: sets END_OF_PROGRAM of a CF instruction (second dword).

Whole instructions (each returns `uint64_t` built with `to_QWORD`):

- `ALU_OP2(inst, dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan, omod)`:
  OP2 ALU instruction, no relative addressing, index mode 0, predicate select
  0, no execute-mask/predicate update, encoding 0, bank swizzle 0, no clamp;
  flags handled as described above.
- `ALU_OP2_REL(inst, dstGpr, dstChan, src0Sel, src0Chan, src0Rel, src1Sel, src1Chan, src1Rel, omod)`:
  same, with the two source REL bits given and index mode `ALU_INDEX_AR_X`.
- `ALU_OP3(inst, dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan, src2Sel, src2Chan)`:
  OP3 ALU instruction, all optional fields zero (dstGpr encoded as given).
- One-instruction helpers, all `ALU_OP2` with `ALU_OMOD_OFF` unless noted:
  `ALU_ADD`, `ALU_ADD_D2` (ADD with OMOD divide-by-2), `ALU_MUL`,
  `ALU_MUL_x2` (MUL, OMOD x2), `ALU_MUL_x4` (MUL, OMOD x4), `ALU_MUL_IEEE`,
  `ALU_ADD_INT`, `ALU_MULLO_INT`, `ALU_LSHL_INT`, `ALU_LSHR_INT`,
  `ALU_AND_INT`, `ALU_SETGE_INT`, `ALU_DOT4`, `ALU_DOT4_IEEE`, `ALU_MIN`,
  `ALU_MAX`, `ALU_SETE`, `ALU_SETGT`, `ALU_SETGE`, `ALU_SETNE`,
  `ALU_PRED_SETGT`, `ALU_KILLGT` — all with parameters
  `(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan)`;
  and the one-source forms `ALU_MOV`, `ALU_FLOOR`, `ALU_RNDNE`,
  `ALU_MOVA_INT`, `ALU_FLT_TO_INT`, `ALU_INT_TO_FLT`, `ALU_RECIP_IEEE`,
  `ALU_RECIPSQRT_IEEE`, `ALU_SQRT_IEEE` with `(dstGpr, dstChan, src0Sel, src0Chan)`,
  whose second source is `ALU_SRC_0` channel 0.
- OP3 helpers `ALU_MULADD`, `ALU_MULADD_D2`, `ALU_CNDGT` with
  `(dstGpr, dstChan, src0Sel, src0Chan, src1Sel, src1Chan, src2Sel, src2Chan)`.
- `ALU_REL_CONST_OP2(inst, dstGpr, dstChan, constReg, srcChan)`: OP2 whose
  first source is `constReg` read relative to AR.x (src0 REL = 1) and whose
  second source is `ALU_SRC_0` channel 0, not relative; `ALU_MOV_REL_CONST(dstGpr, dstChan, constReg, srcChan)`
  is that with the MOV opcode.
- Exports: `POS(id)`, `PARAM(id)`, `PIX(id)` give the `dstReg_and_type`
  value: the export ARRAY_BASE plus the TYPE field in its word-0 position.
  Position exports start at array base 60 (POS(id) = base 60 + id); PARAM and
  PIX use `id` as the base. `POS0` = `POS(0)`, `PIX0` = `PIX(0)`, `PIX_Z` =
  `PIX(61)` (computed-depth export).
- `EXP(dstReg_and_type, srcReg, srcSelX, srcSelY, srcSelZ, srcSelW)` and
  `EXP_DONE(...)` (same parameters): an EXPORT and an EXPORT_DONE CF
  instruction, srcRel/indexGpr/elemSize/validPixelMode 0.
- `CALL_FS`: a CALL_FS CF instruction with every field 0 (barrier set).
- `TEX(addr, cnt)`: a TEX clause CF instruction at `addr` holding `cnt`
  instructions (the ISA stores count minus one), condition ACTIVE.
- `ALU(addr, cnt)`: an ALU clause CF instruction at `addr` with `cnt` slots
  (count minus one), no kcache.
- `TEX_SAMPLE(dstReg, dstSelX, dstSelY, dstSelZ, dstSelW, srcReg, srcSelX, srcSelY, srcSelZ, srcSelW, resourceID, samplerID)`:
  expands to TWO comma-separated `uint64_t` values (a 128-bit fetch
  instruction for an array initializer): first `to_QWORD(TEX_WORD0, TEX_WORD1)`,
  then `to_QWORD(TEX_WORD2, 0)`. SAMPLE instruction, all four coordinate types
  normalized, no LOD bias, no offsets, no relative addressing. NOTE: existing
  callers depend on the source swizzle being FIXED to (x, y, 0, x) — the
  srcSel arguments are accepted but ignored. Keep that and comment it.

The header must include `<stdint.h>`, use an include guard, and compile
cleanly as C and C++ with `-Wall -Wextra`. Fully parenthesise macro arguments.
Also write `test_latte_asm.c`: a few self-checks of field placement you derive
from the ISA document (build it with `cc -Wall -Wextra`), and run it.
