#include <stdio.h>
#include "gb_check.h"
#include "gb_test_util.h"

/* Pan Docs lists these base opcodes as hard-locking the CPU. Every other base
 * opcode, and every CB opcode, is a defined SM83 instruction. */
static bool illegal_opcode(uint8_t opcode)
{
    switch (opcode)
    {
    case 0xd3:
    case 0xdb:
    case 0xdd:
    case 0xe3:
    case 0xe4:
    case 0xeb:
    case 0xec:
    case 0xed:
    case 0xf4:
    case 0xfc:
    case 0xfd:
        return true;
    default:
        return false;
    }
}

int main(void)
{
    GbTestMachine machine;
    unsigned implemented = 0;
    unsigned illegal = 0;
    for (unsigned opcode = 0; opcode < 256; ++opcode)
    {
        uint8_t program[] = {(uint8_t)opcode, 0x00, 0x00};
        GB_REQUIRE(gb_test_load(&machine, program, sizeof program));
        machine.cpu.h = 0xc0;
        machine.cpu.l = 0x00;
        machine.cpu.sp = 0xfffe;
        unsigned cycles = 0;
        GbStepResult result = gb_cpu_step(&machine.cpu, &machine.memory, &cycles);
        if (illegal_opcode((uint8_t)opcode))
        {
            GB_REQUIRE(result == GB_STEP_ILLEGAL);
            GB_REQUIRE(machine.cpu.pc == 0x0100 && machine.cpu.locked && cycles == 0);
            illegal += 1;
            continue;
        }
        GB_REQUIRE(result == GB_STEP_OK || result == GB_STEP_HALTED ||
                   result == GB_STEP_STOPPED);
        GB_REQUIRE(result != GB_STEP_UNSUPPORTED);
        implemented += 1;
    }

    unsigned cb_implemented = 0;
    for (unsigned cb = 0; cb < 256; ++cb)
    {
        uint8_t program[] = {0xcb, (uint8_t)cb};
        GB_REQUIRE(gb_test_load(&machine, program, sizeof program));
        machine.cpu.h = 0xc0;
        machine.cpu.l = 0x00;
        unsigned cycles = 0;
        GbStepResult result = gb_cpu_step(&machine.cpu, &machine.memory, &cycles);
        GB_REQUIRE(result == GB_STEP_OK);
        cb_implemented += 1;
    }

    GB_REQUIRE(illegal == 11);
    GB_REQUIRE(implemented == 245);
    GB_REQUIRE(cb_implemented == 256);
    printf("base implemented %u, base illegal %u, CB implemented %u\n",
           implemented, illegal, cb_implemented);
    return 0;
}
