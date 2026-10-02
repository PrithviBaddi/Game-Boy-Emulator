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

static uint16_t pair_value(const GbCpu *cpu, unsigned pair)
{
    switch (pair)
    {
    case 0:
        return (uint16_t)((cpu->b << 8) | cpu->c);
    case 1:
        return (uint16_t)((cpu->d << 8) | cpu->e);
    case 2:
        return (uint16_t)((cpu->h << 8) | cpu->l);
    default:
        return cpu->sp;
    }
}

static void set_pair(GbCpu *cpu, unsigned pair, uint16_t value)
{
    uint8_t high = (uint8_t)(value >> 8);
    uint8_t low = (uint8_t)value;
    switch (pair)
    {
    case 0:
        cpu->b = high;
        cpu->c = low;
        break;
    case 1:
        cpu->d = high;
        cpu->e = low;
        break;
    case 2:
        cpu->h = high;
        cpu->l = low;
        break;
    default:
        cpu->sp = value;
        break;
    }
}

/* op is the block-2 group: 0 ADD, 1 ADC, 2 SUB, 3 SBC, 4 AND, 5 XOR, 6 OR, 7 CP. */
static void alu8(GbCpu *cpu, unsigned op, uint8_t value)
{
    uint8_t a = cpu->a;
    unsigned carry = (cpu->f & FLAG_C) ? 1u : 0u;
    uint8_t flags = 0;

    if (op == 0 || op == 1)
    {
        unsigned extra = op == 1 ? carry : 0u;
        unsigned result = (unsigned)a + value + extra;
        cpu->a = (uint8_t)result;
        if (cpu->a == 0)
            flags |= FLAG_Z;
        if (((a & 0x0f) + (value & 0x0f) + extra) > 0x0f)
            flags |= FLAG_H;
        if (result > 0xff)
            flags |= FLAG_C;
    }
    else if (op == 2 || op == 3)
    {
        unsigned extra = op == 3 ? carry : 0u;
        cpu->a = (uint8_t)(a - value - extra);
        flags |= FLAG_N;
        if (cpu->a == 0)
            flags |= FLAG_Z;
        if ((a & 0x0f) < ((value & 0x0f) + extra))
            flags |= FLAG_H;
        if ((unsigned)a < (unsigned)value + extra)
            flags |= FLAG_C;
    }
    else if (op == 4)
    {
        cpu->a = (uint8_t)(a & value);
        flags = FLAG_H;
        if (cpu->a == 0)
            flags |= FLAG_Z;
    }
    else if (op == 5)
    {
        cpu->a = (uint8_t)(a ^ value);
        if (cpu->a == 0)
            flags |= FLAG_Z;
    }
    else if (op == 6)
    {
        cpu->a = (uint8_t)(a | value);
        if (cpu->a == 0)
            flags |= FLAG_Z;
    }
    else
    {
        /* CP: same flags as SUB, but A stays as it was. */
        unsigned result = (unsigned)a - value;
        flags = FLAG_N;
        if ((uint8_t)result == 0)
            flags |= FLAG_Z;
        if ((a & 0x0f) < (value & 0x0f))
            flags |= FLAG_H;
        if (a < value)
            flags |= FLAG_C;
    }
    cpu->f = flags;
}

/* Accumulator rotates clear Z even when the result is zero. CB rotates do not. */
static uint8_t rotate_left(uint8_t value, bool through_carry, bool carry_in, uint8_t *flags)
{
    bool carry_out = (value & 0x80) != 0;
    uint8_t incoming = through_carry ? (carry_in ? 1u : 0u) : (carry_out ? 1u : 0u);
    value = (uint8_t)((value << 1) | incoming);
    *flags = carry_out ? FLAG_C : 0;
    return value;
}

static uint8_t rotate_right(uint8_t value, bool through_carry, bool carry_in, uint8_t *flags)
{
    bool carry_out = (value & 0x01) != 0;
    uint8_t incoming = through_carry ? (carry_in ? 0x80u : 0u) : (carry_out ? 0x80u : 0u);
    value = (uint8_t)((value >> 1) | incoming);
    *flags = carry_out ? FLAG_C : 0;
    return value;
}

