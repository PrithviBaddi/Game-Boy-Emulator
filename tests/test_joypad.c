#include "gb_check.h"
#include "gb_cpu.h"
#include "gb_memory.h"
static int fresh(GbMemory *memory, uint8_t *rom)
{
    GB_REQUIRE(gb_memory_init(memory, rom, 0x8000));
    return 0;
}

static int test_matrix(void)
{
    uint8_t rom[0x8000] = {0};
    GbMemory memory;
    GB_REQUIRE(fresh(&memory, rom) == 0);
    gb_memory_write(&memory, 0xff00, 0x30);
    GB_REQUIRE(gb_memory_read(&memory, 0xff00) == 0xff);

    gb_memory_write(&memory, 0xff00, 0x20);
    GB_REQUIRE(gb_joypad_set_pressed(&memory, GB_BTN_RIGHT));
    GB_REQUIRE(gb_memory_read(&memory, 0xff00) == 0xee);
    GB_REQUIRE(gb_joypad_set_pressed(&memory, GB_BTN_RIGHT | GB_BTN_LEFT));
    GB_REQUIRE((gb_memory_read(&memory, 0xff00) & 0x0f) == 0x0c);
    GB_REQUIRE(gb_joypad_set_pressed(&memory, GB_BTN_RIGHT | GB_BTN_LEFT) == false);

    gb_memory_write(&memory, 0xff00, 0x10);
    GB_REQUIRE(gb_joypad_set_pressed(&memory, GB_BTN_A | GB_BTN_START));
    GB_REQUIRE((gb_memory_read(&memory, 0xff00) & 0x0f) == 0x06);

    gb_memory_write(&memory, 0xff00, 0x00);
    GB_REQUIRE(gb_joypad_set_pressed(&memory, GB_BTN_A | GB_BTN_RIGHT));
    GB_REQUIRE((gb_memory_read(&memory, 0xff00) & 0x0f) == 0x0e);
    gb_memory_write(&memory, 0xff00, 0x3f);
    GB_REQUIRE((gb_memory_read(&memory, 0xff00) & 0x30) == 0x30);
    return 0;
}

static int test_interrupt_and_stop(void)
{
    uint8_t rom[0x8000] = {0};
    GbMemory memory;
    GbCpu cpu;
    GB_REQUIRE(fresh(&memory, rom) == 0);
    gb_memory_write(&memory, 0xff00, 0x20);
    gb_memory_write(&memory, 0xff0f, 0);
    GB_REQUIRE(gb_joypad_set_pressed(&memory, GB_BTN_DOWN));
    GB_REQUIRE((gb_memory_read(&memory, 0xff0f) & 0x10) == 0x10);
    gb_memory_write(&memory, 0xff0f, 0);
    GB_REQUIRE(gb_joypad_set_pressed(&memory, 0) == false);
    GB_REQUIRE((gb_memory_read(&memory, 0xff0f) & 0x10) == 0);

    gb_memory_write(&memory, 0xff00, 0x30);
    gb_joypad_set_pressed(&memory, GB_BTN_A);
    gb_memory_write(&memory, 0xff0f, 0);
    gb_memory_write(&memory, 0xff00, 0x10);
    GB_REQUIRE((gb_memory_read(&memory, 0xff0f) & 0x10) == 0x10);

    rom[0x100] = 0x10;
    rom[0x101] = 0x00;
    rom[0x102] = 0x3c;
    gb_cpu_init(&cpu);
    cpu.a = 0x01;
    unsigned cycles = 0;
    GB_REQUIRE(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_STOPPED);
    GB_REQUIRE(gb_memory_read(&memory, 0xff04) == 0);
    GB_REQUIRE(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_STOPPED);
    GB_REQUIRE(gb_memory_read(&memory, 0xff04) == 0);
    GB_REQUIRE(gb_joypad_set_pressed(&memory, GB_BTN_START));
    gb_cpu_leave_stop(&cpu);
    GB_REQUIRE(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(cpu.a == 0x02 && cpu.pc == 0x103);
    return 0;
}

int main(void)
{
    if (test_matrix() != 0) return 1;
    if (test_interrupt_and_stop() != 0) return 1;
    return 0;
}
