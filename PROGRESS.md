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
- Next: run 4/4 tests and cpu_demo on Mac, then extend instruction families.
- No ROM compatibility or emulator functionality is claimed yet.
