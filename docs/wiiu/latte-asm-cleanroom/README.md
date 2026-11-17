# How `platform/wiiu/latte_asm.h` was written

`latte_asm.h` replaces `gx2_shader_inl.h`, whose Latte shader macros came from
RetroArch's Wii U driver (GNU GPL v3 or later) through libultraship's copy.

1. A specification was written that names the macros the renderer needs, their
   parameters and their behaviour, without any of the old code: `SPEC.md`.
2. A separate writer produced `latte_asm.h` from that specification and AMD's
   public "R700-Family Instruction Set Architecture" document only (pages cited
   per instruction word in the commit), in an isolated repository, with no
   access to RetroArch, libultraship or any other shader assembler.
3. Black-box acceptance tests compared the instruction words both headers
   produce for 17,168 calls. Three differences came back to the writer as
   corrections that cite the ISA document: `SPEC-FIXES.md`. After them every
   in-range input produces the same words. The only calls that still differ
   pass an instruction number above 15 to `CF_ALU_WORD1`, whose field is
   4 bits wide: the new header masks it to the field, the old one let it spill
   into the neighbouring bits. Sunshine never passes such a value.
4. A second round added the extended ALU control-flow words that the Super
   Mario Galaxy port's renderer uses, from AMD's "Evergreen Family Instruction
   Set Architecture" document: `SPEC-ROUND2.md`.
5. A diagnostic Sunshine build ran the old and new macros side by side and
   compared every result through Bianco Hills and Ricco Harbor in the Cemu
   emulator. It generated 206 TEV pixel shaders and 121 vertex programs and
   reported no difference.
