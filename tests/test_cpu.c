#include <assert.h>
#include <stdint.h>
#include "gb_cpu.h"

int main(void)
{
    uint8_t rom[0x8000] = {0};
    /* LD A,0x40; LD B,0x05; ADD A,B; LD (0xC000),A; HALT */
    const uint8_t program[] = {0x3e, 0x40, 0x06, 0x05, 0x80,
                               0xea, 0x00, 0xc0, 0x76};
    for (unsigned i = 0; i < sizeof program; ++i)
        rom[0x100 + i] = program[i];
    GbMemory memory;
    GbCpu cpu;
    unsigned cycles;
    assert(gb_memory_init(&memory, rom, sizeof rom));
    gb_cpu_init(&cpu);
    assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    assert(cpu.a == 0x40 && cpu.pc == 0x102 && cycles == 8);
    assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    assert(cpu.b == 5 && cpu.pc == 0x104);
    assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    assert(cpu.a == 0x45 && cpu.f == 0 && cycles == 4);
    assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    assert(gb_memory_read(&memory, 0xc000) == 0x45 && cycles == 16);
    assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_HALTED);
    assert(cpu.halted && cpu.pc == 0x109);

    /* Addition wraps to zero and sets zero, half-carry, and carry. */
    uint8_t sum[] = {0x3e, 0xff, 0x06, 0x01, 0x80};
    for (unsigned i = 0; i < sizeof sum; ++i)
        rom[0x100 + i] = sum[i];
    gb_cpu_init(&cpu);
    for (unsigned i = 0; i < 3; ++i)
        assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    assert(cpu.a == 0 && cpu.f == 0xb0);

    rom[0x100] = 0xd3; /* unsupported opcode */
    gb_cpu_init(&cpu);
    assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_UNSUPPORTED);
    assert(cpu.pc == 0x100 && cycles == 0);
    /* LD C,d8 loads C without changing the flags. */
    rom[0x100] = 0x0e;
    rom[0x101] = 0x07;
    gb_cpu_init(&cpu);
    cpu.f = 0x10;

    assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    assert(cpu.c == 7);
    assert(cpu.pc == 0x102);
    assert(cycles == 8);
    assert(cpu.f == 0x10);
    /* INC A: crossing 0x0f sets H and preserves C. */
    rom[0x100] = 0x3c;
    gb_cpu_init(&cpu);
    cpu.a = 0x0f;
    cpu.f = 0x10;
    assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    assert(cpu.a == 0x10);
    assert(cpu.f == 0x30);
    assert(cpu.pc == 0x101);
    assert(cycles == 4);

    /* INC A: 0xff wraps to zero, setting Z and H. */
    gb_cpu_init(&cpu);
    cpu.a = 0xff;
    assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    assert(cpu.a == 0);
    assert(cpu.f == 0xa0);
    return 0;
}
