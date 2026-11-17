# Corrections after black-box testing (round 1)

Acceptance testing compared the header's output values with the behaviour the
renderer needs. Three corrections, all checkable against the ISA document:

1. ALU_WORD1_OP2: the ISA (section 10.2, ALU_WORD1_OP2 table) gives ALU_INST
   as ONE 11-bit field at [17:7]; there is no separate bit at position 7.
   Put the OP2 opcode in [17:7]. The `encoding` parameter selects the upper
   three bits of that field, [17:15] (zero for every OP2 opcode we use), so it
   is OR-ed in at bit 15. OP2 opcode values are the ISA's OP2 enum values.
2. Suffix tokens apply to a uint64_t whose two dwords are ALREADY byte-swapped
   by to_LE (see "Conventions"). ALU_LAST, CLAMP and NO_BARRIER must therefore
   set/clear the byte-swapped position of their bit, exactly as VALID_PIX and
   END_OF_PROGRAM already (correctly) do: LAST is bit 31 of the first dword,
   CLAMP bit 31 of the second dword, BARRIER bit 31 of the second dword.
   Building them as `to_QWORD(to_LE(...), to_LE(...))` expressions is fine.
3. ALU_REL_CONST_OP2 / ALU_MOV_REL_CONST: callers pass `constReg` as a full
   source-select value that already includes the constant-file base, e.g.
   `ALU_MOV_REL_CONST(__, _x, _GX2_C(7), _y)`. Use it as the select directly;
   do not add the base again.

Out-of-range inputs (for example a CF_INST wider than its field) may be masked;
that is fine. Keep all other behaviour unchanged.
