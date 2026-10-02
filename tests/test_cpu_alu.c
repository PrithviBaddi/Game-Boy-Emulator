#include "gb_check.h"
#include "gb_test_util.h"

typedef struct
{
    uint8_t group; /* 0 ADD, 1 ADC, 2 SUB, 3 SBC, 4 AND, 5 XOR, 6 OR, 7 CP */
    uint8_t a;
    uint8_t operand;
    uint8_t f_in;
    uint8_t a_out;
    uint8_t f_out;
} AluCase;

static int run_form(GbTestMachine *machine, const AluCase *alu, unsigned form)
{
    static const uint8_t reg_opcode[] = {0x80, 0x88, 0x90, 0x98, 0xa0, 0xa8, 0xb0, 0xb8};
    static const uint8_t imm_opcode[] = {0xc6, 0xce, 0xd6, 0xde, 0xe6, 0xee, 0xf6, 0xfe};
    uint8_t opcode = form == 1 ? imm_opcode[alu->group] : reg_opcode[alu->group];
    if (form == 2)
        opcode = (uint8_t)(reg_opcode[alu->group] + 6); /* (HL) */
    unsigned length = form == 1 ? 2u : 1u;
    unsigned expect_cycles = form == 0 ? 4u : 8u;

    GB_REQUIRE(gb_test_load(machine, (const uint8_t *)"", 0));
    machine->rom[0x100] = opcode;
    machine->rom[0x101] = alu->operand;
    gb_cpu_init(&machine->cpu);
    machine->cpu.a = alu->a;
    machine->cpu.f = alu->f_in;
    machine->cpu.b = alu->operand;
    machine->cpu.c = 0x55;
    machine->cpu.h = 0xc0;
    machine->cpu.l = 0x10;
    gb_memory_write(&machine->memory, 0xc010, alu->operand);

    unsigned cycles = 0;
    GB_REQUIRE(gb_cpu_step(&machine->cpu, &machine->memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine->cpu.a == alu->a_out);
    GB_REQUIRE(machine->cpu.f == alu->f_out);
    GB_REQUIRE(machine->cpu.pc == (uint16_t)(0x100 + length));
    GB_REQUIRE(cycles == expect_cycles);
    GB_REQUIRE(machine->cpu.c == 0x55);
    if (form != 2)
        GB_REQUIRE(gb_memory_read(&machine->memory, 0xc010) == alu->operand);
    return 0;
}

static int check_inc_dec(GbTestMachine *machine, uint8_t old_value, bool decrement,
                         uint8_t flags_in, uint8_t expect_value, uint8_t expect_flags)
{
    for (unsigned target = 0; target < 8; ++target)
    {
        uint8_t opcode = (uint8_t)((target << 3) | (decrement ? 0x05 : 0x04));
        GB_REQUIRE(gb_test_load(machine, &opcode, 1));
        gb_cpu_init(&machine->cpu);
        machine->cpu.f = flags_in;
        machine->cpu.h = 0xc0;
        machine->cpu.l = 0x20;
        if (target == 6)
            gb_memory_write(&machine->memory, 0xc020, old_value);
        switch (target)
        {
        case 0:
            machine->cpu.b = old_value;
            break;
        case 1:
            machine->cpu.c = old_value;
            break;
        case 2:
            machine->cpu.d = old_value;
            break;
        case 3:
            machine->cpu.e = old_value;
            break;
        case 4:
            machine->cpu.h = old_value;
            machine->cpu.l = 0x20;
            break;
        case 5:
            machine->cpu.l = old_value;
            break;
        case 7:
            machine->cpu.a = old_value;
            break;
        default:
            break;
        }
        unsigned cycles = 0;
        GB_REQUIRE(gb_cpu_step(&machine->cpu, &machine->memory, &cycles) == GB_STEP_OK);
        uint8_t actual = 0;
        switch (target)
        {
        case 0:
            actual = machine->cpu.b;
            break;
        case 1:
            actual = machine->cpu.c;
            break;
        case 2:
            actual = machine->cpu.d;
            break;
        case 3:
            actual = machine->cpu.e;
            break;
        case 4:
            actual = machine->cpu.h;
            break;
        case 5:
            actual = machine->cpu.l;
            break;
        case 6:
            actual = gb_memory_read(&machine->memory, 0xc020);
            GB_REQUIRE(gb_test_pair(machine->cpu.h, machine->cpu.l) == 0xc020);
            break;
        default:
            actual = machine->cpu.a;
            break;
        }
        GB_REQUIRE(actual == expect_value);
        GB_REQUIRE(machine->cpu.f == expect_flags);
        GB_REQUIRE(cycles == (target == 6 ? 12u : 4u));
        GB_REQUIRE(machine->cpu.pc == 0x101);
    }
    return 0;
}

int main(void)
{
    GbTestMachine machine;
    const AluCase cases[] = {
        {0, 0x00, 0x00, 0xf0, 0x00, 0x80},
        {0, 0x0f, 0x01, 0xf0, 0x10, 0x20},
        {0, 0xff, 0x01, 0x00, 0x00, 0xb0},
        {0, 0xf0, 0x10, 0xf0, 0x00, 0x90},
        {0, 0x08, 0x08, 0xf0, 0x10, 0x20},
        {1, 0x00, 0x00, 0x10, 0x01, 0x00},
        {1, 0x0f, 0x00, 0x10, 0x10, 0x20},
        {1, 0xff, 0x00, 0xf0, 0x00, 0xb0},
        {1, 0xff, 0x01, 0x10, 0x01, 0x30},
        {1, 0x0f, 0x01, 0x00, 0x10, 0x20},
        {2, 0x00, 0x00, 0xf0, 0x00, 0xc0},
        {2, 0x10, 0x01, 0xf0, 0x0f, 0x60},
        {2, 0x00, 0x01, 0x10, 0xff, 0x70},
        {2, 0x10, 0x10, 0xf0, 0x00, 0xc0},
        {2, 0x10, 0x00, 0x10, 0x10, 0x40}, /* SUB ignores the old carry */
        {3, 0x00, 0x00, 0x10, 0xff, 0x70},
        {3, 0x10, 0x00, 0x10, 0x0f, 0x60},
        {3, 0x00, 0x01, 0x10, 0xfe, 0x70},
        {3, 0x01, 0x00, 0x10, 0x00, 0xc0},
        {3, 0x0f, 0x0f, 0x10, 0xff, 0x70},
        {3, 0x10, 0x01, 0x00, 0x0f, 0x60},
        {4, 0xff, 0x00, 0xf0, 0x00, 0xa0},
        {4, 0xf0, 0x0f, 0x10, 0x00, 0xa0},
        {4, 0xf0, 0xff, 0xf0, 0xf0, 0x20},
        {5, 0xff, 0xff, 0xf0, 0x00, 0x80},
        {5, 0xf0, 0x0f, 0xf0, 0xff, 0x00},
        {6, 0x00, 0x00, 0xf0, 0x00, 0x80},
        {6, 0xf0, 0x0f, 0xf0, 0xff, 0x00},
        {6, 0x01, 0x02, 0x10, 0x03, 0x00},
        {7, 0x10, 0x10, 0xf0, 0x10, 0xc0},
        {7, 0x10, 0x01, 0xf0, 0x10, 0x60},
        {7, 0x00, 0x01, 0x00, 0x00, 0x70},
        {7, 0xff, 0x0f, 0x10, 0xff, 0x40}, /* equal low nibbles: N only */
    };

    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i)
        for (unsigned form = 0; form < 3; ++form)
            GB_REQUIRE(run_form(&machine, &cases[i], form) == 0);

    GB_REQUIRE(check_inc_dec(&machine, 0x00, false, 0x10, 0x01, 0x10) == 0);
    GB_REQUIRE(check_inc_dec(&machine, 0x0f, false, 0xf0, 0x10, 0x30) == 0);
    GB_REQUIRE(check_inc_dec(&machine, 0xff, false, 0x00, 0x00, 0xa0) == 0);
    GB_REQUIRE(check_inc_dec(&machine, 0x01, true, 0x10, 0x00, 0xd0) == 0);
    GB_REQUIRE(check_inc_dec(&machine, 0x00, true, 0x00, 0xff, 0x60) == 0);
    GB_REQUIRE(check_inc_dec(&machine, 0x10, true, 0xf0, 0x0f, 0x70) == 0);
    GB_REQUIRE(check_inc_dec(&machine, 0x11, true, 0x00, 0x10, 0x40) == 0);

    /* 16-bit INC/DEC wrap and leave every flag bit alone, including Z/N/H/C. */
    const uint8_t inc_dec_pairs[] = {0x03, 0x13, 0x23, 0x33, 0x0b, 0x1b, 0x2b, 0x3b};
    for (unsigned i = 0; i < 8; ++i)
    {
        bool decrement = inc_dec_pairs[i] & 0x08;
        GB_REQUIRE(gb_test_load(&machine, &inc_dec_pairs[i], 1));
        gb_cpu_init(&machine.cpu);
        machine.cpu.b = 0xff;
        machine.cpu.c = 0xff;
        machine.cpu.d = 0x00;
        machine.cpu.e = 0x00;
        machine.cpu.h = 0x10;
        machine.cpu.l = 0x00;
        machine.cpu.sp = 0x0000;
        machine.cpu.f = 0xf0;
        unsigned cycles = 0;
        GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
        GB_REQUIRE(machine.cpu.f == 0xf0);
        GB_REQUIRE(cycles == 8 && machine.cpu.pc == 0x101);
        unsigned pair = inc_dec_pairs[i] >> 4;
        uint16_t actual = pair == 3 ? machine.cpu.sp : pair == 2 ? gb_test_pair(machine.cpu.h, machine.cpu.l)
                                                       : pair == 1 ? gb_test_pair(machine.cpu.d, machine.cpu.e)
                                                                    : gb_test_pair(machine.cpu.b, machine.cpu.c);
        uint16_t started = pair == 0 ? 0xffff : pair == 2 ? 0x1000 : 0;
        GB_REQUIRE(actual == (uint16_t)(started + (decrement ? -1 : 1)));
    }

    /* ADD HL,BC: half-carry out of bit 11, Z preserved, N cleared. */
    const uint8_t add_hl_bc[] = {0x09};
    GB_REQUIRE(gb_test_load(&machine, add_hl_bc, 1));
    machine.cpu.h = 0x0f;
    machine.cpu.l = 0xff;
    machine.cpu.b = 0x00;
    machine.cpu.c = 0x01;
    machine.cpu.f = 0xc0;
    unsigned cycles = 0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(gb_test_pair(machine.cpu.h, machine.cpu.l) == 0x1000);
    GB_REQUIRE(machine.cpu.f == 0xa0 && cycles == 8);

    const uint8_t add_hl_sp[] = {0x39};
    GB_REQUIRE(gb_test_load(&machine, add_hl_sp, 1));
    machine.cpu.h = 0xff;
    machine.cpu.l = 0xff;
    machine.cpu.sp = 0x0001;
    machine.cpu.f = 0x80;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(gb_test_pair(machine.cpu.h, machine.cpu.l) == 0x0000);
    GB_REQUIRE(machine.cpu.f == 0xb0 && machine.cpu.sp == 0x0001);

    const uint8_t add_hl_hl[] = {0x29};
    GB_REQUIRE(gb_test_load(&machine, add_hl_hl, 1));
    machine.cpu.h = 0xf0;
    machine.cpu.l = 0x00;
    machine.cpu.f = 0x00;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(gb_test_pair(machine.cpu.h, machine.cpu.l) == 0xe000);
    GB_REQUIRE(machine.cpu.f == 0x10); /* carry, no half-carry from bit 11 */

    const uint8_t add_sp[] = {0xe8, 0x01};
    GB_REQUIRE(gb_test_load(&machine, add_sp, sizeof add_sp));
    machine.cpu.sp = 0x000f;
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.sp == 0x0010 && machine.cpu.f == 0x20 && cycles == 16);

    machine.rom[0x101] = 0xff; /* -1 from 0: the low byte does not carry */
    gb_cpu_init(&machine.cpu);
    machine.cpu.sp = 0x0000;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.sp == 0xffff && machine.cpu.f == 0x00);

    gb_cpu_init(&machine.cpu);
    machine.cpu.sp = 0x0001;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.sp == 0x0000 && machine.cpu.f == 0x30);

    machine.rom[0x101] = 0x01;
    gb_cpu_init(&machine.cpu);
    machine.cpu.sp = 0x00ff;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.sp == 0x0100 && machine.cpu.f == 0x30);

    machine.rom[0x101] = 0x00;
    gb_cpu_init(&machine.cpu);
    machine.cpu.sp = 0x0000;
    machine.cpu.f = 0x80;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.sp == 0 && machine.cpu.f == 0x00);

    const uint8_t ld_hl_sp[] = {0xf8, 0x10};
    GB_REQUIRE(gb_test_load(&machine, ld_hl_sp, sizeof ld_hl_sp));
    machine.cpu.sp = 0xfff0;
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(gb_test_pair(machine.cpu.h, machine.cpu.l) == 0x0000);
    GB_REQUIRE(machine.cpu.sp == 0xfff0);
    GB_REQUIRE(machine.cpu.f == 0x10 && cycles == 12 && machine.cpu.pc == 0x102);

    machine.rom[0x101] = 0xf0; /* -16 */
    gb_cpu_init(&machine.cpu);
    machine.cpu.sp = 0x0000;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(gb_test_pair(machine.cpu.h, machine.cpu.l) == 0xfff0);
    GB_REQUIRE(machine.cpu.sp == 0x0000 && machine.cpu.f == 0x00);

    /* 9 + 1 = 0x0A, then DAA corrects the BCD result to 0x10. */
    const uint8_t add_daa[] = {0x3e, 0x09, 0xc6, 0x01, 0x27, 0x76};
    GB_REQUIRE(gb_test_load(&machine, add_daa, sizeof add_daa));
    GbTestRun run = gb_test_run(&machine, 6);
    GB_REQUIRE(run.steps == 4 && run.last == GB_STEP_HALTED && !run.hit_limit);
    GB_REQUIRE(machine.cpu.a == 0x10 && machine.cpu.f == 0x00);
    GB_REQUIRE(machine.cpu.pc == 0x106);

    machine.rom[0x100] = 0x27;
    gb_cpu_init(&machine.cpu);
    machine.cpu.a = 0x9a;
    machine.cpu.f = 0x00;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x00 && machine.cpu.f == 0x90);

    gb_cpu_init(&machine.cpu);
    machine.cpu.a = 0x00;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x00 && machine.cpu.f == 0x80);

    gb_cpu_init(&machine.cpu);
    machine.cpu.a = 0x0f;
    machine.cpu.f = 0x60; /* N and H set, as after a low-nibble subtract */
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x09 && machine.cpu.f == 0x40);

    gb_cpu_init(&machine.cpu);
    machine.cpu.a = 0x00;
    machine.cpu.f = 0x70; /* N, H, and C */
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x9a && machine.cpu.f == 0x50);

    machine.rom[0x100] = 0x2f; /* CPL */
    gb_cpu_init(&machine.cpu);
    machine.cpu.a = 0x00;
    machine.cpu.f = 0x90;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0xff && machine.cpu.f == 0xf0 && cycles == 4);

    machine.rom[0x100] = 0x37; /* SCF */
    gb_cpu_init(&machine.cpu);
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.f == 0x90);

    machine.rom[0x100] = 0x3f; /* CCF */
    gb_cpu_init(&machine.cpu);
    machine.cpu.f = 0x90;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.f == 0x80);
    gb_cpu_init(&machine.cpu);
    machine.cpu.f = 0x80;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.f == 0x90);

    /* RLCA/RRCA/RLA/RRA clear Z even when A becomes 0. */
    machine.rom[0x100] = 0x07;
    gb_cpu_init(&machine.cpu);
    machine.cpu.a = 0x80;
    machine.cpu.f = 0xf0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x01 && machine.cpu.f == 0x10);

    gb_cpu_init(&machine.cpu);
    machine.cpu.a = 0x00;
    machine.cpu.f = 0x80;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x00 && machine.cpu.f == 0x00);

    machine.rom[0x100] = 0x0f;
    gb_cpu_init(&machine.cpu);
    machine.cpu.a = 0x01;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x80 && machine.cpu.f == 0x10);

    machine.rom[0x100] = 0x17; /* RLA through carry */
    gb_cpu_init(&machine.cpu);
    machine.cpu.a = 0x80;
    machine.cpu.f = 0x00;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x00 && machine.cpu.f == 0x10);

    gb_cpu_init(&machine.cpu);
    machine.cpu.a = 0x00;
    machine.cpu.f = 0x10;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x01 && machine.cpu.f == 0x00);

    machine.rom[0x100] = 0x1f; /* RRA */
    gb_cpu_init(&machine.cpu);
    machine.cpu.a = 0x01;
    machine.cpu.f = 0x80;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x00 && machine.cpu.f == 0x10);

    gb_cpu_init(&machine.cpu);
    machine.cpu.a = 0x01;
    machine.cpu.f = 0x10;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x80 && machine.cpu.f == 0x10);

    return 0;
}
