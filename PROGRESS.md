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
- Stage 3 timer is implemented. `ctest` has 12 passing targets. `cpu_demo`
  still ends at `RAM[0xC000] = 69` and 48 T-cycles. The divider and TIMA
  advance on machine cycles inside `gb_cpu_step`. Overflow requests interrupt
  `0x50` through `gb_cpu_request_interrupt`.
- Blargg individual cpu_instrs: 11 Passed, including `02-interrupts`.
  Mooneye `div_timing`, `pop_timing`, and the 13 acceptance timer ROMs passed.
  `halt_ime0_ei` and `call_timing` still hit the runner's cycle limit because
  they wait on `LY` and OAM DMA, which are stage 4.
- Stage 4 picture processor and OAM DMA are implemented. `ctest` has 13
  passing targets, including exact-pixel, scanline, VBlank, access-boundary,
  and DMA tests. `cpu_demo` still ends at `RAM[0xC000] = 69` and 48 T-cycles.
  UBSan reports the same 13/13 and a clean `cpu_demo`.
- `./build/pocketglass examples/video-demo.gb` runs an original ROM through
  the CPU and bus and shows a scrolling checker plus a moving sprite.
  Mode 3 is a fixed 172 dots (`mode3_stall_dots` is the later stall hook).
- Mooneye `halt_ime0_ei`, `oam_dma/basic`, and `oam_dma/reg_read` pass.
  `call_timing` still times out (echo RAM reads `$FF` during DMA, so the
  `CALL` at `$FDFE` becomes `RST 38`). `oam_dma/sources-GS` and the extracted
  PPU timing ROMs fail with signature `0x42`.
- Still needed before an ordinary game is playable: joypad (including `STOP`
  wake), cartridge banking beyond 32 KiB ROM-only, serial, audio, and tighter
  PPU timing. No claim that a commercial game runs.
