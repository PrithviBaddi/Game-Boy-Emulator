#include "gb_cartridge.h"
#include "gb_check.h"
#include "gb_cpu.h"
#include "gb_memory.h"
#include "gb_ppu.h"
#include "gb_timer.h"

#include <stdlib.h>
#include <string.h>

static int fresh(uint8_t *rom, GbMemory *memory)
{
    memset(rom, 0, 0x8000);
    GB_REQUIRE(gb_memory_init(memory, rom, 0x8000));
    return 0;
}

static int enable_lcd(GbMemory *memory, uint8_t lcdc)
{
    gb_memory_write(memory, 0xff47, 0xe4);
    gb_memory_write(memory, 0xff48, 0xe4);
    gb_memory_write(memory, 0xff49, 0xe4);
    gb_memory_write(memory, 0xff40, lcdc);
    return 0;
}

static int test_line_and_frame_timing(void)
{
    uint8_t rom[0x8000];
    GbMemory memory;
    GB_REQUIRE(fresh(rom, &memory) == 0);
    GB_REQUIRE(enable_lcd(&memory, 0x80) == 0);
    GB_REQUIRE(gb_ppu_mode(&memory) == 2);
    GB_REQUIRE((gb_memory_read(&memory, 0xff41) & 0x03) == 2);

    gb_timer_advance(&memory, 76);
    GB_REQUIRE((gb_memory_read(&memory, 0xff41) & 0x03) == 2);
    gb_timer_advance(&memory, 4);
    GB_REQUIRE((gb_memory_read(&memory, 0xff41) & 0x03) == 3);

    gb_timer_advance(&memory, 168);
    GB_REQUIRE((gb_memory_read(&memory, 0xff41) & 0x03) == 3);
    gb_timer_advance(&memory, 4);
    GB_REQUIRE((gb_memory_read(&memory, 0xff41) & 0x03) == 0);
    GB_REQUIRE(gb_memory_read(&memory, 0xff44) == 0);

    gb_timer_advance(&memory, 456 - 252);
    GB_REQUIRE(gb_memory_read(&memory, 0xff44) == 1);
    GB_REQUIRE((gb_memory_read(&memory, 0xff41) & 0x03) == 2);

    gb_memory_write(&memory, 0xff0f, 0);
    gb_timer_advance(&memory, 143 * 456);
    GB_REQUIRE(gb_memory_read(&memory, 0xff44) == 144);
    GB_REQUIRE((gb_memory_read(&memory, 0xff41) & 0x03) == 1);
    GB_REQUIRE((gb_memory_read(&memory, 0xff0f) & 0x01) == 0x01);
    GB_REQUIRE(gb_ppu_frame_ready(&memory));

    gb_timer_advance(&memory, 9 * 456);
    GB_REQUIRE(gb_memory_read(&memory, 0xff44) == 153);
    gb_timer_advance(&memory, 456);
    GB_REQUIRE(gb_memory_read(&memory, 0xff44) == 0);
    return 0;
}

static int test_stat_and_lcd_toggle(void)
{
    uint8_t rom[0x8000];
    GbMemory memory;
    GB_REQUIRE(fresh(rom, &memory) == 0);
    gb_memory_write(&memory, 0xff45, 1);
    gb_memory_write(&memory, 0xff41, 0x40);
    gb_memory_write(&memory, 0xff0f, 0);
    GB_REQUIRE(enable_lcd(&memory, 0x80) == 0);
    GB_REQUIRE((gb_memory_read(&memory, 0xff41) & 0x07) == 0x02); /* mode 2, LY 0 != LYC */
    gb_timer_advance(&memory, 456);
    GB_REQUIRE(gb_memory_read(&memory, 0xff44) == 1);
    GB_REQUIRE((gb_memory_read(&memory, 0xff41) & 0x04) == 0x04);
    GB_REQUIRE((gb_memory_read(&memory, 0xff0f) & 0x02) == 0x02);

    uint8_t saved = memory.vram[0];
    gb_memory_write(&memory, 0xff40, 0x00);
    GB_REQUIRE(gb_memory_read(&memory, 0xff44) == 0);
    GB_REQUIRE(gb_ppu_mode(&memory) == 0);
    gb_timer_advance(&memory, 456 * 3);
    GB_REQUIRE(gb_memory_read(&memory, 0xff44) == 0);
    gb_memory_write(&memory, 0x8000, 0x5a);
    GB_REQUIRE(gb_memory_read(&memory, 0x8000) == 0x5a);
    gb_memory_write(&memory, 0xfe00, 0x17);
    GB_REQUIRE(gb_memory_read(&memory, 0xfe00) == 0x17);
    memory.vram[0] = saved;

    GB_REQUIRE(enable_lcd(&memory, 0x80) == 0);
    GB_REQUIRE(gb_memory_read(&memory, 0xff44) == 0);
    GB_REQUIRE(gb_ppu_mode(&memory) == 2);
    gb_memory_write(&memory, 0xff41, 0x12);
    GB_REQUIRE((gb_memory_read(&memory, 0xff41) & 0x78) == 0x10);
    gb_memory_write(&memory, 0xff44, 0x99);
    GB_REQUIRE(gb_memory_read(&memory, 0xff44) == 0);
    return 0;
}

