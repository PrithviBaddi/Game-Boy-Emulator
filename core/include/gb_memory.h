#ifndef GB_MEMORY_H
#define GB_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DMG divider and timer. Owned by the memory bus so a ROM load zeroes it
 * with the rest of the machine. There is no separate global timer. */
typedef struct {
    uint16_t counter; /* increments by 4 T-cycles once per machine cycle */
    uint8_t phase;    /* leftover T-cycles, kept until they make a machine cycle */
    uint8_t tima;
    uint8_t tma;
    uint8_t tac; /* stored bits 0–2 only; reads force bits 3–7 to 1 */
    bool overflow_pending; /* TIMA is 0 until the next machine cycle reloads it */
    bool reloading;        /* this machine cycle copied TMA into TIMA */
} GbTimer;

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
    GbTimer timer;
} GbMemory;

/* Returns false for a missing or incorrectly sized ROM. */
bool gb_memory_init(GbMemory *memory, const uint8_t *rom, size_t rom_size);
uint8_t gb_memory_read(const GbMemory *memory, uint16_t address);
void gb_memory_write(GbMemory *memory, uint16_t address, uint8_t value);

#ifdef __cplusplus
}
#endif
#endif
