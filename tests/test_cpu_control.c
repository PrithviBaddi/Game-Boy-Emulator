#include "gb_check.h"
#include "gb_test_util.h"

static int expect_relative(GbTestMachine *machine, uint8_t opcode, uint8_t flags,
                           bool taken)
{
    const uint8_t code[] = {opcode, 0x02};
    GB_REQUIRE(gb_test_load(machine, code, sizeof code));
    machine->cpu.f = flags;
    machine->cpu.sp = 0x1234;
    unsigned cycles = 0;
    GB_REQUIRE(gb_cpu_step(&machine->cpu, &machine->memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine->cpu.pc == (taken ? 0x0104 : 0x0102));
    GB_REQUIRE(cycles == (taken ? 12u : 8u));
    GB_REQUIRE(machine->cpu.f == flags);
    GB_REQUIRE(machine->cpu.sp == 0x1234);
    return 0;
}

static int expect_absolute(GbTestMachine *machine, uint8_t opcode, uint8_t flags,
                           bool taken, unsigned taken_cycles, unsigned skipped_cycles)
{
    const uint8_t code[] = {opcode, 0x34, 0x12};
    GB_REQUIRE(gb_test_load(machine, code, sizeof code));
    machine->cpu.f = flags;
    uint16_t sp = machine->cpu.sp;
    unsigned cycles = 0;
    GB_REQUIRE(gb_cpu_step(&machine->cpu, &machine->memory, &cycles) == GB_STEP_OK);
    if (taken)
    {
        GB_REQUIRE(machine->cpu.pc == 0x1234);
        GB_REQUIRE(cycles == taken_cycles);
        if (opcode == 0xc4 || opcode == 0xcc || opcode == 0xd4 || opcode == 0xdc ||
            opcode == 0xcd)
        {
            GB_REQUIRE(machine->cpu.sp == (uint16_t)(sp - 2));
            GB_REQUIRE(gb_memory_read(&machine->memory, machine->cpu.sp) == 0x03);
            GB_REQUIRE(gb_memory_read(&machine->memory, (uint16_t)(machine->cpu.sp + 1)) == 0x01);
        }
        else
        {
            GB_REQUIRE(machine->cpu.sp == sp);
        }
    }
    else
    {
        GB_REQUIRE(machine->cpu.pc == 0x0103);
        GB_REQUIRE(cycles == skipped_cycles);
        GB_REQUIRE(machine->cpu.sp == sp);
    }
    GB_REQUIRE(machine->cpu.f == flags);
    return 0;
}

int main(void)
{
    GbTestMachine machine;
    const struct
    {
        uint8_t opcode;
        uint8_t flags;
        bool taken;
    } relatives[] = {
        {0x20, 0x00, true},  {0x20, 0x80, false}, {0x28, 0x80, true},
        {0x28, 0x00, false}, {0x30, 0x00, true},  {0x30, 0x10, false},
        {0x38, 0x10, true},  {0x38, 0x00, false},
    };
    for (unsigned i = 0; i < sizeof relatives / sizeof relatives[0]; ++i)
        GB_REQUIRE(expect_relative(&machine, relatives[i].opcode, relatives[i].flags,
                                   relatives[i].taken) == 0);

    const uint8_t jp_ops[] = {0xc2, 0xca, 0xd2, 0xda};
    const uint8_t call_ops[] = {0xc4, 0xcc, 0xd4, 0xdc};
    for (unsigned i = 0; i < 4; ++i)
    {
        GB_REQUIRE(expect_absolute(&machine, jp_ops[i], relatives[i * 2].flags, true, 16,
                                   12) == 0);
        GB_REQUIRE(expect_absolute(&machine, jp_ops[i], relatives[i * 2 + 1].flags, false, 16,
                                   12) == 0);
        GB_REQUIRE(expect_absolute(&machine, call_ops[i], relatives[i * 2].flags, true, 24,
                                   12) == 0);
        GB_REQUIRE(expect_absolute(&machine, call_ops[i], relatives[i * 2 + 1].flags, false,
                                   24, 12) == 0);
    }

    const uint8_t jp[] = {0xc3, 0x34, 0x12};
    GB_REQUIRE(gb_test_load(&machine, jp, sizeof jp));
    machine.cpu.f = 0xf0;
    unsigned cycles = 0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.pc == 0x1234 && cycles == 16 && machine.cpu.f == 0xf0);

    const uint8_t jp_hl[] = {0xe9};
    GB_REQUIRE(gb_test_load(&machine, jp_hl, 1));
    machine.cpu.h = 0x02;
    machine.cpu.l = 0x00;
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.pc == 0x0200 && cycles == 4 && machine.cpu.f == 0xf0);
    GB_REQUIRE(gb_test_pair(machine.cpu.h, machine.cpu.l) == 0x0200);

    /* RET cc reads no stack bytes when the condition fails. */
    const uint8_t ret_nz[] = {0xc0};
    GB_REQUIRE(gb_test_load(&machine, ret_nz, 1));
    machine.cpu.f = 0x80;
    machine.cpu.sp = 0xc000;
    gb_memory_write(&machine.memory, 0xc000, 0x99);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.pc == 0x0101 && cycles == 8 && machine.cpu.sp == 0xc000);

    GB_REQUIRE(gb_test_load(&machine, ret_nz, 1));
    machine.cpu.f = 0x00;
    machine.cpu.sp = 0xc000;
    gb_memory_write(&machine.memory, 0xc000, 0x10);
    gb_memory_write(&machine.memory, 0xc001, 0x20);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.pc == 0x2010 && cycles == 20 && machine.cpu.sp == 0xc002);

    /* POP AF drops the low four flag bits. PUSH stores the high byte first. */
    const uint8_t pop_af[] = {0xf1};
    GB_REQUIRE(gb_test_load(&machine, pop_af, 1));
    machine.cpu.sp = 0xc000;
    machine.cpu.f = 0xf0;
    gb_memory_write(&machine.memory, 0xc000, 0x1f);
    gb_memory_write(&machine.memory, 0xc001, 0xab);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0xab && machine.cpu.f == 0x10);
    GB_REQUIRE(machine.cpu.sp == 0xc002 && cycles == 12 && machine.cpu.pc == 0x0101);

    const uint8_t pairs[] = {0xc5, 0xd5, 0xe5, 0xf5};
    for (unsigned i = 0; i < 4; ++i)
    {
        GB_REQUIRE(gb_test_load(&machine, &pairs[i], 1));
        machine.cpu.sp = 0x0001; /* high byte wraps onto ROM and is ignored */
        machine.cpu.b = 0x12;
        machine.cpu.c = 0x34;
        machine.cpu.d = 0x56;
        machine.cpu.e = 0x78;
        machine.cpu.h = 0x9a;
        machine.cpu.l = 0xbc;
        machine.cpu.a = 0xde;
        machine.cpu.f = 0xf1; /* low bit must not be stored */
        GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
        GB_REQUIRE(machine.cpu.sp == 0xffff);
        GB_REQUIRE(gb_memory_read(&machine.memory, 0x0000) == 0); /* the high byte did not stick */
        uint8_t low_expected = i == 0 ? 0x34 : i == 1 ? 0x78 : i == 2 ? 0xbc : 0xf0;
        GB_REQUIRE(gb_memory_read(&machine.memory, 0xffff) == low_expected);
        GB_REQUIRE(cycles == 16);
        GB_REQUIRE(machine.cpu.f == 0xf1);
    }

    const uint8_t push_bc[] = {0xc5};
    GB_REQUIRE(gb_test_load(&machine, push_bc, 1));
    machine.cpu.b = 0x12;
    machine.cpu.c = 0x34;
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.sp == 0xfffc);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xfffc) == 0x34);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xfffd) == 0x12);
    GB_REQUIRE(machine.cpu.f == 0xf0);

    machine.rom[0x100] = 0xc1; /* POP BC */
    machine.cpu.pc = 0x0100;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(gb_test_pair(machine.cpu.b, machine.cpu.c) == 0x1234);
    GB_REQUIRE(machine.cpu.sp == 0xfffe && cycles == 12);

    const uint8_t rst[] = {0xcf};
    GB_REQUIRE(gb_test_load(&machine, rst, 1));
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.pc == 0x0008 && cycles == 16);
    GB_REQUIRE(machine.cpu.sp == 0xfffc);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xfffc) == 0x01);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xfffd) == 0x01);

    for (unsigned vector = 0; vector < 8; ++vector)
    {
        uint8_t opcode = (uint8_t)(0xc7 | (vector << 3));
        GB_REQUIRE(gb_test_load(&machine, &opcode, 1));
        GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
        GB_REQUIRE(machine.cpu.pc == (uint16_t)(vector * 8));
        GB_REQUIRE(cycles == 16);
    }

    const uint8_t reti[] = {0xd9};
    GB_REQUIRE(gb_test_load(&machine, reti, 1));
    machine.cpu.sp = 0xc000;
    machine.cpu.ime = false;
    gb_memory_write(&machine.memory, 0xc000, 0x00);
    gb_memory_write(&machine.memory, 0xc001, 0x02);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.pc == 0x0200 && machine.cpu.ime && cycles == 16);

    /* Count down with DEC B / JR NZ, then return through two nested calls. */
    const uint8_t entry[] = {
        0x06, 0x03,       /* LD B,3 */
        0x05,             /* DEC B */
        0x20, 0xfd,       /* JR NZ,-3 */
        0xcd, 0x50, 0x01, /* CALL 0x0150 */
        0x76};
    const uint8_t outer[] = {0xcd, 0x60, 0x01, 0xc9}; /* CALL 0x0160; RET */
    const uint8_t inner[] = {0x04, 0xc9};             /* INC B; RET */
    GB_REQUIRE(gb_test_load(&machine, entry, sizeof entry));
    GB_REQUIRE(gb_test_place(&machine, 0x0150, outer, sizeof outer));
    GB_REQUIRE(gb_test_place(&machine, 0x0160, inner, sizeof inner));
    GbTestRun run = gb_test_run(&machine, 30);
    GB_REQUIRE(!run.hit_limit && run.last == GB_STEP_HALTED);
    GB_REQUIRE(machine.cpu.b == 0x01);
    GB_REQUIRE(machine.cpu.f == 0x00); /* INC B from 0 clears Z and N */
    GB_REQUIRE(machine.cpu.sp == 0xfffe);
    GB_REQUIRE(machine.cpu.pc == 0x0109);

    const uint8_t spin[] = {0x18, 0xfe};
    GB_REQUIRE(gb_test_load(&machine, spin, sizeof spin));
    run = gb_test_run(&machine, 5);
    GB_REQUIRE(run.hit_limit && run.steps == 5 && run.cycles == 60);

    return 0;
}
