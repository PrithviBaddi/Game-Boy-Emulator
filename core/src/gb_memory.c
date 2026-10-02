#include "gb_memory.h"

#include <string.h>

bool gb_memory_init(GbMemory *memory, const uint8_t *rom, size_t rom_size) {
    if (!memory || !rom || rom_size != 0x8000) return false;
    memset(memory, 0, sizeof *memory);
    memory->rom = rom;
    memory->rom_size = rom_size;
    return true;
}

uint8_t gb_memory_read(const GbMemory *memory, uint16_t address) {
    if (address < 0x8000) return memory->rom[address];
    if (address < 0xa000) return memory->vram[address - 0x8000];
    if (address < 0xc000) return 0xff; /* no external RAM in ROM-only cart */
    if (address < 0xe000) return memory->wram[address - 0xc000];
    if (address < 0xfe00) return memory->wram[address - 0xe000]; /* echo */
    if (address < 0xfea0) return memory->oam[address - 0xfe00];
    if (address < 0xff00) return 0xff; /* unusable region */
    if (address < 0xff80) return memory->io[address - 0xff00];
    if (address < 0xffff) return memory->hram[address - 0xff80];
    return memory->interrupt_enable;
}

void gb_memory_write(GbMemory *memory, uint16_t address, uint8_t value) {
    if (address < 0x8000) return; /* no bank switching for ROM-only cart */
    if (address < 0xa000) memory->vram[address - 0x8000] = value;
    else if (address < 0xc000) return;
    else if (address < 0xe000) memory->wram[address - 0xc000] = value;
    else if (address < 0xfe00) memory->wram[address - 0xe000] = value;
    else if (address < 0xfea0) memory->oam[address - 0xfe00] = value;
    else if (address < 0xff00) return;
    else if (address < 0xff80) memory->io[address - 0xff00] = value;
    else if (address < 0xffff) memory->hram[address - 0xff80] = value;
    else memory->interrupt_enable = value;
}
