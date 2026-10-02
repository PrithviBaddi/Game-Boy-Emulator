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

enum {
    GB_LCD_WIDTH = 160,
    GB_LCD_HEIGHT = 144
};

/* DMG picture processor and OAM DMA. Owned by the bus, same as the timer.
 * mode3_stall_dots is the hook for later fetcher stalls; it is 0 until then,
 * so mode 3 is a fixed 172 dots. */
typedef struct {
    uint8_t lcdc, stat, scy, scx, ly, lyc, dma, bgp, obp0, obp1, wy, wx;
    uint16_t dot; /* 0..455, position within the current line, in dots */
    uint16_t mode3_stall_dots;
    uint8_t window_line;
    uint8_t last_mode;
    bool lcd_on;
    bool frame_ready;
    bool stat_mode0, stat_mode1, stat_mode2, stat_lyc;
    uint8_t pixels[GB_LCD_WIDTH * GB_LCD_HEIGHT]; /* shade 0..3 after the palette */
    bool dma_active;
    uint8_t dma_page;
    uint8_t dma_offset;
} GbPpu;

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
    GbPpu ppu;
} GbMemory;

/* Returns false for a missing or incorrectly sized ROM. */
bool gb_memory_init(GbMemory *memory, const uint8_t *rom, size_t rom_size);
uint8_t gb_memory_read(const GbMemory *memory, uint16_t address);
void gb_memory_write(GbMemory *memory, uint16_t address, uint8_t value);

#ifdef __cplusplus
}
#endif
#endif
