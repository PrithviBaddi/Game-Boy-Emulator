"""Assemble Catch, an original ROM-only game controlled through the joypad."""
from pathlib import Path


class Asm:
    def __init__(self):
        self.code = bytearray()
        self.labels = {}
        self.fixups = []

    def label(self, name):
        self.labels[name] = len(self.code)

    def emit(self, *values):
        self.code.extend(values)

    def link16(self, name):
        here = len(self.code)
        self.emit(0, 0)
        self.fixups.append(("abs", here, name))

    def jr(self, opcode, name):
        self.emit(opcode, 0)
        self.fixups.append(("rel", len(self.code) - 1, name))

    def link(self, base):
        image = bytearray(self.code)
        for kind, at, name in self.fixups:
            target = base + self.labels[name]
            if kind == "rel":
                relative = target - (base + at + 1)
                if not -128 <= relative <= 127:
                    raise SystemExit(f"{name} is {relative} bytes from a JR")
                image[at] = relative & 0xFF
            else:
                image[at] = target & 0xFF
                image[at + 1] = (target >> 8) & 0xFF
        return image


FONT = {
    "C": [" ####   ", "#    #  ", "#       ", "#       ", "#       ", "#    #  ", " ####   ", "        "],
    "A": ["  ##    ", " #  #   ", "#    #  ", "#    #  ", "######  ", "#    #  ", "#    #  ", "        "],
    "T": ["######  ", "  ##    ", "  ##    ", "  ##    ", "  ##    ", "  ##    ", "  ##    ", "        "],
    "H": ["#    #  ", "#    #  ", "#    #  ", "######  ", "#    #  ", "#    #  ", "#    #  ", "        "],
    "S": [" ####   ", "#       ", "#       ", " ####   ", "    #   ", "    #   ", "####    ", "        "],
    "R": ["#####   ", "#    #  ", "#    #  ", "#####   ", "#  #    ", "#   #   ", "#    #  ", "        "],
    "E": ["######  ", "#       ", "#       ", "#####   ", "#       ", "#       ", "######  ", "        "],
    "W": ["#    #  ", "#    #  ", "#    #  ", "# ## #  ", "##  ##  ", "#    #  ", "#    #  ", "        "],
    "I": [" ####   ", "  ##    ", "  ##    ", "  ##    ", "  ##    ", "  ##    ", " ####   ", "        "],
    "N": ["#    #  ", "##   #  ", "# #  #  ", "#  # #  ", "#   ##  ", "#    #  ", "#    #  ", "        "],
    "L": ["#       ", "#       ", "#       ", "#       ", "#       ", "#       ", "######  ", "        "],
    "O": [" ####   ", "#    #  ", "#    #  ", "#    #  ", "#    #  ", "#    #  ", " ####   ", "        "],
}

ORDER = "CATCHSREWINLO"
# Tile 1 is the player, 2 the coin, 3 the hazard. Letters start at tile 4.
LETTER = {ch: 4 + index for index, ch in enumerate(ORDER)}


def rows_to_2bpp(rows, color):
    out = bytearray()
    for row in rows:
        low = high = 0
        for bit, cell in enumerate(row[:8]):
            if cell != "#":
                continue
            shift = 7 - bit
            low |= (color & 1) << shift
            high |= ((color >> 1) & 1) << shift
        out.append(low)
        out.append(high)
    return out


def solid(color):
    return rows_to_2bpp(["########"] * 8, color)


def coin():
    return rows_to_2bpp(
        [
            "  ####  ",
            " #    # ",
            "#  ##  #",
            "# #  # #",
            "#  ##  #",
            " #    # ",
            "  ####  ",
            "        ",
        ],
        3,
    )


def hazard():
    return rows_to_2bpp(
        [
            "##    ##",
            " ##  ## ",
            "  ####  ",
            "   ##   ",
            "   ##   ",
            "  ####  ",
            " ##  ## ",
            "##    ##",
        ],
        3,
    )


