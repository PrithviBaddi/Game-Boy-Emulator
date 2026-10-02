#include "gb_check.h"
#include "gb_test_util.h"

static int expect_step(GbTestMachine *machine, uint8_t opcode, unsigned length,
                       unsigned cycles)
{
    machine->rom[0x100] = opcode;
    machine->rom[0x101] = 0x34;
    machine->rom[0x102] = 0x12;
    gb_cpu_init(&machine->cpu);
    machine->cpu.f = 0xf0;
    machine->cpu.h = 0xc0;
    machine->cpu.l = 0x00;
    unsigned spent = 0;
    GB_REQUIRE(gb_cpu_step(&machine->cpu, &machine->memory, &spent) == GB_STEP_OK);
    GB_REQUIRE(machine->cpu.pc == (uint16_t)(0x100 + length));
    GB_REQUIRE(spent == cycles);
    GB_REQUIRE(machine->cpu.f == 0xf0);
    return 0;
}

int main(void)
{
    GbTestMachine machine;
    GB_REQUIRE(gb_test_load(&machine, (const uint8_t *)"", 0));

    /* 16-bit immediates are little-endian: low byte first. */
    const uint8_t ld_sp[] = {0x31, 0xff, 0x00};
    GB_REQUIRE(gb_test_load(&machine, ld_sp, sizeof ld_sp));
    machine.cpu.f = 0xf0;
    unsigned cycles = 0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.sp == 0x00ff);
    GB_REQUIRE(machine.cpu.pc == 0x103 && cycles == 12 && machine.cpu.f == 0xf0);

    const uint8_t ld_bc[] = {0x01, 0x00, 0xff};
    GB_REQUIRE(gb_test_load(&machine, ld_bc, sizeof ld_bc));
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(gb_test_pair(machine.cpu.b, machine.cpu.c) == 0xff00);

    /* LD (a16),SP writes the low byte first and wraps the address at 0xFFFF. */
    const uint8_t ld_sp_mem[] = {0x08, 0xff, 0xff};
    GB_REQUIRE(gb_test_load(&machine, ld_sp_mem, sizeof ld_sp_mem));
    machine.cpu.sp = 0xbeef;
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xffff) == 0xef);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0x0000) == 0x00); /* ROM ignores the wrapped high byte */
    GB_REQUIRE(cycles == 20 && machine.cpu.f == 0xf0 && machine.cpu.sp == 0xbeef);

    /* HL+ from 0xFFFF wraps to 0. The byte lands in the interrupt-enable register. */
    machine.rom[0x100] = 0x22; /* LD (HL+),A */
    gb_cpu_init(&machine.cpu);
    machine.cpu.h = 0xff;
    machine.cpu.l = 0xff;
    machine.cpu.a = 0x5a;
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xffff) == 0x5a);
    GB_REQUIRE(gb_test_pair(machine.cpu.h, machine.cpu.l) == 0x0000);
    GB_REQUIRE(machine.cpu.f == 0xf0 && cycles == 8);

    /* HL- from 0 wraps to 0xFFFF. Address 0 is ROM, so A is not stored. */
    machine.rom[0x100] = 0x32; /* LD (HL-),A */
    gb_cpu_init(&machine.cpu);
    machine.cpu.a = 0x5a;
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0x0000) == 0x00);
    GB_REQUIRE(gb_test_pair(machine.cpu.h, machine.cpu.l) == 0xffff);
    GB_REQUIRE(machine.cpu.a == 0x5a && machine.cpu.f == 0xf0);

    /* LD A,(HL-) reads the echo of work RAM and then steps back. */
    machine.rom[0x100] = 0x3a;
    gb_memory_write(&machine.memory, 0xc000, 0x77);
    gb_cpu_init(&machine.cpu);
    machine.cpu.h = 0xe0;
    machine.cpu.l = 0x00;
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x77);
    GB_REQUIRE(gb_test_pair(machine.cpu.h, machine.cpu.l) == 0xdfff);
    GB_REQUIRE(machine.cpu.f == 0xf0 && cycles == 8);

    /* High-memory edges: offset 0 is 0xFF00, offset 0xFF is IE. */
    const uint8_t ldh_store[] = {0xe0, 0x00};
    GB_REQUIRE(gb_test_load(&machine, ldh_store, sizeof ldh_store));
    machine.cpu.a = 0x42;
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xff00) == 0x42);
    GB_REQUIRE(cycles == 12 && machine.cpu.f == 0xf0);

    machine.rom[0x100] = 0xe0;
    machine.rom[0x101] = 0xff;
    gb_cpu_init(&machine.cpu);
    machine.cpu.a = 0x11;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xffff) == 0x11);

    machine.rom[0x100] = 0xf0;
    machine.rom[0x101] = 0xff;
    gb_cpu_init(&machine.cpu);
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x11 && machine.cpu.f == 0xf0 && cycles == 12);

    machine.rom[0x100] = 0xe2; /* LD (C),A with C = 0xFF */
    gb_cpu_init(&machine.cpu);
    machine.cpu.c = 0xff;
    machine.cpu.a = 0x90;
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xffff) == 0x90);
    GB_REQUIRE(machine.cpu.pc == 0x101 && cycles == 8 && machine.cpu.f == 0xf0);

    machine.rom[0x100] = 0xf2;
    gb_cpu_init(&machine.cpu);
    machine.cpu.c = 0xff;
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x90 && machine.cpu.f == 0xf0);

    /* LD SP,HL copies the pair and leaves both flags and HL alone. */
    machine.rom[0x100] = 0xf9;
    gb_cpu_init(&machine.cpu);
    machine.cpu.h = 0x00;
    machine.cpu.l = 0x00;
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.sp == 0 && machine.cpu.h == 0 && machine.cpu.l == 0);
    GB_REQUIRE(cycles == 8 && machine.cpu.f == 0xf0);

    /* Absolute read of the last work-RAM byte. */
    const uint8_t ld_abs[] = {0xfa, 0xff, 0xdf};
    GB_REQUIRE(gb_test_load(&machine, ld_abs, sizeof ld_abs));
    gb_memory_write(&machine.memory, 0xdfff, 0x6e);
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x6e && machine.cpu.pc == 0x103 && cycles == 16);

    /* Sweep every ordinary load opcode that does not depend on later phases. */
    GB_REQUIRE(gb_test_load(&machine, (const uint8_t *)"", 0));
    const uint8_t fixed[][3] = {
        {0x01, 3, 12}, {0x11, 3, 12}, {0x21, 3, 12}, {0x31, 3, 12},
        {0x02, 1, 8},  {0x12, 1, 8},  {0x22, 1, 8},  {0x32, 1, 8},
        {0x0a, 1, 8},  {0x1a, 1, 8},  {0x2a, 1, 8},  {0x3a, 1, 8},
        {0x08, 3, 20}, {0xe0, 2, 12}, {0xf0, 2, 12}, {0xe2, 1, 8},
        {0xf2, 1, 8},  {0xea, 3, 16}, {0xfa, 3, 16}, {0xf9, 1, 8},
    };
    for (unsigned i = 0; i < sizeof fixed / sizeof fixed[0]; ++i)
        GB_REQUIRE(expect_step(&machine, fixed[i][0], fixed[i][1], fixed[i][2]) == 0);

    for (unsigned destination = 0; destination < 8; ++destination)
    {
        unsigned length_cycles = destination == 6 ? 12 : 8;
        GB_REQUIRE(expect_step(&machine, (uint8_t)(0x06 | (destination << 3)), 2,
                               length_cycles) == 0);
    }
    for (unsigned opcode = 0x40; opcode <= 0x7f; ++opcode)
    {
        if (opcode == 0x76)
            continue;
        unsigned source = opcode & 7u;
        unsigned destination = (opcode >> 3) & 7u;
        unsigned spent_expected = (source == 6 || destination == 6) ? 8u : 4u;
        GB_REQUIRE(expect_step(&machine, (uint8_t)opcode, 1, spent_expected) == 0);
    }

    return 0;
}
