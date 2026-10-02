# Stage 4 picture processor — DMG video and OAM DMA

The picture processor lives in `gb_ppu.c` and is part of the bus state, next
to the timer. It is not a second instruction decoder. `gb_timer_advance`
still consumes T-cycles in machine cycles, and each machine cycle calls
`gb_ppu_on_machine_cycle`. The CPU, `HALT`, and interrupt service all go
through that clock. A direct memory read or write from a test does not.
`STOP` does not advance time, so the LCD and DMA stay still too.

Rendering writes a 160×144 buffer of shades 0–3. SDL is only a viewer of
that buffer. The unit tests never open a window.

This is the original monochrome Game Boy. Game Boy Color features are not
implemented.

## Timing

One line is 456 dots, which is 114 machine cycles. A frame is 154 lines:
`LY` 0–143 are visible and `LY` 144–153 are VBlank. Entering `LY` 144 sets
IF bit 0 once and marks a frame ready for the window. On line 153, `LY`
reads as 153 for the first 4 dots and as 0 for the rest of that line, while
the mode stays 1. The next line is the real return to line 0.

On a visible line the mode reads as 2 for the first 80 dots, then 3, then 0.
Mode 3 starts at 172 dots and grows when the line is latched at the start of
mode 3: `SCX % 8` dots, 6 dots if the window triggers, and the object
penalties from Pan Docs (an X of 0 costs 11, and other objects pay 6 plus
the leftover background-fetch dots of a new tile). The mode is still sampled
once per machine cycle, so a penalty under 4 dots is visible only when it
crosses that boundary. The scanline is drawn on the machine cycle that
enters mode 0.

`STAT` stores bits 3–6. Reads report the mode in bits 1–0, the `LY`/`LYC`
comparison in bit 2, and 1 in bit 7. The four STAT sources are one signal.
A STAT interrupt is the rising edge of that OR, including a write to `STAT`
or `LYC` that makes it newly true. Leaving one source active while another
drops does not request again. Clearing LCDC bit 7 sets `LY` and the dot
counter to 0 and leaves VRAM and OAM open. Setting the bit again starts
line 0 in mode 2 immediately and recomputes the STAT line. There is no
extra blanking delay before that first mode 2.

`gb_memory_init` leaves the LCD off so existing CPU and timer tests do not
move `LY`. `gb_ppu_apply_dmg_post_boot` is what the ROM runner and the
desktop app use: LCDC `0x91`, BGP `0xFC`, both object palettes `0xFF`, and
the LCD running at the start of line 0. That is not a dump of every boot-ROM
I/O register.

## CPU access

In mode 3 a CPU read of VRAM (`8000`–`9FFF`) returns `FF` and a write is
ignored. In modes 2 and 3, and during OAM DMA, a CPU read of OAM
(`FE00`–`FE9F`) returns `FF` and a write is ignored. Drawing and the DMA
copy use the arrays directly, so they are not blocked by those rules.

## Picture

Tile bytes are 2 bits per pixel, low plane first. Background and window use
the map selected by LCDC and either unsigned tiles at `8000` or signed tiles
at `8800`. The window uses `WX - 7` and its own line counter, which advances
only on lines where the window is actually drawn. Object palettes, flips,
transparency (color 0), and the "behind background" flag follow the DMG
rules. At most 10 objects per line are considered, in OAM order. When two
of those overlap, the smaller X wins, then the earlier OAM slot. On the DMG,
LCDC bit 0 off makes the background color 0, so objects are drawn over it.

## OAM DMA

A write to `FF46` stores the page and starts a 160-byte copy into
`FE00`–`FE9F`. The machine cycle after the write does not copy a byte.
Each following machine cycle copies one byte. The cycle that copies byte
159 still leaves OAM locked for the CPU; the next machine cycle clears
`dma_active`. Writing `FF46` again restarts the transfer.

OAM (`FE00`–`FE9F`) is the region the CPU cannot read or write while the
transfer is active, and also in modes 2 and 3. ROM, VRAM (outside mode 3),
external RAM, work RAM, echo RAM, and HRAM stay reachable. `call_timing`
fetches its `CALL` from echo RAM at `$FDFE` and now passes. A DMA source at
`$E000` or above is read as the address minus `$2000`, so `$FE00`–`$FFFF`
come from work RAM `$DE00`–`$DFFF` rather than from OAM or I/O.
`oam_dma/sources-GS` passes with that map once the cartridge's
MBC5 RAM is present: the ROM failed first on a DMA from `$A000`, which is
external RAM enabled by a write of `$0A` to `$0000`. The pass was 2544444
T-cycles and 363647 steps. There is no same-cycle bus-conflict model beyond
the OAM lock.

## What is still simplified

Mode 3 penalties are applied, but the PPU is not dot-accurate. The twelve
Mooneye PPU timing ROMs still end at signature `0x42`. LCD enable does not
imitate the first-frame blanking delay some of those ROMs measure. There is
no boot ROM.