program = Asm()
program.label("start")
program.emit(0xF3)  # DI
program.emit(0x3E, 0x00)
program.emit(0xE0, 0x40)  # LCD off
program.emit(0x21)  # LD HL, tiles
program.link16("tiles")
program.emit(0x11, 0x00, 0x80)  # LD DE, VRAM
program.emit(0x01, 0x10, 0x01)  # LD BC, 272 tile bytes
program.label("copy_tiles")
program.emit(0x2A, 0x12, 0x13, 0x0B, 0x78, 0xB1)
program.jr(0x20, "copy_tiles")
program.emit(0x21, 0x00, 0x98)  # clear the map
program.emit(0x01, 0x00, 0x04)
program.emit(0xAF)
program.label("clear_map")
program.emit(0x22, 0x0B, 0x78, 0xB1)
program.jr(0x20, "clear_map")
program.emit(0x3E, 80)
program.emit(0xEA, 0x01, 0xC0)  # player x
program.emit(0x3E, 0xFF)
program.emit(0xEA, 0x0A, 0xC0)  # last painted state
program.emit(0x3E, 0xE4)
program.emit(0xE0, 0x47)
program.emit(0xE0, 0x48)
program.emit(0x3E, 0x93)
program.emit(0xE0, 0x40)
program.emit(0x3E, 0x01)
program.emit(0xEA, 0xFF, 0xFF)  # IE = VBlank
program.emit(0xFB)  # EI
program.label("main")
program.emit(0xCD)
program.link16("update")
program.emit(0x76)
program.jr(0x18, "main")

program.label("vblank")
program.emit(0xF5, 0xC5)  # PUSH AF, PUSH BC
program.emit(0x21, 0x10, 0xC0)
program.emit(0x11, 0x00, 0xFE)
program.emit(0x06, 16)
program.label("oam_copy")
program.emit(0x2A, 0x12, 0x13, 0x05)
program.jr(0x20, "oam_copy")
program.emit(0xC1, 0xF1, 0xD9)

program.label("read_pad")
program.emit(0x3E, 0x20, 0xE0, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0x2F, 0xE6, 0x0F)
program.emit(0xEA, 0x08, 0xC0)
program.emit(0x3E, 0x10, 0xE0, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0x2F, 0xE6, 0x0F)
program.emit(0xEA, 0x09, 0xC0)
program.emit(0xFA, 0x09, 0xC0)
program.emit(0x47)  # B = action buttons this frame
program.emit(0xFA, 0x0B, 0xC0)
program.emit(0x2F)  # bits that were released last frame
program.emit(0xA0)  # AND B
program.emit(0xE6, 0x08)
program.emit(0xEA, 0x0C, 0xC0)
program.emit(0x78)
program.emit(0xEA, 0x0B, 0xC0)
program.emit(0xC9)

