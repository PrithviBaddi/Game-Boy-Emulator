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
    /* JR +2: start at 0x0100, skip the two-byte instruction,
   then move two bytes forward to 0x0104. */
    rom[0x100] = 0x18;
    rom[0x101] = 0x02;
    gb_cpu_init(&cpu);
    cpu.f = 0x10;
    assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    assert(cpu.pc == 0x104);
    assert(cycles == 12);
    assert(cpu.f == 0x10); /* JR does not change flags */

    /* JR -4: start at 0x0104 and land at 0x0102. */
    rom[0x104] = 0x18;
    rom[0x105] = 0xfc; /* 0xfc represents -4 as a signed byte */
    gb_cpu_init(&cpu);
    cpu.pc = 0x104;
    assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    assert(cpu.pc == 0x102);
    assert(cycles == 12);
    /* 0x78 = LD A,B */
    rom[0x100] = 0x78;
    gb_cpu_init(&cpu);
    cpu.b = 0x42;
    cpu.f = 0x10;
    assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    assert(cpu.a == 0x42);
    assert(cpu.pc == 0x101);
    assert(cpu.f == 0x10);
    assert(cycles == 4);

    /* 0x41 = LD B,C */
    rom[0x100] = 0x41;
    gb_cpu_init(&cpu);
    cpu.c = 0x27;
    assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    assert(cpu.b == 0x27);

    /* 0x46 = LD B,[HL]: read memory at address 0xC123 into B. */
    rom[0x100] = 0x46;
    gb_memory_write(&memory, 0xc123, 0x5a);
    gb_cpu_init(&cpu);
    cpu.h = 0xc1;
    cpu.l = 0x23;
    cpu.f = 0x10;
    assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    assert(cpu.b == 0x5a);
    assert(cpu.pc == 0x101);
    assert(cpu.f == 0x10);
    assert(cycles == 8);

    /* 0x70 = LD [HL],B: write B to memory at address 0xC123. */
    rom[0x100] = 0x70;
    gb_cpu_init(&cpu);
    cpu.h = 0xc1;
    cpu.l = 0x23;
    cpu.b = 0x73;
    assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
    assert(gb_memory_read(&memory, 0xc123) == 0x73);
    assert(cycles == 8);

    /* Exercise every LD destination,source opcode except 0x76 (HALT). */
    for (unsigned opcode = 0x40; opcode <= 0x7f; ++opcode)
    {
        if (opcode == 0x76)
            continue;

        rom[0x100] = (uint8_t)opcode;
        gb_cpu_init(&cpu);
        cpu.b = 0x11;
        cpu.c = 0x22;
        cpu.d = 0x33;
        cpu.e = 0x44;
        cpu.h = 0xc1;
        cpu.l = 0x23;
        cpu.a = 0x88;
        cpu.f = 0x10;
        gb_memory_write(&memory, 0xc123, 0x77);

        uint8_t before[] = {
            cpu.b, cpu.c, cpu.d, cpu.e, cpu.h, cpu.l,
            gb_memory_read(&memory, 0xc123), cpu.a};

        unsigned destination = (opcode >> 3) & 7u;
        unsigned source = opcode & 7u;
        assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);

        uint8_t after[] = {
            cpu.b, cpu.c, cpu.d, cpu.e, cpu.h, cpu.l,
            gb_memory_read(&memory, 0xc123), cpu.a};
        assert(after[destination] == before[source]);
        assert(cpu.pc == 0x101);
        assert(cpu.f == 0x10);
        assert(cycles == ((source == 6 || destination == 6) ? 8u : 4u));
    }

    /* ADD A,r for B, C, D, E, H, L, [HL], and A. */
    const uint8_t expected_a[] = {
        0x00, 0xf2, 0x01, 0xf0, 0xb2, 0x14, 0x00, 0xe2};
    const uint8_t expected_f[] = {
        0xb0, 0x00, 0x10, 0x30, 0x10, 0x10, 0xb0, 0x10};

    for (unsigned source = 0; source < 8; ++source)
    {
        rom[0x100] = (uint8_t)(0x80 + source);
        gb_cpu_init(&cpu);
        cpu.a = 0xf1;
        cpu.b = 0x0f;
        cpu.c = 0x01;
        cpu.d = 0x10;
        cpu.e = 0xff;
        cpu.h = 0xc1;
        cpu.l = 0x23;
        cpu.f = 0xf0; /* confirm ADD replaces old flags */
        gb_memory_write(&memory, 0xc123, 0x0f);

        assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);
        assert(cpu.a == expected_a[source]);
        assert(cpu.f == expected_f[source]);
        assert(cpu.pc == 0x101);
        assert(cycles == (source == 6 ? 8u : 4u));
    }

    /* LD r,d8: load 0xA5 into each register or [HL]. */
    for (unsigned destination = 0; destination < 8; ++destination)
    {
        rom[0x100] = (uint8_t)(0x06 | (destination << 3));
        rom[0x101] = 0xa5;

        gb_cpu_init(&cpu);
        cpu.h = 0xc1;
        cpu.l = 0x23;
        cpu.f = 0x10;
        gb_memory_write(&memory, 0xc123, 0);

        assert(gb_cpu_step(&cpu, &memory, &cycles) == GB_STEP_OK);

        uint8_t result[] = {
            cpu.b, cpu.c, cpu.d, cpu.e, cpu.h, cpu.l,
            gb_memory_read(&memory, 0xc123), cpu.a};
        assert(result[destination] == 0xa5);
        assert(cpu.pc == 0x102);
        assert(cpu.f == 0x10);
        assert(cycles == (destination == 6 ? 12u : 8u));
    }

    return 0;
}
