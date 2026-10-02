#include "gb_check.h"
#include "gb_test_util.h"

static int expect_cb(GbTestMachine *machine, uint8_t cb, uint8_t before, uint8_t flags_in,
                     uint8_t after, uint8_t flags_out, bool memory_operand)
{
    uint8_t opcode = memory_operand ? (uint8_t)((cb & 0xf8) | 6) : cb;
    const uint8_t code[] = {0xcb, opcode};
    GB_REQUIRE(gb_test_load(machine, code, sizeof code));
    machine->cpu.b = before;
    machine->cpu.f = flags_in;
    machine->cpu.h = 0xc0;
    machine->cpu.l = 0x10;
    gb_memory_write(&machine->memory, 0xc010, before);
    unsigned cycles = 0;
    GB_REQUIRE(gb_cpu_step(&machine->cpu, &machine->memory, &cycles) == GB_STEP_OK);
    if (memory_operand)
    {
        if ((opcode & 0xc0) != 0x40)
            GB_REQUIRE(gb_memory_read(&machine->memory, 0xc010) == after);
        else
            GB_REQUIRE(gb_memory_read(&machine->memory, 0xc010) == before);
        GB_REQUIRE(gb_test_pair(machine->cpu.h, machine->cpu.l) == 0xc010);
        GB_REQUIRE(cycles == ((opcode & 0xc0) == 0x40 ? 12u : 16u));
    }
    else
    {
        GB_REQUIRE(machine->cpu.b == ((opcode & 0xc0) == 0x40 ? before : after));
        GB_REQUIRE(cycles == 8);
    }
    GB_REQUIRE(machine->cpu.f == flags_out);
    GB_REQUIRE(machine->cpu.pc == 0x0102);
    return 0;
}

int main(void)
{
    GbTestMachine machine;
    /* RLC B: the CB form sets Z, unlike RLCA. */
    GB_REQUIRE(expect_cb(&machine, 0x00, 0x80, 0xf0, 0x01, 0x10, false) == 0);
    GB_REQUIRE(expect_cb(&machine, 0x00, 0x00, 0xf0, 0x00, 0x80, false) == 0);
    GB_REQUIRE(expect_cb(&machine, 0x00, 0x80, 0x00, 0x01, 0x10, true) == 0);

    /* RRC, RL through carry, RR through carry. */
    GB_REQUIRE(expect_cb(&machine, 0x08, 0x01, 0xf0, 0x80, 0x10, false) == 0);
    GB_REQUIRE(expect_cb(&machine, 0x10, 0x80, 0x10, 0x01, 0x10, false) == 0);
    GB_REQUIRE(expect_cb(&machine, 0x10, 0x80, 0x00, 0x00, 0x90, false) == 0);
    GB_REQUIRE(expect_cb(&machine, 0x18, 0x01, 0x10, 0x80, 0x10, false) == 0);
    GB_REQUIRE(expect_cb(&machine, 0x18, 0x01, 0x00, 0x00, 0x90, false) == 0);

    /* SLA, SRA, SWAP, SRL. */
    GB_REQUIRE(expect_cb(&machine, 0x20, 0x80, 0xf0, 0x00, 0x90, false) == 0);
    GB_REQUIRE(expect_cb(&machine, 0x28, 0x80, 0xf0, 0xc0, 0x00, false) == 0);
    GB_REQUIRE(expect_cb(&machine, 0x28, 0x01, 0x00, 0x00, 0x90, false) == 0);
    GB_REQUIRE(expect_cb(&machine, 0x30, 0xab, 0xf0, 0xba, 0x00, false) == 0);
    GB_REQUIRE(expect_cb(&machine, 0x30, 0x00, 0xf0, 0x00, 0x80, false) == 0);
    GB_REQUIRE(expect_cb(&machine, 0x38, 0x01, 0xf0, 0x00, 0x90, false) == 0);

    /* BIT preserves C, sets H, and does not change the operand. */
    GB_REQUIRE(expect_cb(&machine, 0x40, 0x00, 0x10, 0x00, 0xb0, false) == 0);
    GB_REQUIRE(expect_cb(&machine, 0x78, 0x80, 0x00, 0x80, 0x20, false) == 0);
    GB_REQUIRE(expect_cb(&machine, 0x40, 0xfe, 0x90, 0xfe, 0xb0, true) == 0);

    /* RES and SET leave flags alone. */
    GB_REQUIRE(expect_cb(&machine, 0x80, 0x01, 0xf0, 0x00, 0xf0, false) == 0);
    GB_REQUIRE(expect_cb(&machine, 0xc0, 0x00, 0x10, 0x01, 0x10, false) == 0);
    GB_REQUIRE(expect_cb(&machine, 0xfe, 0x00, 0xf0, 0x80, 0xf0, true) == 0);

    /* Every CB operation on B, and the (HL) encoding, must be defined. */
    for (unsigned cb = 0; cb < 256; ++cb)
    {
        uint8_t ops[] = {(uint8_t)(cb & 0xf8), (uint8_t)((cb & 0xf8) | 6)};
        for (unsigned form = 0; form < 2; ++form)
        {
            const uint8_t code[] = {0xcb, ops[form]};
            GB_REQUIRE(gb_test_load(&machine, code, sizeof code));
            machine.cpu.h = 0xc0;
            machine.cpu.l = 0x00;
            machine.cpu.f = 0x10;
            unsigned cycles = 0;
            GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
            GB_REQUIRE(machine.cpu.pc == 0x0102);
            bool bit = (ops[form] & 0xc0) == 0x40;
            bool hl = (ops[form] & 7) == 6;
            unsigned expect = !hl ? 8u : bit ? 12u : 16u;
            GB_REQUIRE(cycles == expect);
            GB_REQUIRE((machine.cpu.f & 0x0f) == 0);
        }
    }
    return 0;
}
