# Pocketglass

An original Game Boy emulator in development. The hardware core will be C;
the desktop app will be C++. The finished product is planned to play permitted
homebrew ROMs, rewind gameplay, and show a live view inside the machine.

## Current state

Milestone 0 is complete on an Apple M2 Mac. Milestone 1 reads a cartridge
header and maps a 32 KiB ROM-only cartridge into a Game Boy memory bus.
Stage 2 of the CPU is complete: every documented legal SM83 opcode, including
the CB prefix, has an implementation, and the 11 illegal opcodes lock the CPU
instead of running as `NOP`. The desktop window still does not play a game.
Timers, the picture processor, audio, and cartridge banking are later stages.
`tinydbg` is
an unrelated, abandoned debugger experiment and is not a dependency.

## Build and run

You need a C/C++ compiler, CMake, and SDL3. On a Mac, install Apple's Command
Line Tools with `xcode-select --install` if not already present. If Homebrew is
installed, run `brew install cmake sdl3`. Run the following in this folder:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/pocketglass
```

Expected output includes `C core called from C++: bit 7 = 1`, and a green
480×432 window opens. Close it to exit. The 160×144 Game Boy display will be
scaled 3× in this window once we implement graphics. The window does not yet
show a game.

## First cartridge experiment

Generate a synthetic 32 KiB ROM with a valid header (it is not a playable game):

```sh
python3 examples/make_demo_rom.py
./build/pocketglass examples/header-demo.gb
```

The program should print `Cartridge: POCKETGLASS!` and `Header checksum: valid`
plus `ROM-only memory map ready` before the green window opens. Close the window
to exit. To try a permitted
homebrew `.gb` file, pass its path instead. We only read its metadata so far.
If a file is missing, truncated, or too large, the program reports an error.

The cartridge header starts at byte address `0x100`; the title starts at
`0x134`. `gb_cartridge.c` reads fields by their byte offsets, computes a header
checksum, and checks that the file is large enough before accessing them.
The test changes one title byte and confirms that the checksum becomes invalid.

The CPU will ask the bus for a byte at an address. For instance, address
`0x0100` comes from the cartridge, while `0xC123` is working RAM. Writes to
the ROM are ignored. Addresses `0xE000` through `0xFDFF` mirror part of RAM.
`gb_memory.c` is the single routing point for these accesses. The I/O region
currently stores plain bytes; later hardware components will give those
registers their real behavior. Only 32 KiB ROM-only cartridges are mapped now.

## First CPU experiment

```sh
./build/cpu_demo
```

This demo puts a short program into a synthetic cartridge at address `0x0100`.
The CPU loads 64 into register A, loads 5 into B, loads 7 into C, adds A and B,
writes 69 to work RAM at `0xC000`, then halts. Every printed row is one
instruction. `PC` is the **program counter**, the address of the next
instruction. `opcode` is the instruction byte. `cycles` is the instruction's
time in **T-cycles** (a `NOP` is 4).

The expected last line is `RAM[0xC000] = 69 (expected 69); total cycles = 48`.
Open `examples/cpu_demo.c` and find the `program[]` array; then look for each
opcode in `core/src/gb_cpu.c`. The CPU's `gb_memory_read` and
`gb_memory_write` calls are exactly why we built the memory bus first.

`gb_cpu_init` is a diagnostic state: every register is 0, `PC` is `0x0100`,
and `SP` is `0xFFFE`. That is not the state left by Nintendo's boot ROM, which
this project does not include. `gb_cpu_init_dmg_post_boot` copies only the
documented DMG register values from after that ROM. `gb_cpu_step` returns
`GB_STEP_UNSUPPORTED` only if a legal opcode has no implementation. The 11
illegal opcodes (`D3`, `DB`, `DD`, `E3`, `E4`, `EB`, `EC`, `ED`, `F4`, `FC`,
`FD`) return `GB_STEP_ILLEGAL` and hard-lock the CPU. Coverage and the phase
notes are in `docs/CPU_OPCODE_COVERAGE.md` and `docs/CPU_STAGE2_CHECKLIST.md`.

Independent ROM checks are optional and are not part of `ctest`, because the
images are downloaded rather than stored in git:

```sh
tests/external/fetch_cpu_roms.sh
cmake --build build --target cpu_rom_runner
tests/external/run_cpu_roms.sh
```

Blargg's individual `cpu_instrs` ROMs come from the retrio mirror at commit
`c240dd7` (Shay Green's test ROMs; `cpu_instrs/readme.txt` has no separate
SPDX license, so the binaries are not committed). Mooneye's published build
`mts-20260714-0944-31510e1` is MIT licensed. On the current core, 10 of the 11
individual Blargg ROMs print `Passed`. `02-interrupts` stops with
`Timer doesn't work`. Mooneye `instr/daa`, `ei_sequence`, `ei_timing`,
`rapid_di_ei`, `if_ie_registers`, and `boot_regs-dmgABC` report the pass
signature. `div_timing` and `pop_timing` report the failure signature.
`halt_ime0_ei` and `call_timing` reach the cycle limit without a signature.
Those depend on the timer, the picture hardware, or interrupt timing finer
than one instruction. The combined 64 KiB `cpu_instrs.gb` is not run: the
memory bus maps 32 KiB only.

## First C lesson

`uint8_t` means an unsigned integer with exactly eight bits: one byte. `0x80`
is hexadecimal for a byte with only bit 7 set. The expression `1u << 7`
moves a single `1` seven places left, making that mask. `|` turns a bit on;
`& ~` turns one off. Game Boy CPU flags and hardware registers use these
operations constantly. `gb_bits.h` declares functions so the C++ app and the
C test can call the same C implementation.

Try changing only the `7` in `gb_bit_is_set(flags, 7)` to `3`; predict the
window color, then build and run again. Restore it afterward. Don't change
the tests yet: they define expected behavior.

See `PROGRESS.md` for the current gate and next action.
