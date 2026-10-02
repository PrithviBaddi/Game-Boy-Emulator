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
    bool dma_wait;   /* the cycle after FF46, before the first copied byte */
    bool dma_finish; /* last byte is copied; OAM stays locked until the next cycle */
    uint8_t dma_page;
    uint8_t dma_offset;
} GbPpu;

/* Button bits for gb_joypad_set_pressed. 1 means the button is held.
 * These are not the active-low bits of FF00. */
enum {
    GB_BTN_RIGHT = 1 << 0,
    GB_BTN_LEFT = 1 << 1,
    GB_BTN_UP = 1 << 2,
    GB_BTN_DOWN = 1 << 3,
    GB_BTN_A = 1 << 4,
    GB_BTN_B = 1 << 5,
    GB_BTN_SELECT = 1 << 6,
    GB_BTN_START = 1 << 7
};

typedef enum {
    GB_MAPPER_ROM = 0,
    GB_MAPPER_MBC1,
    GB_MAPPER_MBC3
} GbMapper;

/* Cartridge banking and external RAM. ROM bytes stay owned by the caller.
 * eram is the external RAM image, including unused banks as zeros. */
typedef struct {
    GbMapper mapper;
    bool has_ram;
    bool battery;
    bool has_rtc;
    bool ram_enable;
    bool save_blocked; /* a mismatched save was not loaded; do not overwrite it */
    uint32_t ram_bytes;
    uint8_t rom_bank;
    uint8_t ram_bank;
    uint8_t mode; /* MBC1: 0 simple ROM banking, 1 RAM banking */
    uint8_t joy_select; /* FF00 bits 4–5; 0 selects that group */
    uint8_t joy_pressed;
    uint8_t rtc[5]; /* seconds, minutes, hours, day low, day high */
    uint8_t rtc_latched[5];
    uint8_t rtc_latch_prev;
    int64_t rtc_unix; /* host time of the last RTC catch-up */
    uint8_t eram[0x8000];
} GbCart;

/* Bus for a DMG. gb_memory_init maps a raw 32 KiB ROM with no header check.
 * gb_memory_load_cartridge maps a header and supports ROM, MBC1, and MBC3. */
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
    GbCart cart;
} GbMemory;

/* Returns false for a missing ROM or any size other than exactly 32 KiB. */
bool gb_memory_init(GbMemory *memory, const uint8_t *rom, size_t rom_size);
/* Maps a cartridge whose file size matches the header. Writes an error on failure. */
bool gb_memory_load_cartridge(GbMemory *memory, const uint8_t *rom, size_t rom_size,
                              char *error, size_t error_size);
uint8_t gb_memory_read(const GbMemory *memory, uint16_t address);
void gb_memory_write(GbMemory *memory, uint16_t address, uint8_t value);
/* DMA source read. Echo and the FE/FF pages wrap into the 8 KiB of work RAM. */
uint8_t gb_memory_dma_peek(const GbMemory *memory, uint16_t address);
/* Replace the held-button mask. Returns true when at least one button changes
 * from released to held, which is what leaves STOP. */
bool gb_joypad_set_pressed(GbMemory *memory, uint8_t pressed);
bool gb_memory_load_save(GbMemory *memory, const char *path, char *error, size_t error_size);
bool gb_memory_store_save(const GbMemory *memory, const char *path, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
