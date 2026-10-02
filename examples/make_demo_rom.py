"""Create a harmless header demo, not a playable Game Boy game."""
from pathlib import Path

rom = bytearray(32 * 1024)
rom[0x134:0x140] = b"POCKETGLASS!"
rom[0x147] = 0  # ROM-only cartridge
rom[0x148] = 0  # 32 KiB
rom[0x149] = 0  # no external RAM
rom[0x14D] = (-sum(rom[0x134:0x14D]) - (0x14D - 0x134)) & 0xFF
path = Path("examples/header-demo.gb")
path.write_bytes(rom)
print(f"Created {path} ({len(rom)} bytes). It is metadata only, not a game.")
