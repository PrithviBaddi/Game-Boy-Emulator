#include "gb_apu.h"
#include "gb_check.h"
#include "gb_memory.h"
#include "gb_timer.h"

#include <stdlib.h>
#include <string.h>

static int setup(uint8_t *rom, GbMemory *memory)
{
    memset(rom, 0, 0x8000);
    GB_REQUIRE(gb_memory_init(memory, rom, 0x8000));
    return 0;
}

static int test_power_and_square(void)
{
    uint8_t rom[0x8000];
    GbMemory memory;
    GB_REQUIRE(setup(rom, &memory) == 0);
    GB_REQUIRE(gb_memory_read(&memory, 0xff26) == 0x70);
    gb_memory_write(&memory, 0xff12, 0xf0);
    GB_REQUIRE(gb_memory_read(&memory, 0xff12) == 0x00);

    gb_memory_write(&memory, 0xff26, 0x80);
    gb_memory_write(&memory, 0xff24, 0x77);
    gb_memory_write(&memory, 0xff25, 0xff);
    gb_memory_write(&memory, 0xff16, 0x80);
    gb_memory_write(&memory, 0xff17, 0xf0);
    gb_memory_write(&memory, 0xff18, 0xf8);
    gb_memory_write(&memory, 0xff19, 0x87);
    GB_REQUIRE((gb_memory_read(&memory, 0xff26) & 0x82) == 0x82);
    GB_REQUIRE((gb_memory_read(&memory, 0xff16) & 0xc0) == 0x80);

    int nonzero = 0;
    int positive = 0;
    int negative = 0;
    gb_timer_advance(&memory, 8192);
    int16_t left = 0;
    int16_t right = 0;
    while (gb_apu_pull_sample(&memory, &left, &right))
    {
        if (left != 0 || right != 0)
            nonzero++;
        if (left > 0)
            positive++;
        if (left < 0)
            negative++;
    }
    GB_REQUIRE(nonzero > 8);
    GB_REQUIRE(positive > 0 && negative > 0);

    gb_memory_write(&memory, 0xff25, 0x02);
    gb_timer_advance(&memory, 4096);
    int left_energy = 0;
    int right_energy = 0;
    while (gb_apu_pull_sample(&memory, &left, &right))
    {
        if (left != 0)
            left_energy++;
        if (right != 0)
            right_energy++;
    }
    GB_REQUIRE(left_energy == 0);
    GB_REQUIRE(right_energy > 0);

    gb_memory_write(&memory, 0xff16, 0xbf);
    gb_memory_write(&memory, 0xff19, 0xc7);
    GB_REQUIRE((gb_memory_read(&memory, 0xff26) & 0x02) == 0x02);
    gb_timer_advance(&memory, 8192 * 2);
    GB_REQUIRE((gb_memory_read(&memory, 0xff26) & 0x02) == 0);
    return 0;
}

static int test_wave(void)
{
    uint8_t rom[0x8000];
    GbMemory memory;
    GB_REQUIRE(setup(rom, &memory) == 0);
    gb_memory_write(&memory, 0xff26, 0x80);
    gb_memory_write(&memory, 0xff24, 0x77);
    gb_memory_write(&memory, 0xff25, 0x44);
    gb_memory_write(&memory, 0xff30, 0xf0);
    gb_memory_write(&memory, 0xff31, 0x0f);
    gb_memory_write(&memory, 0xff1a, 0x80);
    gb_memory_write(&memory, 0xff1c, 0x20);
    gb_memory_write(&memory, 0xff1d, 0xf8);
    gb_memory_write(&memory, 0xff1e, 0x87);
    GB_REQUIRE(gb_memory_read(&memory, 0xff30) == 0xf0);
    GB_REQUIRE((gb_memory_read(&memory, 0xff26) & 0x04) != 0);
    gb_timer_advance(&memory, 4096);
    int16_t left = 0;
    int16_t right = 0;
    int hits = 0;
    while (gb_apu_pull_sample(&memory, &left, &right))
    {
        if (left != 0)
            hits++;
    }
    GB_REQUIRE(hits > 0);
    gb_memory_write(&memory, 0xff26, 0x00);
    GB_REQUIRE(gb_memory_read(&memory, 0xff26) == 0x70);
    GB_REQUIRE(gb_memory_read(&memory, 0xff30) == 0xf0);
    return 0;
}

int main(void)
{
    if (test_power_and_square() != 0) return 1;
    if (test_wave() != 0) return 1;
    return 0;
}
