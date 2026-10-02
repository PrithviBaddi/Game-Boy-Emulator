#include "gb_cpu.h"

#include <string.h>

enum
{
    FLAG_Z = 0x80,
    FLAG_N = 0x40,
    FLAG_H = 0x20,
    FLAG_C = 0x10
};

void gb_cpu_init(GbCpu *cpu)
{
    memset(cpu, 0, sizeof *cpu);
    cpu->pc = 0x0100; /* standard cartridge entry address */
    cpu->sp = 0xfffe;
}

GbStepResult gb_cpu_step(GbCpu *cpu, GbMemory *memory, unsigned *cycles)
{
    if (!cpu || !memory || !cycles)
        return GB_STEP_UNSUPPORTED;
    *cycles = 0;
    if (cpu->halted)
        return GB_STEP_HALTED;
    uint8_t opcode = gb_memory_read(memory, cpu->pc);
    switch (opcode)
    {
    case 0x00: /* NOP: do nothing */
        cpu->pc += 1;
        *cycles = 4;
        return GB_STEP_OK;
    case 0x3e: /* LD A,d8: load the following byte into register A */
        cpu->a = gb_memory_read(memory, (uint16_t)(cpu->pc + 1));
        cpu->pc += 2;
        *cycles = 8;
        return GB_STEP_OK;
    case 0x06: /* LD B,d8 */
        cpu->b = gb_memory_read(memory, (uint16_t)(cpu->pc + 1));
        cpu->pc += 2;
        *cycles = 8;
        return GB_STEP_OK;
    case 0x0e: /* LD C,d8: put the next byte into register C */
        cpu->c = gb_memory_read(memory, (uint16_t)(cpu->pc + 1));
        cpu->pc += 2;
        *cycles = 8;
        return GB_STEP_OK;
    case 0x80:
    { /* ADD A,B: update result and Z/N/H/C flags */
        unsigned result = (unsigned)cpu->a + cpu->b;
        uint8_t flags = 0;
        if ((uint8_t)result == 0)
            flags |= FLAG_Z;
        if (((cpu->a & 0x0f) + (cpu->b & 0x0f)) > 0x0f)
            flags |= FLAG_H;
        if (result > 0xff)
            flags |= FLAG_C;
        cpu->a = (uint8_t)result;
        cpu->f = flags; /* addition clears N */
        cpu->pc += 1;
        *cycles = 4;
        return GB_STEP_OK;
    }
    case 0xea:
    { /* LD (a16),A: write A to the next 16-bit address */
        uint8_t low = gb_memory_read(memory, (uint16_t)(cpu->pc + 1));
        uint8_t high = gb_memory_read(memory, (uint16_t)(cpu->pc + 2));
        uint16_t address = (uint16_t)(low | ((uint16_t)high << 8));
        gb_memory_write(memory, address, cpu->a);
        cpu->pc += 3;
        *cycles = 16;
        return GB_STEP_OK;
    }
    case 0x3c:
    { /* INC A */
        uint8_t old_a = cpu->a;
        cpu->a = (uint8_t)(old_a + 1);
        cpu->f &= FLAG_C;

        if (cpu->a == 0)
            cpu->f |= FLAG_Z;
        if ((old_a & 0x0f) == 0x0f)
            cpu->f |= FLAG_H;

        cpu->pc += 1;
        *cycles = 4;
        return GB_STEP_OK;
    }
    case 0x76: /* HALT: wait until future interrupt handling wakes CPU */
        cpu->pc += 1;
        cpu->halted = true;
        *cycles = 4;
        return GB_STEP_HALTED;
    default:
        return GB_STEP_UNSUPPORTED;
    }
}
