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
shows frames produced by that processor. Stage 5 adds the DMG joypad,
keyboard input, ROM-only and MBC1/MBC3 cartridges, battery-backed saves, and
an original game you can finish. Stage 6 adds MBC5, a basic four-channel
sound chip, pause and a controls overlay, and a macOS folder you can copy
out of the tree. Serial is still later. This is not a complete Game Boy.
`tinydbg` is an unrelated, abandoned debugger experiment and is not a
dependency.

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
`NOP` and the LCD shows an empty frame. Close the window to exit. A missing
file, a bad header checksum, a ROM size that does not match the header, or
an unsupported cartridge type is reported and does not open a game window.

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

## Catch

Catch is an original 32 KiB ROM-only game. The generator source is
`examples/make_play_game.py`, and the built image is `examples/catch.gb`.
One command regenerates it and opens the window:

```sh
python3 examples/make_play_game.py && ./build/pocketglass examples/catch.gb
```

The title says CATCH and START. Press Enter to play. Arrow keys move the
player. On even waves a block falls at the left; catch it and you lose. On
odd waves a coin falls in the center; catch three coins and you win. Enter
on the win or lose screen returns to the title, and Enter again starts a
new game. The gameplay runs on the emulated CPU, joypad, timer, and picture
processor. Z is A, X is B, and Backspace or Right Shift is Select. Those
keys are not used by Catch. P pauses and shows the controls. H toggles that
help text. M mutes. Losing window focus releases every button. If the CPU
executes `STOP`, the window waits and the next new press wakes it.

## Cartridges and saves

`gb_memory_load_cartridge` accepts header types `00` (ROM only), `08` and
`09` (ROM plus external RAM), `01` through `03` (MBC1), `0F`, `10`, `11`,
`12`, and `13` (MBC3, including the timer variants), and `19` through `1E`
(MBC5, including RAM and the rumble types). A rumble cartridge ignores RAM
bank bit 3; there is no motor. Anything else is rejected with the type code
in the message. A Game Boy Color-only header (`0x143` = `0xC0`) is rejected.
A `.nes`, `.sfc`, `.smc`, or `.gba` file is rejected before the window
opens. The file length must equal the header ROM size (32 KiB through
8 MiB). MBC1 bank 0 in the high window is bank 1. MBC5 starts in bank 1, and
a later write of 0 really selects bank 0. MBC3 bank 0 is bank 1. Its RAM and
RTC registers are banked, and a `00` then `01` write on `6000`–`7FFF`
latches the clock. External RAM reads as `FF` until the enable nibble is
`0A`. MBC5 RAM uses the same enable write.

A battery type writes a save beside the ROM: `game.gb` becomes `game.sav`
(and `game.gbc` becomes `game.sav`; other names gain a `.sav` suffix). The
file is loaded at startup. A missing save is a blank RAM. The length must
be the external RAM size, or that size plus 18 bytes when the cartridge
has an RTC. A mismatch is reported and the file is not overwritten on
exit. A successful shutdown writes a temporary file, then renames it over
the old save, so a failed write leaves the previous file in place. Closing
the window is the only automatic write. A crash or a kill discards changes
since the last successful save. The RTC, when present, is the host clock,
not a count of T-cycles. The 18-byte trailer stores the latched registers,
the live registers, and the host unix time.

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
`FF00` is the joypad. `FF10`–`FF3F` are the sound chip. The other I/O ports
that are not the timer or the picture processor still store plain bytes.
`gb_memory_init` is still the raw 32 KiB path used by unit tests. The ROM
runner uses `gb_memory_load_cartridge` when the file matches a supported
header, and the raw 32 KiB path otherwise. The clock contract is in
`docs/TIMER.md` and `docs/PPU.md`: `gb_cpu_step` still reports T-cycles, and
the timer, LCD, DMA, and audio advance one machine cycle (4 T-cycles) at a
time.

The core is one C library: the cartridge bus, CPU, timer, picture processor,
and sound. The C++ file `app/main.cpp` only owns the SDL window, keyboard,
and audio device. It does not contain game rules.

## Sound

The four DMG channels are generated in `gb_apu.c` and queued to SDL as
signed 16-bit stereo at 32768 Hz. Length, envelope, channel-1 sweep, the
wave RAM, and the noise polynomial follow Pan Docs closely enough to be
recognizable. These parts are approximate: the 512 Hz sequencer is not tied
to `DIV`, square and noise are centered around zero instead of the hardware
DC offset, and there is no VIN pin. Turning `NR52` bit 7 off silences the
channels and keeps wave RAM. If SDL cannot open a device, the window runs
silent. M drops queued samples without muting the game's own registers.

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

`halt_ime0_ei` reports the pass signature (718384 T-cycles, 108444 steps).
`oam_dma/basic`, `oam_dma/reg_read`, `oam_dma/sources-GS`, and `call_timing`
pass as well. `call_timing` finished at 928732 T-cycles and 121092 steps.
`sources-GS` finished at 2544444 T-cycles and 363647 steps. The extracted
PPU timing ROMs still end at `0x42`, with no timeouts: `hblank_ly_scx_timing-GS`
(1216656 T-cycles), `intr_1_2_timing-GS` (943812), `intr_2_0_timing` (873596),
`intr_2_mode0_timing` (873564), `intr_2_mode0_timing_sprites` (1446076),
`intr_2_mode3_timing` (873556), `intr_2_oam_ok_timing` (873564),
`lcdon_timing-GS` (1322184), `lcdon_write_timing-GS` (2517116),
`stat_irq_blocking` (795320), `stat_lyc_onoff` (718832), and
`vblank_stat_intr-GS` (1295668). Those are not passes. The combined 64 KiB
`cpu_instrs.gb` is not in that list.

## Homebrew that was actually played

These are not claims that every Game Boy game runs. The ROMs are not stored
in git. Catch is the one shipped here.

- Catch, `examples/catch.gb`, ROM-only type `00`. Headless test reaches lose,
  win, and a restart. Keyboard play uses the window.
- 2048 by Sanqui, zlib license, 32 KiB MBC1+RAM+battery type `03`. Source
  and the assembled URL are in `https://github.com/Sanqui/2048-gb`. After the
  title, Start, Right, and Down each changed the frame. No samples were
  produced in that session. No illegal opcode.
- Droneboy 1.09 by purefunktion, MIT license, 32 KiB ROM-only type `00`.
  Download `droneboy.gb` from the v1.09 GitHub release. The drone produces
  samples from the first frames, and Right changes the picture. No illegal
  opcode.

Rhythm Land 1.0.1 (MIT, MBC5, 128 KiB) draws a static frame and then sits
in `HALT` at `$0033`. Buttons did not change that frame and no samples were
produced, so it is not listed as playable.

## macOS package and screenshots

```sh
scripts/package_macos.sh
```

That writes `dist/pocketglass-macos-arm64/` with the arm64 executable,
`libSDL3.0.dylib`, `catch.gb`, a short README, and the SDL3 license. Copy
that folder somewhere that is not the source tree and run
`./pocketglass catch.gb` from there.

No demo video or GIF was recorded in this environment. To capture one after
the window is open:

```sh
screencapture -x docs/images/catch.png
# A short GIF, if ffmpeg is installed:
ffmpeg -f avfoundation -i "1:none" -t 8 -vf "fps=12,scale=480:-1" docs/images/catch.gif
```

Checklist before adding media to the README: the Catch title is readable,
the player moves, a coin or the block is on screen, the win or lose text is
visible, the P-key help overlay is up, and the file is a real capture rather
than a drawing. Until those files exist, this paragraph is the placeholder.

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