program.label("update")
program.emit(0xCD)
program.link16("read_pad")
program.emit(0xFA, 0x00, 0xC0)  # state
program.emit(0xFE, 0x01)
program.emit(0xCA)  # JP Z, playing
program.link16("playing")
program.emit(0xB7)
program.jr(0x20, "ending")
program.label("title")
program.emit(0xFA, 0x0C, 0xC0)  # Start edge
program.emit(0xB7)
program.jr(0x20, "begin_play")
program.emit(0xC3)
program.link16("paint")
program.label("begin_play")
program.emit(0x3E, 0x01, 0xEA, 0x00, 0xC0)
program.emit(0xAF)
program.emit(0xEA, 0x02, 0xC0)  # score
program.emit(0xEA, 0x03, 0xC0)  # wave
program.emit(0xEA, 0x04, 0xC0)  # object y
program.emit(0x3E, 80)
program.emit(0xEA, 0x01, 0xC0)
program.emit(0x3E, 32)
program.emit(0xEA, 0x05, 0xC0)  # object x
program.emit(0xC3)
program.link16("paint")
program.label("ending")
program.emit(0xFA, 0x0C, 0xC0)
program.emit(0xB7)
program.jr(0x28, "paint")
program.emit(0xAF)
program.emit(0xEA, 0x00, 0xC0)  # back to the title
program.label("paint")
program.emit(0xFA, 0x00, 0xC0)
program.emit(0x47)
program.emit(0xFA, 0x0A, 0xC0)
program.emit(0xB8)
program.emit(0xCA)
program.link16("sprites")
program.emit(0x78, 0xEA, 0x0A, 0xC0)
program.emit(0xCD)
program.link16("redraw")
program.label("sprites")
program.emit(0xFA, 0x00, 0xC0)
program.emit(0xFE, 0x01)
program.jr(0x20, "hide_sprites")
program.emit(0x3E, 144)  # player screen y 128 + 16
program.emit(0xEA, 0x10, 0xC0)
program.emit(0xFA, 0x01, 0xC0)
program.emit(0xC6, 8)
program.emit(0xEA, 0x11, 0xC0)
program.emit(0x3E, 1)
program.emit(0xEA, 0x12, 0xC0)
program.emit(0xAF, 0xEA, 0x13, 0xC0)
program.emit(0xFA, 0x04, 0xC0)
program.emit(0xC6, 16)
program.emit(0xEA, 0x14, 0xC0)
program.emit(0xFA, 0x05, 0xC0)
program.emit(0xC6, 8)
program.emit(0xEA, 0x15, 0xC0)
program.emit(0xFA, 0x03, 0xC0)
program.emit(0xE6, 0x01)
program.jr(0x20, "coin_tile")
program.emit(0x3E, 3)
program.jr(0x18, "store_tile")
program.label("coin_tile")
program.emit(0x3E, 2)
program.label("store_tile")
program.emit(0xEA, 0x16, 0xC0)
program.emit(0xAF, 0xEA, 0x17, 0xC0)
program.emit(0xC9)
program.label("hide_sprites")
program.emit(0xAF)
program.emit(0xEA, 0x10, 0xC0)
program.emit(0xEA, 0x14, 0xC0)
program.emit(0xC9)

program.label("playing")
program.emit(0xFA, 0x01, 0xC0)
program.emit(0x47)  # B = x
program.emit(0xFA, 0x08, 0xC0)
program.emit(0xCB, 0x4F)  # BIT 1, left
program.jr(0x28, "no_left")
program.emit(0x78)
program.emit(0xFE, 2)
program.jr(0x38, "no_left")
program.emit(0xD6, 2)
program.emit(0xEA, 0x01, 0xC0)
program.emit(0x47)
program.label("no_left")
program.emit(0xFA, 0x08, 0xC0)
program.emit(0xCB, 0x47)  # BIT 0, right
program.jr(0x28, "no_right")
program.emit(0x78)
program.emit(0xFE, 144)
program.jr(0x30, "no_right")
program.emit(0xC6, 2)
program.emit(0xEA, 0x01, 0xC0)
program.label("no_right")
program.emit(0xFA, 0x04, 0xC0)
program.emit(0xC6, 4)
program.emit(0xEA, 0x04, 0xC0)
program.emit(0xFE, 128)
program.emit(0xDA)
program.link16("sprites")
program.emit(0xFA, 0x05, 0xC0)
program.emit(0x4F)  # C = object x
program.emit(0xFA, 0x01, 0xC0)
program.emit(0x91)  # SUB C, so A = player - object
program.jr(0x38, "obj_is_right")
program.emit(0xFE, 8)
program.jr(0x38, "caught")
program.jr(0x18, "advance_wave")
program.label("obj_is_right")
program.emit(0xFA, 0x01, 0xC0)  # player x
program.emit(0x5F)  # E = player x
program.emit(0x79)  # A = object x, still in C
program.emit(0x93)  # SUB E
program.emit(0xFE, 8)
program.jr(0x30, "advance_wave")
program.label("caught")
program.emit(0xFA, 0x03, 0xC0)
program.emit(0xE6, 0x01)
program.jr(0x28, "hit_hazard")
program.emit(0xFA, 0x02, 0xC0)
program.emit(0x3C)
program.emit(0xEA, 0x02, 0xC0)
program.emit(0xFE, 3)
program.jr(0x20, "advance_wave")
program.emit(0x3E, 2)
program.emit(0xEA, 0x00, 0xC0)  # win
program.jr(0x18, "advance_wave")
program.label("hit_hazard")
program.emit(0x3E, 3)
program.emit(0xEA, 0x00, 0xC0)
program.label("advance_wave")
program.emit(0xAF, 0xEA, 0x04, 0xC0)
program.emit(0xFA, 0x03, 0xC0)
program.emit(0x3C)
program.emit(0xEA, 0x03, 0xC0)
program.emit(0xE6, 0x01)
program.jr(0x20, "coin_x")
program.emit(0x3E, 32)
program.jr(0x18, "store_x")
program.label("coin_x")
program.emit(0x3E, 80)
program.label("store_x")
program.emit(0xEA, 0x05, 0xC0)
program.emit(0xC3)
program.link16("sprites")