static int test_access_boundaries(void)
{
    uint8_t rom[0x8000];
    GbMemory memory;
    GB_REQUIRE(fresh(rom, &memory) == 0);
    GB_REQUIRE(enable_lcd(&memory, 0x80) == 0);
    memory.vram[0] = 0x11;
    memory.oam[0] = 0x22;

    gb_memory_write(&memory, 0x8000, 0x33);
    gb_memory_write(&memory, 0xfe00, 0x44);
    GB_REQUIRE(gb_memory_read(&memory, 0x8000) == 0x33);
    GB_REQUIRE(memory.oam[0] == 0x22);
    GB_REQUIRE(gb_memory_read(&memory, 0xfe00) == 0xff);

    gb_timer_advance(&memory, 80);
    GB_REQUIRE((gb_memory_read(&memory, 0xff41) & 0x03) == 3);
    gb_memory_write(&memory, 0x8000, 0x55);
    gb_memory_write(&memory, 0xfe00, 0x66);
    GB_REQUIRE(memory.vram[0] == 0x33);
    GB_REQUIRE(gb_memory_read(&memory, 0x8000) == 0xff);
    GB_REQUIRE(memory.oam[0] == 0x22);

    gb_timer_advance(&memory, 172);
    GB_REQUIRE((gb_memory_read(&memory, 0xff41) & 0x03) == 0);
    gb_memory_write(&memory, 0x8000, 0x77);
    gb_memory_write(&memory, 0xfe00, 0x88);
    GB_REQUIRE(gb_memory_read(&memory, 0x8000) == 0x77);
    GB_REQUIRE(gb_memory_read(&memory, 0xfe00) == 0x88);
    return 0;
}

static uint8_t pixel(const GbMemory *memory, unsigned x, unsigned y)
{
    return gb_ppu_pixels(memory)[y * 160u + x];
}

static int render_line(GbMemory *memory, unsigned line)
{
    uint32_t need = (uint32_t)(line * 456u + 252u);
    gb_timer_advance(memory, need);
    return 0;
}

static int test_background_and_window(void)
{
    uint8_t rom[0x8000];
    GbMemory memory;
    GB_REQUIRE(fresh(rom, &memory) == 0);
    memory.vram[0] = 0x80;
    memory.vram[1] = 0x00;
    GB_REQUIRE(enable_lcd(&memory, 0x91) == 0);
    GB_REQUIRE(render_line(&memory, 0) == 0);
    GB_REQUIRE(pixel(&memory, 0, 0) == 1);
    GB_REQUIRE(pixel(&memory, 1, 0) == 0);

    gb_memory_write(&memory, 0xff40, 0x00);
    gb_memory_write(&memory, 0xff43, 1);
    GB_REQUIRE(enable_lcd(&memory, 0x91) == 0);
    GB_REQUIRE(render_line(&memory, 0) == 0);
    GB_REQUIRE(pixel(&memory, 0, 0) == 0);

    gb_memory_write(&memory, 0xff40, 0x00);
    memory.vram[0x1000] = 0xff;
    memory.vram[0x1001] = 0xff;
    gb_memory_write(&memory, 0xff43, 0);
    GB_REQUIRE(enable_lcd(&memory, 0x81) == 0);
    GB_REQUIRE(render_line(&memory, 0) == 0);
    GB_REQUIRE(pixel(&memory, 0, 0) == 3);

    gb_memory_write(&memory, 0xff40, 0x00);
    memory.vram[0x1c00] = 1;
    memory.vram[16] = 0x00;
    memory.vram[17] = 0xff;
    gb_memory_write(&memory, 0xff43, 0);
    GB_REQUIRE(enable_lcd(&memory, 0x99) == 0);
    GB_REQUIRE(render_line(&memory, 0) == 0);
    GB_REQUIRE(pixel(&memory, 0, 0) == 2);

    gb_memory_write(&memory, 0xff40, 0x00);
    memset(memory.vram, 0, sizeof memory.vram);
    memory.vram[0] = 0xff;
    memory.vram[1] = 0xff;
    memory.vram[2] = 0x00;
    memory.vram[3] = 0xff;
    memory.vram[0x1c00] = 0;
    gb_memory_write(&memory, 0xff4a, 0);
    gb_memory_write(&memory, 0xff4b, 7);
    GB_REQUIRE(enable_lcd(&memory, 0xd1) == 0); /* LCD, window map 9C00, window on, 8000 tiles */
    GB_REQUIRE(render_line(&memory, 0) == 0);
    GB_REQUIRE(pixel(&memory, 0, 0) == 3);
    gb_timer_advance(&memory, 456);
    GB_REQUIRE(pixel(&memory, 0, 1) == 2);
    return 0;
}

