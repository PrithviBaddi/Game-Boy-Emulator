#include "gb_cartridge.h"
#include "gb_check.h"
#include "gb_cpu.h"
#include "gb_memory.h"
#include "gb_ppu.h"

#include <stdio.h>
#include <stdlib.h>

static int run_frames(GbCpu *cpu, GbMemory *memory, int frames)
{
    int seen = 0;
    for (int steps = 0; steps < 4000000 && seen < frames; ++steps)
    {
        unsigned spent = 0;
        GbStepResult result = gb_cpu_step(cpu, memory, &spent);
        GB_REQUIRE(result == GB_STEP_OK || result == GB_STEP_HALTED);
        if (gb_ppu_frame_ready(memory))
        {
            gb_ppu_acknowledge_frame(memory);
            seen++;
        }
    }
    GB_REQUIRE(seen == frames);
    return 0;
}

static int load_game(GbCpu *cpu, GbMemory *memory, uint8_t **rom)
{
    size_t length = 0;
    char error[160];
    GB_REQUIRE(gb_cartridge_read_file(GAME_ROM, rom, &length, error, sizeof error));
    GB_REQUIRE(gb_memory_load_cartridge(memory, *rom, length, error, sizeof error));
    gb_cpu_init_dmg_post_boot(cpu);
    gb_ppu_apply_dmg_post_boot(memory);
    return 0;
}

static int test_catch(void)
{
    uint8_t *rom = NULL;
    GbMemory memory;
    GbCpu cpu;
    GB_REQUIRE(load_game(&cpu, &memory, &rom) == 0);
    GB_REQUIRE(run_frames(&cpu, &memory, 3) == 0);
    GB_REQUIRE(memory.wram[0] == 0);

    gb_joypad_set_pressed(&memory, GB_BTN_START);
    GB_REQUIRE(run_frames(&cpu, &memory, 2) == 0);
    GB_REQUIRE(memory.wram[0] == 1);
    gb_joypad_set_pressed(&memory, 0);

    gb_joypad_set_pressed(&memory, GB_BTN_LEFT);
    GB_REQUIRE(run_frames(&cpu, &memory, 24) == 0);
    GB_REQUIRE(memory.wram[1] <= 36);
    gb_joypad_set_pressed(&memory, 0);
    for (int i = 0; i < 20 && memory.wram[0] == 1; ++i)
        GB_REQUIRE(run_frames(&cpu, &memory, 1) == 0);
    GB_REQUIRE(memory.wram[0] == 3);

    free(rom);
    GB_REQUIRE(load_game(&cpu, &memory, &rom) == 0);
    gb_joypad_set_pressed(&memory, GB_BTN_START);
    GB_REQUIRE(run_frames(&cpu, &memory, 2) == 0);
    gb_joypad_set_pressed(&memory, 0);
    GB_REQUIRE(memory.wram[0] == 1);
    for (int i = 0; i < 250 && memory.wram[0] == 1; ++i)
        GB_REQUIRE(run_frames(&cpu, &memory, 1) == 0);
    GB_REQUIRE(memory.wram[0] == 2);
    GB_REQUIRE(memory.wram[2] == 3);
    int ink = 0;
    for (int i = 0; i < 160 * 144; ++i)
        ink += gb_ppu_pixels(&memory)[i] != 0;
    GB_REQUIRE(ink > 20);
    gb_joypad_set_pressed(&memory, GB_BTN_START);
    GB_REQUIRE(run_frames(&cpu, &memory, 4) == 0);
    GB_REQUIRE(memory.wram[0] == 0);
    gb_joypad_set_pressed(&memory, 0);
    GB_REQUIRE(run_frames(&cpu, &memory, 2) == 0);
    gb_joypad_set_pressed(&memory, GB_BTN_START);
    GB_REQUIRE(run_frames(&cpu, &memory, 4) == 0);
    GB_REQUIRE(memory.wram[0] == 1);
    GB_REQUIRE(memory.wram[2] == 0);
    free(rom);
    return 0;
}

int main(void)
{
    return test_catch();
}
