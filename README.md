# Pocketglass

An original Game Boy emulator in development. The hardware core will be C;
the desktop app will be C++. The finished product is planned to play permitted
homebrew ROMs, rewind gameplay, and show a live view inside the machine.

## Current state

Milestone 0 is complete on an Apple M2 Mac. Milestone 1 reads a cartridge
header and maps a 32 KiB ROM-only cartridge into a Game Boy memory bus.
Stage 2 of the CPU is complete: every documented legal SM83 opcode, including
the CB prefix, has an implementation, and the 11 illegal opcodes lock the CPU
instead of running as `NOP`. Stage 3 adds the DMG divider and programmable
timer. A timer overflow can request interrupt `0x50`. Stage 4 adds the
monochrome picture processor, a 160-byte OAM DMA transfer, and a window that
shows frames produced by that processor. Audio, the joypad, and cartridge
banking are later stages. `tinydbg` is an unrelated, abandoned debugger
experiment and is not a dependency.

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

With no cartridge, the expected output includes
`C core called from C++: bit 7 = 1`, and a green 480×432 window opens. Close
it to exit. With a cartridge, the same window size is a 3× view of the
emulated 160×144 frame.

## First cartridge experiment

Generate a synthetic 32 KiB ROM with a valid header (it is not a playable game):

```sh
python3 examples/make_demo_rom.py
./build/pocketglass examples/header-demo.gb
```

The program prints `Cartridge: POCKETGLASS!` and `Header checksum: valid`, then
runs the image. That file has no program at `0x0100`, so the CPU executes
`NOP` and the LCD shows an empty frame. Close the window to exit. If a file
is missing, truncated, not a valid 32 KiB ROM-only cartridge, or too large,
the program reports that and does not open a game window.

## First picture

Generate an original demo and open it. The checker and the sprite are drawn
by the emulated CPU writing video memory, then scrolling and moving during
VBlank:

```sh
python3 examples/make_video_demo.py
./build/pocketglass examples/video-demo.gb
```

The window should show a scrolling checkerboard and a sprite moving right.
Close it to exit. An illegal or unsupported opcode is printed with its
address, and the last frame stays up until the window closes.

The cartridge header starts at byte address `0x100`; the title starts at
`0x134`. `gb_cartridge.c` reads fields by their byte offsets, computes a header
checksum, and checks that the file is large enough before accessing them.
The test changes one title byte and confirms that the checksum becomes invalid.

The CPU will ask the bus for a byte at an address. For instance, address
`0x0100` comes from the cartridge, while `0xC123` is working RAM. Writes to
the ROM are ignored. Addresses `0xE000` through `0xFDFF` mirror part of RAM.
`gb_memory.c` is the single routing point for these accesses. `DIV`, `TIMA`,
`TMA`, and `TAC` (`FF04`–`FF07`) are the timer in `gb_timer.c`. LCDC, STAT,
scroll, `LY`, DMA, and the palettes are the picture processor in `gb_ppu.c`.
The other I/O ports still store plain bytes. Only 32 KiB ROM-only cartridges
are mapped now. The clock contract is in `docs/TIMER.md` and `docs/PPU.md`:
`gb_cpu_step` still reports T-cycles, and the timer, LCD, and DMA advance one
machine cycle (4 T-cycles) at a time.

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
`mts-20260714-0944-31510e1` is MIT licensed. All 11 individual Blargg ROMs
print `Passed`, including `02-interrupts`. Mooneye `instr/daa`,
`ei_sequence`, `ei_timing`, `rapid_di_ei`, `if_ie_registers`,
`boot_regs-dmgABC`, `div_timing`, and `pop_timing` report the pass signature.
The timer acceptance ROMs do too: `div_write`, `rapid_toggle`, `tim00`,
`tim00_div_trigger`, `tim01`, `tim01_div_trigger`, `tim10`,
`tim10_div_trigger`, `tim11`, `tim11_div_trigger`, `tima_reload`,
`tima_write_reloading`, and `tma_write_reloading`.

`halt_ime0_ei` now reports the pass signature (718384 T-cycles, 108444
steps). `oam_dma/basic` and `oam_dma/reg_read` pass as well. `call_timing`
still reaches the 300000000 T-cycle limit. It fetches a `CALL` from echo RAM
at `$FDFE` while DMA is active; this core returns `$FF` for that read, the
CPU executes `RST 38`, and the ROM stays there. `oam_dma/sources-GS` ends
with the failure signature `0x42`: a DMA source page of `$FE` is copied from
OAM itself, which is the case that test rejects. The extracted PPU timing
ROMs also end at `0x42`: `hblank_ly_scx_timing-GS`, `intr_1_2_timing-GS`,
`intr_2_0_timing`, `intr_2_mode0_timing`, `intr_2_mode0_timing_sprites`,
`intr_2_mode3_timing`, `intr_2_oam_ok_timing`, `lcdon_timing-GS`,
`lcdon_write_timing-GS`, `stat_irq_blocking`, `stat_lyc_onoff`, and
`vblank_stat_intr-GS`. Mode 3 is a fixed 172 dots, so those cycle counts are
not claimed as passes. The combined 64 KiB `cpu_instrs.gb` is not run: the
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
