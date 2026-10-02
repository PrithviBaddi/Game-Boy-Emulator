#include "gb_memory.h"

#include <string.h>
#include "gb_ppu.h"
#include "gb_timer.h"

static bool timer_address(uint16_t address)
{
    return address >= 0xff04 && address <= 0xff07;
}

static bool ppu_address(uint16_t address)
{
    return address == 0xff40 || (address >= 0xff41 && address <= 0xff4b);
}

bool gb_memory_init(GbMemory *memory, const uint8_t *rom, size_t rom_size) {
    if (!memory || !rom || rom_size != 0x8000) return false;
    memset(memory, 0, sizeof *memory);
    memory->rom = rom;
    memory->rom_size = rom_size;
    return true;
}

uint8_t gb_memory_read(const GbMemory *memory, uint16_t address) {
    if (gb_ppu_dma_blocks_cpu(memory, address))
        return 0xff;
    if ((address >= 0x8000 && address <= 0x9fff) ||
        (address >= 0xfe00 && address <= 0xfe9f)) {
        if (!gb_ppu_cpu_can_access(memory, address))
            return 0xff;
    }
    if (address < 0x8000) return memory->rom[address];
    if (address < 0xa000) return memory->vram[address - 0x8000];
    if (address < 0xc000) return 0xff; /* no external RAM in ROM-only cart */
    if (address < 0xe000) return memory->wram[address - 0xc000];
    if (address < 0xfe00) return memory->wram[address - 0xe000]; /* echo */
    if (address < 0xfea0) return memory->oam[address - 0xfe00];
    if (address < 0xff00) return 0xff; /* unusable region */
    if (address < 0xff80) {
        if (timer_address(address)) return gb_timer_read(memory, address);
        if (ppu_address(address)) return gb_ppu_read(memory, address);
        uint8_t value = memory->io[address - 0xff00];
        /* IF bits 5–7 are unused and read as 1 on a DMG. */
        if (address == 0xff0f) return (uint8_t)(value | 0xe0);
        return value;
    }
    if (address < 0xffff) return memory->hram[address - 0xff80];
    return memory->interrupt_enable;
}

void gb_memory_write(GbMemory *memory, uint16_t address, uint8_t value) {
    if (gb_ppu_dma_blocks_cpu(memory, address))
        return;
    if (address < 0x8000) return; /* no bank switching for ROM-only cart */
    if (address < 0xa000) {
        if (!gb_ppu_cpu_can_access(memory, address))
            return;
        memory->vram[address - 0x8000] = value;
        return;
    }
    else if (address < 0xc000) return;
    else if (address < 0xe000) memory->wram[address - 0xc000] = value;
    else if (address < 0xfe00) memory->wram[address - 0xe000] = value;
    else if (address < 0xfea0) {
        if (!gb_ppu_cpu_can_access(memory, address))
            return;
        memory->oam[address - 0xfe00] = value;
    }
    else if (address < 0xff00) return;
    else if (address < 0xff80) {
        if (timer_address(address)) {
            gb_timer_write(memory, address, value);
            return;
        }
        if (ppu_address(address)) {
            gb_ppu_write(memory, address, value);
            return;
        }
        if (address == 0xff0f) value &= 0x1f;
        memory->io[address - 0xff00] = value;
    }
    else if (address < 0xffff) memory->hram[address - 0xff80] = value;
    else memory->interrupt_enable = value;
}
