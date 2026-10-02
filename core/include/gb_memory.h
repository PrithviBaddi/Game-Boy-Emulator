#ifndef GB_MEMORY_H
#define GB_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initial memory bus for an original Game Boy with a 32 KiB ROM-only cart. */
typedef struct {
    const uint8_t *rom;  /* borrowed: caller keeps cartridge bytes alive */
    size_t rom_size;
    uint8_t vram[0x2000];
    uint8_t wram[0x2000];
    uint8_t oam[0xa0];
    uint8_t io[0x80];
    uint8_t hram[0x7f];
    uint8_t interrupt_enable;
} GbMemory;

/* Returns false for a missing or incorrectly sized ROM. */
bool gb_memory_init(GbMemory *memory, const uint8_t *rom, size_t rom_size);
uint8_t gb_memory_read(const GbMemory *memory, uint16_t address);
void gb_memory_write(GbMemory *memory, uint16_t address, uint8_t value);

#ifdef __cplusplus
}
#endif
#endif
