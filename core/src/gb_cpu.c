#include "gb_cpu.h"

#include <string.h>

enum
{
    FLAG_Z = 0x80,
    FLAG_N = 0x40,
    FLAG_H = 0x20,
    FLAG_C = 0x10
};
static uint8_t *register_by_index(GbCpu *cpu, unsigned index)
{
    switch (index)
    {
    case 0:
        return &cpu->b;
    case 1:
        return &cpu->c;
    case 2:
        return &cpu->d;
    case 3:
        return &cpu->e;
    case 4:
        return &cpu->h;
    case 5:
        return &cpu->l;
    case 7:
        return &cpu->a;
    default:
        return NULL; /* index 6 means memory at [HL] */
    }
}
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
    if ((opcode & 0xc7) == 0x06)
    {
        unsigned destination = (opcode >> 3) & 7u;
        uint8_t value = gb_memory_read(memory, (uint16_t)(cpu->pc + 1));

        if (destination == 6)
        {
            uint16_t hl = (uint16_t)(((uint16_t)cpu->h << 8) | cpu->l);
            gb_memory_write(memory, hl, value);
            *cycles = 12;
        }
        else
        {
            *register_by_index(cpu, destination) = value;
            *cycles = 8;
        }

        cpu->pc += 2;
        return GB_STEP_OK;
    }
    if (opcode >= 0x40 && opcode <= 0x7f && opcode != 0x76)
    {
        unsigned destination = (opcode >> 3) & 7u;
        unsigned source = opcode & 7u;
        uint16_t hl = (uint16_t)(((uint16_t)cpu->h << 8) | cpu->l);

        uint8_t value;
        if (source == 6)
        {
            value = gb_memory_read(memory, hl);
        }
        else
        {
            value = *register_by_index(cpu, source);
        }

        if (destination == 6)
        {
            gb_memory_write(memory, hl, value);
        }
        else
        {
            *register_by_index(cpu, destination) = value;
        }

        cpu->pc += 1;
        *cycles = (source == 6 || destination == 6) ? 8 : 4;
        return GB_STEP_OK;
    }
    if (opcode >= 0x80 && opcode <= 0x87)
    {
        unsigned source = opcode & 7u;
        uint16_t hl = (uint16_t)(((uint16_t)cpu->h << 8) | cpu->l);
        uint8_t value = source == 6
                            ? gb_memory_read(memory, hl)
                            : *register_by_index(cpu, source);

        uint8_t old_a = cpu->a;
        unsigned result = (unsigned)old_a + value;

        cpu->a = (uint8_t)result;
        cpu->f = 0; /* ADD clears the subtraction flag */
        if (cpu->a == 0)
            cpu->f |= FLAG_Z;
        if (((old_a & 0x0f) + (value & 0x0f)) > 0x0f)
            cpu->f |= FLAG_H;
        if (result > 0xff)
            cpu->f |= FLAG_C;

        cpu->pc += 1;
        *cycles = source == 6 ? 8 : 4;
        return GB_STEP_OK;
    }
    if ((opcode & 0xcf) == 0x01)
    {
        uint8_t low = gb_memory_read(memory, (uint16_t)(cpu->pc + 1));
        uint8_t high = gb_memory_read(memory, (uint16_t)(cpu->pc + 2));
        uint16_t value = (uint16_t)(low | ((uint16_t)high << 8));

        switch ((opcode >> 4) & 3u)
        {
        case 0:
            cpu->b = high;
            cpu->c = low;
            break; /* BC */
        case 1:
            cpu->d = high;
            cpu->e = low;
            break; /* DE */
        case 2:
            cpu->h = high;
            cpu->l = low;
            break; /* HL */
        case 3:
            cpu->sp = value;
            break; /* SP */
        }

        cpu->pc += 3;
        *cycles = 12;
        return GB_STEP_OK;
    }
    if ((opcode & 0xc7) == 0x02)
    {
        unsigned pair = (opcode >> 4) & 3u;
        uint16_t address;

        switch (pair)
        {
        case 0:
            address = (uint16_t)((cpu->b << 8) | cpu->c);
            break;
        case 1:
            address = (uint16_t)((cpu->d << 8) | cpu->e);
            break;
        default:
            address = (uint16_t)((cpu->h << 8) | cpu->l);
            break;
        }

        if (opcode & 0x08)
        {
            cpu->a = gb_memory_read(memory, address);
        }
        else
        {
            gb_memory_write(memory, address, cpu->a);
        }

        if (pair >= 2)
        {
            uint16_t next = (uint16_t)(address + (pair == 2 ? 1 : -1));
            cpu->h = (uint8_t)(next >> 8);
            cpu->l = (uint8_t)next;
        }

        cpu->pc += 1;
        *cycles = 8;
        return GB_STEP_OK;
    }
    switch (opcode)
    {
    case 0x00: /* NOP: do nothing */
        cpu->pc += 1;
        *cycles = 4;
        return GB_STEP_OK;
    // case 0x3e: /* LD A,d8: load the following byte into register A */
    //     cpu->a = gb_memory_read(memory, (uint16_t)(cpu->pc + 1));
    //     cpu->pc += 2;
    //     *cycles = 8;
    //     return GB_STEP_OK;
    // case 0x06: /* LD B,d8 */
    //     cpu->b = gb_memory_read(memory, (uint16_t)(cpu->pc + 1));
    //     cpu->pc += 2;
    //     *cycles = 8;
    //     return GB_STEP_OK;
    // case 0x0e: /* LD C,d8: put the next byte into register C */
    //     cpu->c = gb_memory_read(memory, (uint16_t)(cpu->pc + 1));
    //     cpu->pc += 2;
    //     *cycles = 8;
    //     return GB_STEP_OK;
    // case 0x80:
    // { /* ADD A,B: update result and Z/N/H/C flags */
    //     unsigned result = (unsigned)cpu->a + cpu->b;
    //     uint8_t flags = 0;
    //     if ((uint8_t)result == 0)
    //         flags |= FLAG_Z;
    //     if (((cpu->a & 0x0f) + (cpu->b & 0x0f)) > 0x0f)
    //         flags |= FLAG_H;
    //     if (result > 0xff)
    //         flags |= FLAG_C;
    //     cpu->a = (uint8_t)result;
    //     cpu->f = flags; /* addition clears N */
    //     cpu->pc += 1;
    //     *cycles = 4;
    //     return GB_STEP_OK;
    // }
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
    case 0x18:
    { /* JR e8: jump relative to the next instruction */
        int8_t offset = (int8_t)gb_memory_read(memory, (uint16_t)(cpu->pc + 1));
        cpu->pc = (uint16_t)(cpu->pc + 2 + offset);
        *cycles = 12;
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
