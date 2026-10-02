# Pocketglass

An original Game Boy emulator in development. The hardware core will be C;
the desktop app will be C++. The finished product is planned to play permitted
homebrew ROMs, rewind gameplay, and show a live view inside the machine.

## Current state

Milestone 0 is complete on an Apple M2 Mac. Milestone 1 reads a cartridge
header and maps a 32 KiB ROM-only cartridge into a Game Boy memory bus.
Milestone 2 has begun with six CPU opcodes and a tiny instruction demo.
It cannot run a real game yet.
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

This demo puts nine instruction bytes into a synthetic cartridge at address
`0x0100`. The CPU loads 64 into register A, loads 5 into B, adds them, writes
69 to work RAM at `0xC000`, then halts. Every printed row is one instruction.
`PC` is the **program counter**, the address of the next instruction. `opcode`
is the instruction byte. `cycles` tells how much hardware time it consumes.

The expected last line is `RAM[0xC000] = 69 (expected 69); total cycles = 40`.
Open `examples/cpu_demo.c` and find the `program[]` array; then look for each
opcode in `core/src/gb_cpu.c`. The CPU's `gb_memory_read` and
`gb_memory_write` calls are exactly why we built the memory bus first.

Only `NOP`, `LD A,d8`, `LD B,d8`, `ADD A,B`, `LD (a16),A`, and `HALT` are
implemented so far. Initial registers are a diagnostic state, not the final
Game Boy post-boot values. Unsupported opcodes stop rather than silently
pretending they worked. Next we will implement the remaining instructions in
small families and validate them against independent test ROMs.

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
