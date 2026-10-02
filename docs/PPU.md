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
IF bit 0 once and marks a frame ready for the window. `LY` then runs through
153 and returns to 0. Line 153 is a full 456 dots; the short line-153 quirk
is not modeled.

On a visible line the mode reads as 2 for the first 80 dots, 3 for the next
172, and 0 for the rest. `GbPpu.mode3_stall_dots` is reserved so a later
fetcher can lengthen mode 3. It is 0, so the 172-dot mode 3 is a fixed
simplification, not a cycle-accurate pixel pipeline. The scanline is drawn
on the machine cycle that enters mode 0.

`STAT` stores bits 3–6. Reads report the mode in bits 1–0, the `LY`/`LYC`
comparison in bit 2, and 1 in bit 7. A STAT interrupt is requested on the
rising edge of an enabled mode-0, mode-1, mode-2, or `LYC` source. Clearing
LCDC bit 7 sets `LY` and the dot counter to 0 and leaves VRAM and OAM open.
Setting the bit again starts line 0 in mode 2. The first-frame delay of the
real LCD turn-on is not modeled.

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
`FE00`–`FE9F`. One byte is copied per machine cycle, beginning on the
machine cycle after the write. The write is not an instantaneous copy.
Writing `FF46` again restarts the transfer. While it runs, CPU reads below
`FF80` return `FF` and those writes are ignored, except another write to
`FF46`. `FF80`–`FFFE` stay usable, which is the HRAM execution path.

This is the block described in Pan Docs. It is stricter than the real DMA
bus-conflict behavior. `call_timing` fetches a `CALL` from echo RAM at
`$FDFE` during the transfer, so that read becomes `$FF` (`RST 38`) and the
ROM loops instead of finishing. `oam_dma/sources-GS` also fails: a source
page of `$FE` is copied as OAM data, without the external-bus decode that
test is written to catch.

## What is still simplified

Mode 3 does not stretch for sprites or scroll. `LY` 153 is not short. LCD
enable does not delay the first frame. STAT timing is evaluated once per
machine cycle after the line counter updates. There is no boot ROM.