static void put_sprite(GbMemory *memory, unsigned index, uint8_t y, uint8_t x, uint8_t tile, uint8_t attr)
{
    memory->oam[index * 4u] = y;
    memory->oam[index * 4u + 1] = x;
    memory->oam[index * 4u + 2] = tile;
    memory->oam[index * 4u + 3] = attr;
}

static int test_objects(void)
{
    uint8_t rom[0x8000];
    GbMemory memory;
    GB_REQUIRE(fresh(rom, &memory) == 0);
    memory.vram[0] = 0x80;
    memory.vram[1] = 0x00;
    put_sprite(&memory, 0, 16, 8, 0, 0x00);
    GB_REQUIRE(enable_lcd(&memory, 0x92) == 0);
    GB_REQUIRE(render_line(&memory, 0) == 0);
    GB_REQUIRE(pixel(&memory, 0, 0) == 1);
    GB_REQUIRE(pixel(&memory, 1, 0) == 0);

    gb_memory_write(&memory, 0xff40, 0x00);
    memset(memory.oam, 0, sizeof memory.oam);
    memset(memory.vram, 0, 16);
    memory.vram[0] = 0xff;
    memory.vram[1] = 0xff;
    memory.vram[16] = 0xff;
    memory.vram[17] = 0x00;
    put_sprite(&memory, 0, 16, 8, 1, 0x80);
    GB_REQUIRE(enable_lcd(&memory, 0x93) == 0);
    GB_REQUIRE(render_line(&memory, 0) == 0);
    GB_REQUIRE(pixel(&memory, 0, 0) == 3);

    gb_memory_write(&memory, 0xff40, 0x00);
    memset(memory.oam, 0, sizeof memory.oam);
    memset(memory.vram, 0, 32);
    memory.vram[0] = 0x80;
    memory.vram[1] = 0x00;
    put_sprite(&memory, 0, 16, 8, 0, 0x30);
    GB_REQUIRE(enable_lcd(&memory, 0x92) == 0);
    gb_memory_write(&memory, 0xff49, 0x0c);
    GB_REQUIRE(render_line(&memory, 0) == 0);
    GB_REQUIRE(pixel(&memory, 0, 0) == 0);
    GB_REQUIRE(pixel(&memory, 7, 0) == 3);

    gb_memory_write(&memory, 0xff40, 0x00);
    memset(memory.oam, 0, sizeof memory.oam);
    memset(memory.vram, 0, 48);
    memory.vram[0] = 0x80;
    memory.vram[1] = 0x00;
    memory.vram[16] = 0x00;
    memory.vram[17] = 0x80;
    put_sprite(&memory, 0, 16, 8, 0, 0x00);
    GB_REQUIRE(enable_lcd(&memory, 0x97) == 0);
    GB_REQUIRE(render_line(&memory, 0) == 0);
    GB_REQUIRE(pixel(&memory, 0, 0) == 1);
    gb_timer_advance(&memory, 8 * 456);
    GB_REQUIRE(pixel(&memory, 0, 8) == 2);

    gb_memory_write(&memory, 0xff40, 0x00);
    memset(memory.oam, 0, sizeof memory.oam);
    memset(memory.vram, 0, 32);
    memory.vram[0] = 0xff;
    memory.vram[1] = 0x00;
    memory.vram[16] = 0xff;
    memory.vram[17] = 0xff;
    for (unsigned i = 0; i < 10; ++i)
        put_sprite(&memory, i, 16, (uint8_t)(8 + i * 8), 0, 0x00);
    put_sprite(&memory, 10, 16, 88, 1, 0x00);
    GB_REQUIRE(enable_lcd(&memory, 0x92) == 0);
    GB_REQUIRE(render_line(&memory, 0) == 0);
    GB_REQUIRE(pixel(&memory, 0, 0) == 1);
    GB_REQUIRE(pixel(&memory, 80, 0) == 0);
    return 0;
}

