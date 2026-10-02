# Stage 2 checklist — DMG SM83 CPU

Cycles reported by `gb_cpu_step` are **T-cycles** (NOP = 4). That matches the
existing unit tests (`LD A,d8` = 8, `ADD A,r` = 4, `LD (a16),A` = 16) and the
usual 4 T-cycles per machine cycle. Opcode meaning, length, flags, and timing
follow [Pan Docs](https://gbdev.io/pandocs/), the
[SM83 opcode tables](https://gbdev.io/gb-opcodes/optables/), and the
[RGBDS gbz80(7) reference](https://rgbds.gbdev.io/docs/master/gbz80.7).

## Starting point (commit `fa22315`, plus uncommitted `core/src/gb_cpu.c`)

Diagnostic `gb_cpu_init` state, not the post-boot state. The repository does
not contain Nintendo boot firmware.

| Field | Diagnostic init | Documented DMG state after the boot ROM |
| --- | --- | --- |
| A, F | 0, 0 | 0x01, 0xB0 |
| BC | 0x0000 | 0x0013 |
| DE | 0x0000 | 0x00D8 |
| HL | 0x0000 | 0x014D |
| SP | 0xFFFE | 0xFFFE |
| PC | 0x0100 | 0x0100 |

Working tree before phase 1: grouped 8-bit INC/DEC is already in
`gb_cpu_step`, and the old single `case 0x3c` is commented out. That edit is
kept and is committed with the arithmetic phase, not this audit.

### Base opcodes at the start

Legal base opcodes: 245. Documented illegal opcodes, which hard-lock the CPU:
`D3 DB DD E3 E4 EB EC ED F4 FC FD` (11). All 256 CB-prefixed operations are
legal and were unimplemented.

Implemented and covered by `tests/test_cpu.c` (118 base opcodes):

- `NOP`, `JR e8`, `HALT`
- `LD r,r'` for `0x40–0x7F` except `HALT`
- `LD r,d8` including `LD (HL),d8`
- `LD rr,d16` for BC, DE, HL, SP
- `LD (BC/DE/HL+/HL-),A` and `LD A,(BC/DE/HL+/HL-)`
- `LD (a16),A`, `LD A,(a16)`, `LD (a16),SP`, `LD SP,HL`
- `LDH (a8),A`, `LDH A,(a8)`, `LD (C),A`, `LD A,(C)`
- `ADD A,r` including `ADD A,(HL)`
- 8-bit `INC`/`DEC` for registers and `(HL)` (uncommitted at the start)

Not yet data-movement gaps in the ordinary load set. `LD HL,SP+e8` follows
the arithmetic flag rules and is scheduled with that phase. `PUSH`/`POP` are
scheduled with the stack phase.

Every other base opcode, including the 11 illegal opcodes, returned
`GB_STEP_UNSUPPORTED` and left PC unchanged. None of them fell through as
`NOP`. No CB prefix was decoded.

## Phases

- [x] **1. Audit and test infrastructure.** Always-on `GB_REQUIRE` checks (they
  stay active if a build defines `NDEBUG`), `tests/gb_test_util.c` for bounded
  headless programs, and this checklist. `cpu_harness` also records the
  diagnostic initial registers.
- [x] **2. Data movement.** No new load opcodes were required. `cpu_loads`
  checks little-endian immediates, 16-bit address wrap, HL+ / HL- wrap, echo
  RAM, high-page edges, unchanged flags (`0xF0`), and a sweep of every
  ordinary load opcode. `LD HL,SP+e8` is still with the arithmetic phase.
- [x] **3. Arithmetic and logic.** 8-bit INC/DEC (including the previously
  uncommitted grouped form), ADC/SUB/SBC/AND/XOR/OR/CP for registers, `(HL)`,
  and immediates, 16-bit INC/DEC, `ADD HL,rr`, `ADD SP,e8`, `LD HL,SP+e8`,
  DAA, CPL, SCF, CCF, and the accumulator rotates. `cpu_alu` covers the flag
  boundaries. `RLCA`/`RLA`/`RRCA`/`RRA` clear Z even when A becomes 0.
- [x] **4. Control flow and stack.** Conditional and absolute jumps, calls,
  returns, `RETI` (sets `ime` immediately), `RST`, and `PUSH`/`POP` including
  AF masking and SP wrap. `cpu_control` includes a countdown loop and nested
  calls, and every test run has a step limit.
- [x] **5. CB prefix, CPU control, illegal opcodes, coverage report.** All 256
  CB operations, `DI`, delayed `EI`, `HALT` including the HALT bug, `STOP`,
  instruction-boundary interrupt dispatch, and the 11 illegal opcodes.
  `docs/CPU_OPCODE_COVERAGE.md` records the Pan Docs split: 245 implemented,
  11 illegal, 256 CB. `gb_cpu_request_interrupt` is the hook stage 3 devices
  will call. Joypad wake from `STOP`, and anything that needs a running timer
  or PPU, is still stage 3.
- [ ] **6. Independent ROMs and stage-2 exit notes.**
