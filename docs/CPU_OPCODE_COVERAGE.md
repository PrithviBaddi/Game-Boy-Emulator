# SM83 opcode coverage

Checked by `cpu_coverage` against the legal/illegal split in
[Pan Docs](https://gbdev.io/pandocs/CPU_Instruction_Set.html). The test walks
every base opcode and every CB opcode. It does not trust a count by itself:
the 11 illegal opcodes are the Pan Docs list, and every other opcode must
return a defined result.

| Set | Count | Result |
| --- | ---: | --- |
| Legal base opcodes | 245 | Implemented. `HALT` returns `GB_STEP_HALTED`, `STOP` returns `GB_STEP_STOPPED`, and the rest return `GB_STEP_OK`. |
| Illegal base opcodes | 11 | `D3 DB DD E3 E4 EB EC ED F4 FC FD`. Each locks the CPU, leaves PC at the opcode, and reports `GB_STEP_ILLEGAL`. None of them run as a real instruction. |
| CB opcodes | 256 | Implemented for registers and `(HL)`: rotate, shift, SWAP, BIT, RES, and SET. |

`GB_STEP_UNSUPPORTED` remains in the API for a legal opcode that has not been
written yet. After this stage the coverage test fails if any legal opcode
still returns it.

Cycles from `gb_cpu_step` are T-cycles. `(HL)` CB shifts, rotates, RES, and
SET take 16. `BIT b,(HL)` takes 12. Register CB operations take 8.
