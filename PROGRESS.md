# Progress

## 2026-10-02 — Milestone 0 complete on user's Mac

- Created a C core, C++ SDL3 window, CMake build, and a byte/bit exercise.
- User reported successful native Apple M2 build, passing test, and visible green window.
- Mac: Apple M2, macOS 26.5.2. SDL3 and CMake installed under /opt/homebrew.
- Cartridge-header reader passed on the user's Mac with synthetic 32 KiB ROM.
- ROM-only memory bus passed 3/3 tests on user's Mac; entry byte read at 0x0100.
- Milestone 1's initial ROM-only gate complete. Banked cartridges and working
  hardware I/O are later extensions.
- Milestone 2 started: CPU registers, six opcodes, cycle counts, and a diagnostic
  program that writes 69 into work RAM. Local tests pass; Mac test pending.
- Stage 2 CPU is implemented. `ctest` has 11 passing targets, including
  opcode coverage of 245 legal base opcodes, 11 illegal opcodes, and 256 CB
  opcodes. `cpu_demo` still ends at `RAM[0xC000] = 69` and 48 T-cycles.
- Blargg individual cpu_instrs: 10 Passed, `02-interrupts` reports
  `Timer doesn't work`. Mooneye `daa`, `ei_sequence`, `ei_timing`,
  `rapid_di_ei`, `if_ie_registers`, and `boot_regs-dmgABC` passed. Timer and
  instruction-timing ROMs are recorded in the README and are not faked.
- Next is stage 3: the timer and the rest of the interrupt sources, then the
  PPU. `gb_cpu_request_interrupt` is the CPU-side hook those devices should
  call. `STOP` stays stopped until a later joypad stage clears `stopped`.
- No claim that a commercial game or the full picture hardware runs.
