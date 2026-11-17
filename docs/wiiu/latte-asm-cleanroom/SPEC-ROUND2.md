# Round 2: three more names (needed by the Galaxy renderer)

Add these to `latte_asm.h`. Their encodings are NOT in the R700 document; use
AMD's "Evergreen-Family Instruction Set Architecture" document, now in
`ref/evergreen-isa.txt` (text of `ref/evergreen-isa.pdf`), sections for
CF_ALU_WORD0_EXT and CF_ALU_WORD1_EXT (pages 10-11 to 10-13) and the
ALU_EXTENDED instruction. The same clean-room rules from SPEC.md apply.

- `CF_ALU_EXT_WORD0(index0, index1, index2, index3, bank2, bank3, mode2)`:
  the first dword of the ALU_EXTENDED control-flow instruction
  (CF_ALU_WORD0_EXT): `index0..index3` are the kcache index modes of constant
  banks 0..3, `bank2`/`bank3` the kcache banks 2 and 3, `mode2` the kcache mode
  of bank 2. Byte-swapped with to_LE like every other word.
- `CF_ALU_EXT_WORD1(mode3, addr2, addr3, inst)`: the second dword
  (CF_ALU_WORD1_EXT): kcache mode of bank 3, kcache addresses 2 and 3, and the
  CF instruction field; sets the BARRIER bit like the other CF words.
- `CF_INST_ALU_EXT`: the CF opcode of ALU_EXTENDED.

Fields the document reserves or that are not listed stay zero. Add checks for
the three to test_latte_asm.c.