static int test_oam_dma(void)
{
    uint8_t rom[0x8000];
    GbMemory memory;
    GbCpu cpu;
    GB_REQUIRE(fresh(rom, &memory) == 0);
    rom[0x0000] = 0xab;
    memory.wram[0] = 0x12;
    memory.wram[1] = 0x34;
    memory.vram[0] = 0x56;
    memory.oam[0] = 0x00;

    gb_memory_write(&memory, 0xff46, 0xc0);
    GB_REQUIRE(memory.oam[0] == 0x00);
    gb_timer_advance(&memory, 4);
    GB_REQUIRE(memory.oam[0] == 0x12);
    GB_REQUIRE(memory.ppu.dma_active);
    GB_REQUIRE(gb_memory_read(&memory, 0xfe00) == 0xff);
    GB_REQUIRE(gb_memory_read(&memory, 0x0000) == 0xff);
    gb_memory_write(&memory, 0xff80, 0x99);
    GB_REQUIRE(gb_memory_read(&memory, 0xff80) == 0x99);
    gb_memory_write(&memory, 0xc002, 0x77);
    GB_REQUIRE(memory.wram[2] == 0x00);

    gb_timer_advance(&memory, 4 * 159);
    GB_REQUIRE(!memory.ppu.dma_active);
    GB_REQUIRE(gb_memory_read(&memory, 0xfe00) == 0x12);
    GB_REQUIRE(gb_memory_read(&memory, 0xfe01) == 0x34);

    gb_memory_write(&memory, 0xff46, 0x80);
    gb_timer_advance(&memory, 4);
    GB_REQUIRE(memory.oam[0] == 0x56);
    gb_memory_write(&memory, 0xff46, 0xc0);
    GB_REQUIRE(memory.ppu.dma_offset == 0);
    gb_timer_advance(&memory, 4);
    GB_REQUIRE(memory.oam[0] == 0x12);

    GB_REQUIRE(fresh(rom, &memory) == 0);
    memory.hram[0] = 0x3e;
    memory.hram[1] = 0x42;
    memory.hram[2] = 0x00;
    gb_memory_write(&memory, 0xff46, 0xc0);
    gb_cpu_init(&cpu);
    cpu.pc = 0xff80;
    unsigned cycles = 0;
    GB_REQUIRE(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(cpu.a == 0x42);
    GB_REQUIRE(memory.ppu.dma_active);
    return 0;
}

static int test_video_demo_rom(void)
{
#ifndef DEMO_ROM
    return 0;
#else
    uint8_t *rom = NULL;
    size_t length = 0;
    char error[160];
    GB_REQUIRE(gb_cartridge_read_file(DEMO_ROM, &rom, &length, error, sizeof error));
    GB_REQUIRE(length == 0x8000);
    GbMemory memory;
    GbCpu cpu;
    GB_REQUIRE(gb_memory_init(&memory, rom, length));
    gb_cpu_init_dmg_post_boot(&cpu);
    gb_ppu_apply_dmg_post_boot(&memory);
    int frames = 0;
    uint8_t first = 0;
    uint8_t second = 0;
    for (int steps = 0; steps < 5000000 && frames < 2; ++steps)
    {
        unsigned spent = 0;
        GbStepResult result = gb_cpu_step(&cpu, &memory, &spent);
        GB_REQUIRE(result == GB_STEP_OK || result == GB_STEP_HALTED);
        if (gb_ppu_frame_ready(&memory))
        {
            uint8_t sample = pixel(&memory, 3, 0);
            if (frames == 0)
                first = sample;
            else
                second = sample;
            gb_ppu_acknowledge_frame(&memory);
            frames++;
        }
    }
    free(rom);
    GB_REQUIRE(frames == 2);
    GB_REQUIRE(first == 3);
    GB_REQUIRE(second == 0);
    return 0;
#endif
}

int main(void)
{
    if (test_line_and_frame_timing() != 0) return 1;
    if (test_stat_and_lcd_toggle() != 0) return 1;
    if (test_access_boundaries() != 0) return 1;
    if (test_background_and_window() != 0) return 1;
    if (test_objects() != 0) return 1;
    if (test_oam_dma() != 0) return 1;
    if (test_video_demo_rom() != 0) return 1;
    return 0;
}