/* Decimal adjust after an add or subtract. H is always cleared; N is kept. */
static void daa(GbCpu *cpu)
{
    uint8_t correction = 0;
    bool carry = (cpu->f & FLAG_C) != 0;
    if ((cpu->f & FLAG_H) || (!(cpu->f & FLAG_N) && (cpu->a & 0x0f) > 9))
        correction |= 0x06;
    if (carry || (!(cpu->f & FLAG_N) && cpu->a > 0x99))
    {
        correction |= 0x60;
        carry = true;
    }
    if (cpu->f & FLAG_N)
        cpu->a = (uint8_t)(cpu->a - correction);
    else
        cpu->a = (uint8_t)(cpu->a + correction);
    cpu->f &= FLAG_N;
    if (cpu->a == 0)
        cpu->f |= FLAG_Z;
    if (carry)
        cpu->f |= FLAG_C;
}

static void add_sp_signed(GbCpu *cpu, int8_t offset, bool write_sp)
{
    uint16_t base = cpu->sp;
    uint8_t low = (uint8_t)base;
    uint8_t off = (uint8_t)offset;
    uint16_t result = (uint16_t)(base + (int16_t)offset);
    uint8_t flags = 0;
    if (((low & 0x0f) + (off & 0x0f)) > 0x0f)
        flags |= FLAG_H;
    if ((unsigned)low + off > 0xff)
        flags |= FLAG_C;
    if (write_sp)
        cpu->sp = result;
    else
    {
        cpu->h = (uint8_t)(result >> 8);
        cpu->l = (uint8_t)result;
    }
    cpu->f = flags; /* Z and N are always cleared */
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
    if (opcode >= 0x80 && opcode <= 0xbf)
    {
        unsigned source = opcode & 7u;
        uint16_t hl = (uint16_t)(((uint16_t)cpu->h << 8) | cpu->l);
        uint8_t value = source == 6
                            ? gb_memory_read(memory, hl)
                            : *register_by_index(cpu, source);
        alu8(cpu, (opcode >> 3) & 7u, value);
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

    if ((opcode & 0xc7) == 0x04 || (opcode & 0xc7) == 0x05)
    {
        unsigned target = (opcode >> 3) & 7u;
        bool decrement = (opcode & 1u) != 0;
        uint16_t hl = (uint16_t)((cpu->h << 8) | cpu->l);

        uint8_t old_value = target == 6
                                ? gb_memory_read(memory, hl)
                                : *register_by_index(cpu, target);
        uint8_t result = (uint8_t)(old_value + (decrement ? -1 : 1));

        cpu->f &= FLAG_C; /* INC and DEC preserve carry */
        if (result == 0)
            cpu->f |= FLAG_Z;
        if (decrement)
        {
            cpu->f |= FLAG_N;
            if ((old_value & 0x0f) == 0)
                cpu->f |= FLAG_H;
        }
        else
        {
            if ((old_value & 0x0f) == 0x0f)
                cpu->f |= FLAG_H;
        }

        if (target == 6)
            gb_memory_write(memory, hl, result);
        else
            *register_by_index(cpu, target) = result;

        cpu->pc += 1;
        *cycles = target == 6 ? 12 : 4;
        return GB_STEP_OK;
    }

    if ((opcode & 0xcf) == 0x03 || (opcode & 0xcf) == 0x0b)
    {
        unsigned pair = (opcode >> 4) & 3u;
        bool decrement = (opcode & 0x08) != 0;
        uint16_t value = (uint16_t)(pair_value(cpu, pair) + (decrement ? -1 : 1));
        set_pair(cpu, pair, value);
        cpu->pc += 1;
        *cycles = 8;
        return GB_STEP_OK;
    }

    if ((opcode & 0xcf) == 0x09)
    {
        unsigned pair = (opcode >> 4) & 3u;
        uint16_t hl = pair_value(cpu, 2);
        uint16_t value = pair_value(cpu, pair);
        unsigned result = (unsigned)hl + value;
        uint8_t flags = (uint8_t)(cpu->f & FLAG_Z); /* ADD HL keeps Z and clears N */
        if (((hl & 0x0fff) + (value & 0x0fff)) > 0x0fff)
            flags |= FLAG_H;
        if (result > 0xffff)
            flags |= FLAG_C;
        cpu->h = (uint8_t)(result >> 8);
        cpu->l = (uint8_t)result;
        cpu->f = flags;
        cpu->pc += 1;
        *cycles = 8;
        return GB_STEP_OK;
    }

    if ((opcode & 0xc7) == 0xc6)
    {
        uint8_t value = gb_memory_read(memory, (uint16_t)(cpu->pc + 1));
        alu8(cpu, (opcode >> 3) & 7u, value);
        cpu->pc += 2;
        *cycles = 8;
        return GB_STEP_OK;
    }

    switch (opcode)
    {
    case 0x00: /* NOP: do nothing */
        cpu->pc += 1;
        *cycles = 4;
        return GB_STEP_OK;
    case 0x07: /* RLCA */
    case 0x17: /* RLA */
    {
        bool through = opcode == 0x17;
        cpu->a = rotate_left(cpu->a, through, (cpu->f & FLAG_C) != 0, &cpu->f);
        cpu->pc += 1;
        *cycles = 4;
        return GB_STEP_OK;
    }
    case 0x0f: /* RRCA */
    case 0x1f: /* RRA */
    {
        bool through = opcode == 0x1f;
        cpu->a = rotate_right(cpu->a, through, (cpu->f & FLAG_C) != 0, &cpu->f);
        cpu->pc += 1;
        *cycles = 4;
        return GB_STEP_OK;
    }
    case 0x27: /* DAA */
        daa(cpu);
        cpu->pc += 1;
        *cycles = 4;
        return GB_STEP_OK;
    case 0x2f: /* CPL */
        cpu->a = (uint8_t)~cpu->a;
        cpu->f = (uint8_t)((cpu->f & (FLAG_Z | FLAG_C)) | FLAG_N | FLAG_H);
        cpu->pc += 1;
        *cycles = 4;
        return GB_STEP_OK;
    case 0x37: /* SCF */
        cpu->f = (uint8_t)((cpu->f & FLAG_Z) | FLAG_C);
        cpu->pc += 1;
        *cycles = 4;
        return GB_STEP_OK;
    case 0x3f: /* CCF */
        cpu->f = (uint8_t)((cpu->f & FLAG_Z) | ((cpu->f & FLAG_C) ? 0 : FLAG_C));
        cpu->pc += 1;
        *cycles = 4;
        return GB_STEP_OK;
    case 0xe0:
    case 0xf0:
    { /* LDH A,(a8) */
        uint8_t offset = gb_memory_read(memory, (uint16_t)(cpu->pc + 1));
        uint16_t address = (uint16_t)(0xff00 | offset);
        if (opcode == 0xe0)
            gb_memory_write(memory, address, cpu->a);
        else
            cpu->a = gb_memory_read(memory, address);
        cpu->pc += 2;
        *cycles = 12;
        return GB_STEP_OK;
    }
    case 0xe2: /* LD (C),A */
    case 0xf2:
    { /* LD A,(C) */
        uint16_t address = (uint16_t)(0xff00 | cpu->c);
        if (opcode == 0xe2)
            gb_memory_write(memory, address, cpu->a);
        else
            cpu->a = gb_memory_read(memory, address);
        cpu->pc += 1;
        *cycles = 8;
        return GB_STEP_OK;
    }
    case 0xf9: /* LD SP,HL */
        cpu->sp = (uint16_t)((cpu->h << 8) | cpu->l);
        cpu->pc += 1;
        *cycles = 8;
        return GB_STEP_OK;

    case 0xfa:
    { /* LD A,(a16): read A from a 16-bit address */
        uint8_t low = gb_memory_read(memory, (uint16_t)(cpu->pc + 1));
        uint8_t high = gb_memory_read(memory, (uint16_t)(cpu->pc + 2));
        uint16_t address = (uint16_t)(low | ((uint16_t)high << 8));
        cpu->a = gb_memory_read(memory, address);
        cpu->pc += 3;
        *cycles = 16;
        return GB_STEP_OK;
    }
    case 0x08:
    { /* LD (a16),SP: store SP low byte, then high byte */
        uint8_t low = gb_memory_read(memory, (uint16_t)(cpu->pc + 1));
        uint8_t high = gb_memory_read(memory, (uint16_t)(cpu->pc + 2));
        uint16_t address = (uint16_t)(low | ((uint16_t)high << 8));
        gb_memory_write(memory, address, (uint8_t)cpu->sp);
        gb_memory_write(memory, (uint16_t)(address + 1), (uint8_t)(cpu->sp >> 8));
        cpu->pc += 3;
        *cycles = 20;
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
    case 0xe8: /* ADD SP,e8 */
    case 0xf8: /* LD HL,SP+e8 */
    {
        int8_t offset = (int8_t)gb_memory_read(memory, (uint16_t)(cpu->pc + 1));
        add_sp_signed(cpu, offset, opcode == 0xe8);
        cpu->pc += 2;
        *cycles = opcode == 0xe8 ? 16 : 12;
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
