"""Assemble an original 32 KiB ROM that scrolls a checker and moves a sprite."""
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

    def imm16(self, name):
        here = len(self.code)
        self.emit(0, 0)
        self.fixups.append((here, name))

    def jr_nz(self, name):
        self.emit(0x20, 0)
        self.fixups.append((len(self.code) - 1, name))

    def link(self, base):
        image = bytearray(self.code)
        for at, name in self.fixups:
            target = base + self.labels[name]
            if image[at - 1] == 0x20:
                relative = target - (base + at + 1)
                if not -128 <= relative <= 127:
                    raise SystemExit(f"{name} is out of JR range ({relative})")
                image[at] = relative & 0xFF
            else:
                image[at] = target & 0xFF
                image[at + 1] = (target >> 8) & 0xFF
        return image


base = 0x0150
program = Asm()
program.label("start")
program.emit(0xF3)                 # DI
program.emit(0x3E, 0x00)           # LD A,0
program.emit(0xE0, 0x40)           # LDH (LCDC),A
program.emit(0x21)                 # LD HL,tiles
program.imm16("tiles")
program.emit(0x11, 0x00, 0x80)     # LD DE,0x8000
program.emit(0x01, 48, 0x00)       # LD BC,48
program.label("copy")
program.emit(0x2A)                 # LD A,(HL+)
program.emit(0x12)                 # LD (DE),A
program.emit(0x13)                 # INC DE
program.emit(0x0B)                 # DEC BC
program.emit(0x78)                 # LD A,B
program.emit(0xB1)                 # OR C
program.jr_nz("copy")
program.emit(0x21, 0x00, 0x98)     # LD HL,0x9800
program.emit(0x01, 0x40, 0x02)     # LD BC,576
program.emit(0x1E, 0x00)           # LD E,0
program.label("map")
program.emit(0x7B)                 # LD A,E
program.emit(0xE6, 0x01)           # AND 1
program.emit(0x22)                 # LD (HL+),A
program.emit(0x1C)                 # INC E
program.emit(0x0B)                 # DEC BC
program.emit(0x78)                 # LD A,B
program.emit(0xB1)                 # OR C
program.jr_nz("map")
program.emit(0x3E, 0x40)           # sprite Y = screen 48
program.emit(0xEA, 0x00, 0xFE)
program.emit(0x3E, 0x28)           # sprite X = screen 32
program.emit(0xEA, 0x01, 0xFE)
program.emit(0x3E, 0x02)
program.emit(0xEA, 0x02, 0xFE)
program.emit(0xAF)                 # XOR A
program.emit(0xEA, 0x03, 0xFE)
program.emit(0x3E, 0xE4)
program.emit(0xE0, 0x47)
program.emit(0xE0, 0x48)
program.emit(0x3E, 0x93)           # LCD, BG, sprites, 8000 tiles
program.emit(0xE0, 0x40)
program.emit(0x3E, 0x01)
program.emit(0xEA, 0xFF, 0xFF)     # IE = VBlank
program.emit(0xFB)                 # EI
program.label("wait")
program.emit(0x76)                 # HALT
program.emit(0x18, 0xFD)           # JR wait
program.label("vblank")
program.emit(0xF5)                 # PUSH AF
program.emit(0xF0, 0x43)           # LDH A,(SCX)
program.emit(0x3C)                 # INC A
program.emit(0xE0, 0x43)
program.emit(0xFA, 0x01, 0xFE)     # sprite X
program.emit(0x3C)
program.emit(0xEA, 0x01, 0xFE)
program.emit(0xF1)                 # POP AF
program.emit(0xD9)                 # RETI
program.label("tiles")
program.emit(*([0xF0] * 16))       # left half color 3
program.emit(*([0x0F] * 16))       # right half color 3
program.emit(*([0xFF] * 16))       # solid sprite tile

image = program.link(base)
if base + len(image) > 0x8000:
    raise SystemExit("program does not fit in a 32 KiB ROM")

rom = bytearray(32 * 1024)
rom[0x0100] = 0xC3
rom[0x0101] = base & 0xFF
rom[0x0102] = (base >> 8) & 0xFF
rom[0x0040] = 0xC3
handler = base + program.labels["vblank"]
rom[0x0041] = handler & 0xFF
rom[0x0042] = (handler >> 8) & 0xFF
rom[base:base + len(image)] = image
title = b"VIDEO DEMO"
rom[0x134:0x134 + len(title)] = title
rom[0x147] = 0
rom[0x148] = 0
rom[0x149] = 0
rom[0x14D] = (-sum(rom[0x134:0x14D]) - (0x14D - 0x134)) & 0xFF
path = Path(__file__).resolve().parent / "video-demo.gb"
path.write_bytes(rom)
print(f"Created {path.name} ({len(rom)} bytes) at entry 0x{base:04X}, handler 0x{handler:04X}.")