program.label("redraw")
program.emit(0x21, 0x80, 0x98)  # row 4
program.emit(0x06, 64)
program.emit(0xAF)
program.label("wipe")
program.emit(0x22, 0x05)
program.jr(0x20, "wipe")
program.emit(0xFA, 0x00, 0xC0)
program.emit(0xFE, 0x01)
program.jr(0x28, "redraw_done")
program.emit(0xB7)
program.jr(0x20, "redraw_end")
program.emit(0x21, 0x86, 0x98)
program.emit(0x11)  # DE = CATCH
program.link16("word_catch")
program.emit(0xCD)
program.link16("place_word")
program.emit(0x21, 0xA6, 0x98)  # row 5, column 6
program.emit(0x11)
program.link16("word_start")
program.emit(0xCD)
program.link16("place_word")
program.emit(0xC9)
program.label("redraw_end")
program.emit(0x21, 0x87, 0x98)
program.emit(0xFA, 0x00, 0xC0)
program.emit(0xFE, 2)
program.jr(0x20, "word_lose")
program.emit(0x11)
program.link16("word_win")
program.emit(0xC3)
program.link16("place_word")
program.label("word_lose")
program.emit(0x11)
program.link16("word_lose_bytes")
program.emit(0xC3)
program.link16("place_word")
program.label("redraw_done")
program.emit(0xC9)
program.label("place_word")
program.emit(0x1A, 0xB7)
program.jr(0x28, "redraw_done")
program.emit(0x22, 0x13)
program.jr(0x18, "place_word")

program.label("word_catch")
program.emit(LETTER["C"], LETTER["A"], LETTER["T"], LETTER["C"], LETTER["H"], 0)
program.label("word_start")
program.emit(LETTER["S"], LETTER["T"], LETTER["A"], LETTER["R"], LETTER["T"], 0)
program.label("word_win")
program.emit(LETTER["W"], LETTER["I"], LETTER["N"], 0)
program.label("word_lose_bytes")
program.emit(LETTER["L"], LETTER["O"], LETTER["S"], LETTER["E"], 0)

program.label("tiles")
tiles = solid(0) + solid(3) + coin() + hazard()
for ch in ORDER:
    tiles += rows_to_2bpp(FONT[ch], 3)
if len(tiles) != 0x110:
    raise SystemExit(f"tile block is {len(tiles)} bytes, expected 272")
program.emit(*tiles)

base = 0x0150
image = program.link(base)
rom = bytearray(32 * 1024)
rom[0x100] = 0xC3
rom[0x101] = base & 0xFF
rom[0x102] = (base >> 8) & 0xFF
handler = base + program.labels["vblank"]
rom[0x40] = 0xC3
rom[0x41] = handler & 0xFF
rom[0x42] = (handler >> 8) & 0xFF
rom[base:base + len(image)] = image
title = b"CATCH"
rom[0x134:0x134 + len(title)] = title
rom[0x147] = 0
rom[0x148] = 0
rom[0x149] = 0
rom[0x14D] = (-sum(rom[0x134:0x14D]) - (0x14D - 0x134)) & 0xFF
path = Path(__file__).resolve().parent / "catch.gb"
path.write_bytes(rom)
print(f"Created {path.name} ({len(rom)} bytes), entry 0x{base:04X}, handler 0x{handler:04X}.")
