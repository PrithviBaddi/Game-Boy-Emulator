#include "gb_cpu.h"

#include <string.h>
#include "gb_timer.h"

/* One machine cycle, then a bus transfer. Peeking with gb_memory_read does
 * not call these, so a test or the desktop app can inspect memory without
 * moving the divider. */
static uint8_t mread(GbMemory *memory, unsigned *t_cycles, uint16_t address)
{
    gb_timer_advance(memory, 4);
    *t_cycles += 4;
    return gb_memory_read(memory, address);
}

static void mwrite(GbMemory *memory, unsigned *t_cycles, uint16_t address, uint8_t value)
{
    gb_timer_advance(memory, 4);
    *t_cycles += 4;
    gb_memory_write(memory, address, value);
}

static void midle(GbMemory *memory, unsigned *t_cycles)
{
    gb_timer_advance(memory, 4);
    *t_cycles += 4;
}

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

static bool condition_met(const GbCpu *cpu, unsigned condition)
{
    bool zero = (cpu->f & FLAG_Z) != 0;
    bool carry = (cpu->f & FLAG_C) != 0;
    switch (condition)
    {
    case 0:
        return !zero; /* NZ */
    case 1:
        return zero;
    case 2:
        return !carry; /* NC */
    default:
        return carry;
    }
}

static void push16(GbCpu *cpu, GbMemory *memory, unsigned *t_cycles, uint16_t value)
{
    cpu->sp = (uint16_t)(cpu->sp - 1);
    mwrite(memory, t_cycles, cpu->sp, (uint8_t)(value >> 8));
    cpu->sp = (uint16_t)(cpu->sp - 1);
    mwrite(memory, t_cycles, cpu->sp, (uint8_t)value);
}

static uint16_t pop16(GbCpu *cpu, GbMemory *memory, unsigned *t_cycles)
{
    uint8_t low = mread(memory, t_cycles, cpu->sp);
    cpu->sp = (uint16_t)(cpu->sp + 1);
    uint8_t high = mread(memory, t_cycles, cpu->sp);
    cpu->sp = (uint16_t)(cpu->sp + 1);
    return (uint16_t)(low | ((uint16_t)high << 8));
}

static uint16_t stack_pair(const GbCpu *cpu, unsigned pair)
{
    if (pair == 3)
        return (uint16_t)((cpu->a << 8) | (cpu->f & 0xf0));
    return pair_value(cpu, pair);
}

static void set_stack_pair(GbCpu *cpu, unsigned pair, uint16_t value)
{
    if (pair == 3)
    {
        cpu->a = (uint8_t)(value >> 8);
        cpu->f = (uint8_t)(value & 0xf0); /* POP AF clears the unused flag bits */
        return;
    }
    set_pair(cpu, pair, value);
}

/* The HALT bug suppresses the PC increment of the opcode byte, so operand i
 * is read from opcode_pc + i rather than opcode_pc + i + 1. */
static uint8_t operand8(GbMemory *memory, unsigned *t_cycles, uint16_t opcode_pc,
                        bool opcode_repeat, unsigned index)
{
    uint16_t address = (uint16_t)(opcode_pc + index + (opcode_repeat ? 0u : 1u));
    return mread(memory, t_cycles, address);
}

static uint16_t operand16(GbMemory *memory, unsigned *t_cycles, uint16_t opcode_pc,
                          bool opcode_repeat)
{
    uint8_t low = operand8(memory, t_cycles, opcode_pc, opcode_repeat, 0);
    uint8_t high = operand8(memory, t_cycles, opcode_pc, opcode_repeat, 1);
    return (uint16_t)(low | ((uint16_t)high << 8));
}

static uint16_t end_pc(uint16_t opcode_pc, bool opcode_repeat, unsigned length)
{
    return (uint16_t)(opcode_pc + length - (opcode_repeat ? 1u : 0u));
}

static int pending_source(const GbMemory *memory)
{
    uint8_t pending = (uint8_t)(gb_memory_read(memory, 0xff0f) &
                                gb_memory_read(memory, 0xffff) & 0x1fu);
    for (int bit = 0; bit < 5; ++bit)
    {
        if (pending & (uint8_t)(1u << bit))
            return bit;
    }
    return -1;
}

static void dispatch_interrupt(GbCpu *cpu, GbMemory *memory, int source, unsigned *cycles)
{
    static const uint16_t vectors[] = {0x0040, 0x0048, 0x0050, 0x0058, 0x0060};
    unsigned t_cycles = 0;
    cpu->ime = false;
    cpu->ei_delay = 0;
    cpu->halted = false;
    /* Two acknowledge cycles, then the pushed PC, then the jump. 20 T-cycles. */
    midle(memory, &t_cycles);
    midle(memory, &t_cycles);
    uint8_t if_reg = gb_memory_read(memory, 0xff0f);
    gb_memory_write(memory, 0xff0f, (uint8_t)(if_reg & ~(1u << source)));
    push16(cpu, memory, &t_cycles, cpu->pc);
    midle(memory, &t_cycles);
    cpu->pc = vectors[source];
    *cycles = t_cycles;
}

static GbStepResult finish_step(GbCpu *cpu, GbMemory *memory, unsigned *t_cycles,
                                unsigned *cycles, unsigned spent, bool enable_after,
                                GbStepResult result)
{
    if (spent > *t_cycles)
        gb_timer_advance(memory, spent - *t_cycles);
    *cycles = spent;
    if (enable_after && cpu->ei_delay == 1 &&
        (result == GB_STEP_OK || result == GB_STEP_HALTED || result == GB_STEP_STOPPED))
    {
        cpu->ime = true;
        cpu->ei_delay = 0;
    }
    return result;
}

static bool is_illegal(uint8_t opcode)
{
    /* Pan Docs: these opcodes hard-lock the CPU until power-off. */
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

static uint8_t apply_cb(uint8_t value, unsigned operation, bool carry_in, uint8_t *flags)
{
    bool carry_out = false;
    switch (operation)
    {
    case 0: /* RLC */
        carry_out = (value & 0x80) != 0;
        value = (uint8_t)((value << 1) | (carry_out ? 1u : 0u));
        break;
    case 1: /* RRC */
        carry_out = (value & 0x01) != 0;
        value = (uint8_t)((value >> 1) | (carry_out ? 0x80u : 0u));
        break;
    case 2: /* RL */
        carry_out = (value & 0x80) != 0;
        value = (uint8_t)((value << 1) | (carry_in ? 1u : 0u));
        break;
    case 3: /* RR */
        carry_out = (value & 0x01) != 0;
        value = (uint8_t)((value >> 1) | (carry_in ? 0x80u : 0u));
        break;
    case 4: /* SLA */
        carry_out = (value & 0x80) != 0;
        value = (uint8_t)(value << 1);
        break;
    case 5: /* SRA */
        carry_out = (value & 0x01) != 0;
        value = (uint8_t)((value >> 1) | (value & 0x80));
        break;
    case 6: /* SWAP */
        value = (uint8_t)((value << 4) | (value >> 4));
        break;
    default: /* SRL */
        carry_out = (value & 0x01) != 0;
        value = (uint8_t)(value >> 1);
        break;
    }
    *flags = carry_out ? FLAG_C : 0;
    if (value == 0)
        *flags |= FLAG_Z;
    return value;
}

void gb_cpu_init(GbCpu *cpu)
{
    memset(cpu, 0, sizeof *cpu);
    cpu->pc = 0x0100; /* standard cartridge entry address */
    cpu->sp = 0xfffe;
}

void gb_cpu_init_dmg_post_boot(GbCpu *cpu)
{
    gb_cpu_init(cpu);
    cpu->a = 0x01;
    cpu->f = 0xb0;
    cpu->c = 0x13;
    cpu->e = 0xd8;
    cpu->h = 0x01;
    cpu->l = 0x4d;
}

void gb_cpu_leave_stop(GbCpu *cpu)
{
    if (cpu)
        cpu->stopped = false;
}

void gb_cpu_request_interrupt(GbMemory *memory, GbInterrupt source)
{
    if (!memory || source > GB_INT_JOYPAD)
        return;
    uint8_t if_reg = gb_memory_read(memory, 0xff0f);
    gb_memory_write(memory, 0xff0f, (uint8_t)(if_reg | (uint8_t)(1u << source)));
}

GbStepResult gb_cpu_step(GbCpu *cpu, GbMemory *memory, unsigned *cycles)
{
    if (!cpu || !memory || !cycles)
        return GB_STEP_UNSUPPORTED;
    *cycles = 0;
    if (cpu->locked)
        return GB_STEP_ILLEGAL;
    if (cpu->stopped)
    {
        /* STOP freezes the divider. The returned 4 T-cycles are only the
         * caller's view of a step that does not execute. */
        *cycles = 4;
        return GB_STEP_STOPPED;
    }

    int source = pending_source(memory);
    if (cpu->ime && source >= 0)
    {
        dispatch_interrupt(cpu, memory, source, cycles);
        return GB_STEP_OK;
    }
    if (cpu->halted)
    {
        if (source >= 0)
            cpu->halted = false;
        else
        {
            gb_timer_advance(memory, 4);
            *cycles = 4;
            if (pending_source(memory) >= 0)
                cpu->halted = false;
            return GB_STEP_HALTED;
        }
    }

    /* Captured before the instruction, so EI itself does not enable IME yet. */
    bool enable_after = cpu->ei_delay == 1;
    bool opcode_repeat = cpu->halt_bug;
    cpu->halt_bug = false;
    uint16_t op_pc = cpu->pc;
    uint8_t opcode = gb_memory_read(memory, op_pc);
    if (is_illegal(opcode))
    {
        cpu->locked = true;
        return GB_STEP_ILLEGAL;
    }
    /* The opcode fetch is the first machine cycle. The byte itself came from
     * ROM or RAM, so doing the clock edge just after the read does not hide
     * a divider increment from the instruction decoder. */
    unsigned t_cycles = 0;
    gb_timer_advance(memory, 4);
    t_cycles = 4;
    /* A reload that lands on this fetch sets IF during the cycle. The next
     * instruction has not started its work yet, so the interrupt replaces it. */
    if (cpu->ime)
    {
        int raised = pending_source(memory);
        if (raised >= 0)
        {
            dispatch_interrupt(cpu, memory, raised, cycles);
            *cycles += t_cycles;
            return GB_STEP_OK;
        }
    }
    if ((opcode & 0xc7) == 0x06)
    {
        unsigned destination = (opcode >> 3) & 7u;
        uint8_t value = operand8(memory, &t_cycles, op_pc, opcode_repeat, 0);

        unsigned spent;
        if (destination == 6)
        {
            uint16_t hl = (uint16_t)(((uint16_t)cpu->h << 8) | cpu->l);
            mwrite(memory, &t_cycles, hl, value);
            spent = 12;
        }
        else
        {
            *register_by_index(cpu, destination) = value;
            spent = 8;
        }

        cpu->pc = end_pc(op_pc, opcode_repeat, 2);
        return finish_step(cpu, memory, &t_cycles, cycles, spent, enable_after, GB_STEP_OK);
    }
    if (opcode >= 0x40 && opcode <= 0x7f && opcode != 0x76)
    {
        unsigned destination = (opcode >> 3) & 7u;
        unsigned source = opcode & 7u;
        uint16_t hl = (uint16_t)(((uint16_t)cpu->h << 8) | cpu->l);

        uint8_t value;
        if (source == 6)
        {
            value = mread(memory, &t_cycles, hl);
        }
        else
        {
            value = *register_by_index(cpu, source);
        }

        if (destination == 6)
        {
            mwrite(memory, &t_cycles, hl, value);
        }
        else
        {
            *register_by_index(cpu, destination) = value;
        }

        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, (source == 6 || destination == 6) ? 8 : 4, enable_after, GB_STEP_OK);
    }
    if (opcode >= 0x80 && opcode <= 0xbf)
    {
        unsigned source = opcode & 7u;
        uint16_t hl = (uint16_t)(((uint16_t)cpu->h << 8) | cpu->l);
        uint8_t value = source == 6
                            ? mread(memory, &t_cycles, hl)
                            : *register_by_index(cpu, source);
        alu8(cpu, (opcode >> 3) & 7u, value);
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, source == 6 ? 8 : 4, enable_after, GB_STEP_OK);
    }
    if ((opcode & 0xcf) == 0x01)
    {
        uint8_t low = operand8(memory, &t_cycles, op_pc, opcode_repeat, 0);
        uint8_t high = operand8(memory, &t_cycles, op_pc, opcode_repeat, 1);
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

        cpu->pc = end_pc(op_pc, opcode_repeat, 3);
        return finish_step(cpu, memory, &t_cycles, cycles, 12, enable_after, GB_STEP_OK);
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
            cpu->a = mread(memory, &t_cycles, address);
        }
        else
        {
            mwrite(memory, &t_cycles, address, cpu->a);
        }

        if (pair >= 2)
        {
            uint16_t next = (uint16_t)(address + (pair == 2 ? 1 : -1));
            cpu->h = (uint8_t)(next >> 8);
            cpu->l = (uint8_t)next;
        }

        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, 8, enable_after, GB_STEP_OK);
    }

    if ((opcode & 0xc7) == 0x04 || (opcode & 0xc7) == 0x05)
    {
        unsigned target = (opcode >> 3) & 7u;
        bool decrement = (opcode & 1u) != 0;
        uint16_t hl = (uint16_t)((cpu->h << 8) | cpu->l);

        uint8_t old_value = target == 6
                                ? mread(memory, &t_cycles, hl)
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
            mwrite(memory, &t_cycles, hl, result);
        else
            *register_by_index(cpu, target) = result;

        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, target == 6 ? 12 : 4, enable_after, GB_STEP_OK);
    }

    if ((opcode & 0xcf) == 0x03 || (opcode & 0xcf) == 0x0b)
    {
        unsigned pair = (opcode >> 4) & 3u;
        bool decrement = (opcode & 0x08) != 0;
        uint16_t value = (uint16_t)(pair_value(cpu, pair) + (decrement ? -1 : 1));
        set_pair(cpu, pair, value);
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, 8, enable_after, GB_STEP_OK);
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
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, 8, enable_after, GB_STEP_OK);
    }

    if ((opcode & 0xc7) == 0xc6)
    {
        uint8_t value = operand8(memory, &t_cycles, op_pc, opcode_repeat, 0);
        alu8(cpu, (opcode >> 3) & 7u, value);
        cpu->pc = end_pc(op_pc, opcode_repeat, 2);
        return finish_step(cpu, memory, &t_cycles, cycles, 8, enable_after, GB_STEP_OK);
    }

    if ((opcode & 0xe7) == 0x20)
    {
        int8_t offset = (int8_t)operand8(memory, &t_cycles, op_pc, opcode_repeat, 0);
        unsigned spent;
        if (condition_met(cpu, (opcode >> 3) & 3u))
        {
            cpu->pc = (uint16_t)(end_pc(op_pc, opcode_repeat, 2) + offset);
            spent = 12;
        }
        else
        {
            cpu->pc = end_pc(op_pc, opcode_repeat, 2);
            spent = 8;
        }
        return finish_step(cpu, memory, &t_cycles, cycles, spent, enable_after, GB_STEP_OK);
    }

    if ((opcode & 0xe7) == 0xc0)
    {
        unsigned spent;
        if (condition_met(cpu, (opcode >> 3) & 3u))
        {
            midle(memory, &t_cycles);
            cpu->pc = pop16(cpu, memory, &t_cycles);
            spent = 20;
        }
        else
        {
            cpu->pc = end_pc(op_pc, opcode_repeat, 1);
            spent = 8;
        }
        return finish_step(cpu, memory, &t_cycles, cycles, spent, enable_after, GB_STEP_OK);
    }

    if ((opcode & 0xe7) == 0xc2)
    {
        uint16_t target = operand16(memory, &t_cycles, op_pc, opcode_repeat);
        unsigned spent;
        if (condition_met(cpu, (opcode >> 3) & 3u))
        {
            cpu->pc = target;
            spent = 16;
        }
        else
        {
            cpu->pc = end_pc(op_pc, opcode_repeat, 3);
            spent = 12;
        }
        return finish_step(cpu, memory, &t_cycles, cycles, spent, enable_after, GB_STEP_OK);
    }

    if ((opcode & 0xe7) == 0xc4)
    {
        uint16_t target = operand16(memory, &t_cycles, op_pc, opcode_repeat);
        unsigned spent;
        if (condition_met(cpu, (opcode >> 3) & 3u))
        {
            midle(memory, &t_cycles);
            push16(cpu, memory, &t_cycles, end_pc(op_pc, opcode_repeat, 3));
            cpu->pc = target;
            spent = 24;
        }
        else
        {
            cpu->pc = end_pc(op_pc, opcode_repeat, 3);
            spent = 12;
        }
        return finish_step(cpu, memory, &t_cycles, cycles, spent, enable_after, GB_STEP_OK);
    }

    if ((opcode & 0xcf) == 0xc1 || (opcode & 0xcf) == 0xc5)
    {
        unsigned pair = (opcode >> 4) & 3u;
        unsigned spent;
        if (opcode & 0x04)
        {
            midle(memory, &t_cycles);
            push16(cpu, memory, &t_cycles, stack_pair(cpu, pair));
            spent = 16;
        }
        else
        {
            set_stack_pair(cpu, pair, pop16(cpu, memory, &t_cycles));
            spent = 12;
        }
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, spent, enable_after, GB_STEP_OK);
    }

    if ((opcode & 0xc7) == 0xc7)
    {
        midle(memory, &t_cycles);
        push16(cpu, memory, &t_cycles, end_pc(op_pc, opcode_repeat, 1));
        cpu->pc = (uint16_t)(opcode & 0x38);
        return finish_step(cpu, memory, &t_cycles, cycles, 16, enable_after, GB_STEP_OK);
    }

    switch (opcode)
    {
    case 0x00: /* NOP: do nothing */
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, 4, enable_after, GB_STEP_OK);
    case 0x07: /* RLCA */
    case 0x17: /* RLA */
    {
        bool through = opcode == 0x17;
        cpu->a = rotate_left(cpu->a, through, (cpu->f & FLAG_C) != 0, &cpu->f);
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, 4, enable_after, GB_STEP_OK);
    }
    case 0x0f: /* RRCA */
    case 0x1f: /* RRA */
    {
        bool through = opcode == 0x1f;
        cpu->a = rotate_right(cpu->a, through, (cpu->f & FLAG_C) != 0, &cpu->f);
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, 4, enable_after, GB_STEP_OK);
    }
    case 0x27: /* DAA */
        daa(cpu);
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, 4, enable_after, GB_STEP_OK);
    case 0x2f: /* CPL */
        cpu->a = (uint8_t)~cpu->a;
        cpu->f = (uint8_t)((cpu->f & (FLAG_Z | FLAG_C)) | FLAG_N | FLAG_H);
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, 4, enable_after, GB_STEP_OK);
    case 0x37: /* SCF */
        cpu->f = (uint8_t)((cpu->f & FLAG_Z) | FLAG_C);
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, 4, enable_after, GB_STEP_OK);
    case 0x3f: /* CCF */
        cpu->f = (uint8_t)((cpu->f & FLAG_Z) | ((cpu->f & FLAG_C) ? 0 : FLAG_C));
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, 4, enable_after, GB_STEP_OK);
    case 0xe0:
    case 0xf0:
    { /* LDH A,(a8) */
        uint8_t offset = operand8(memory, &t_cycles, op_pc, opcode_repeat, 0);
        uint16_t address = (uint16_t)(0xff00 | offset);
        if (opcode == 0xe0)
            mwrite(memory, &t_cycles, address, cpu->a);
        else
            cpu->a = mread(memory, &t_cycles, address);
        cpu->pc = end_pc(op_pc, opcode_repeat, 2);
        return finish_step(cpu, memory, &t_cycles, cycles, 12, enable_after, GB_STEP_OK);
    }
    case 0xe2: /* LD (C),A */
    case 0xf2:
    { /* LD A,(C) */
        uint16_t address = (uint16_t)(0xff00 | cpu->c);
        if (opcode == 0xe2)
            mwrite(memory, &t_cycles, address, cpu->a);
        else
            cpu->a = mread(memory, &t_cycles, address);
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, 8, enable_after, GB_STEP_OK);
    }
    case 0xf9: /* LD SP,HL */
        cpu->sp = (uint16_t)((cpu->h << 8) | cpu->l);
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, 8, enable_after, GB_STEP_OK);

    case 0xfa:
    { /* LD A,(a16): read A from a 16-bit address */
        uint8_t low = operand8(memory, &t_cycles, op_pc, opcode_repeat, 0);
        uint8_t high = operand8(memory, &t_cycles, op_pc, opcode_repeat, 1);
        uint16_t address = (uint16_t)(low | ((uint16_t)high << 8));
        cpu->a = mread(memory, &t_cycles, address);
        cpu->pc = end_pc(op_pc, opcode_repeat, 3);
        return finish_step(cpu, memory, &t_cycles, cycles, 16, enable_after, GB_STEP_OK);
    }
    case 0x08:
    { /* LD (a16),SP: store SP low byte, then high byte */
        uint8_t low = operand8(memory, &t_cycles, op_pc, opcode_repeat, 0);
        uint8_t high = operand8(memory, &t_cycles, op_pc, opcode_repeat, 1);
        uint16_t address = (uint16_t)(low | ((uint16_t)high << 8));
        mwrite(memory, &t_cycles, address, (uint8_t)cpu->sp);
        mwrite(memory, &t_cycles, (uint16_t)(address + 1), (uint8_t)(cpu->sp >> 8));
        cpu->pc = end_pc(op_pc, opcode_repeat, 3);
        return finish_step(cpu, memory, &t_cycles, cycles, 20, enable_after, GB_STEP_OK);
    }

    case 0xea:
    { /* LD (a16),A: write A to the next 16-bit address */
        uint8_t low = operand8(memory, &t_cycles, op_pc, opcode_repeat, 0);
        uint8_t high = operand8(memory, &t_cycles, op_pc, opcode_repeat, 1);
        uint16_t address = (uint16_t)(low | ((uint16_t)high << 8));
        mwrite(memory, &t_cycles, address, cpu->a);
        cpu->pc = end_pc(op_pc, opcode_repeat, 3);
        return finish_step(cpu, memory, &t_cycles, cycles, 16, enable_after, GB_STEP_OK);
    }
    case 0xe8: /* ADD SP,e8 */
    case 0xf8: /* LD HL,SP+e8 */
    {
        int8_t offset = (int8_t)operand8(memory, &t_cycles, op_pc, opcode_repeat, 0);
        add_sp_signed(cpu, offset, opcode == 0xe8);
        cpu->pc = end_pc(op_pc, opcode_repeat, 2);
        return finish_step(cpu, memory, &t_cycles, cycles, opcode == 0xe8 ? 16 : 12, enable_after, GB_STEP_OK);
    }
    case 0x18:
    { /* JR e8: jump relative to the next instruction */
        int8_t offset = (int8_t)operand8(memory, &t_cycles, op_pc, opcode_repeat, 0);
        cpu->pc = (uint16_t)(end_pc(op_pc, opcode_repeat, 2) + offset);
        return finish_step(cpu, memory, &t_cycles, cycles, 12, enable_after, GB_STEP_OK);
    }
    case 0xc3: /* JP a16 */
        cpu->pc = operand16(memory, &t_cycles, op_pc, opcode_repeat);
        return finish_step(cpu, memory, &t_cycles, cycles, 16, enable_after, GB_STEP_OK);
    case 0xc9: /* RET */
        cpu->pc = pop16(cpu, memory, &t_cycles);
        return finish_step(cpu, memory, &t_cycles, cycles, 16, enable_after, GB_STEP_OK);
    case 0xcd: /* CALL a16: read the target, wait one cycle, then push PC */
    {
        uint16_t target = operand16(memory, &t_cycles, op_pc, opcode_repeat);
        midle(memory, &t_cycles);
        push16(cpu, memory, &t_cycles, end_pc(op_pc, opcode_repeat, 3));
        cpu->pc = target;
        return finish_step(cpu, memory, &t_cycles, cycles, 24, enable_after, GB_STEP_OK);
    }
    case 0xd9: /* RETI */
        cpu->pc = pop16(cpu, memory, &t_cycles);
        cpu->ime = true;
        return finish_step(cpu, memory, &t_cycles, cycles, 16, enable_after, GB_STEP_OK);
    case 0xe9: /* JP HL */
        cpu->pc = pair_value(cpu, 2);
        return finish_step(cpu, memory, &t_cycles, cycles, 4, enable_after, GB_STEP_OK);
    case 0x10: /* STOP: the following byte is skipped, and the divider resets */
    {
        uint16_t skipped = (uint16_t)(op_pc + (opcode_repeat ? 0u : 1u));
        (void)gb_memory_read(memory, skipped);
        gb_memory_write(memory, 0xff04, 0);
        cpu->pc = end_pc(op_pc, opcode_repeat, 2);
        cpu->stopped = true;
        return finish_step(cpu, memory, &t_cycles, cycles, 4, enable_after, GB_STEP_STOPPED);
    }
    case 0x76: /* HALT */
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        /* The DMG HALT bug: a pending interrupt with IME off skips the halt
         * and makes the next opcode byte be read twice. */
        if (!cpu->ime && pending_source(memory) >= 0)
        {
            cpu->halt_bug = true;
            return finish_step(cpu, memory, &t_cycles, cycles, 4, enable_after, GB_STEP_OK);
        }
        cpu->halted = true;
        return finish_step(cpu, memory, &t_cycles, cycles, 4, enable_after, GB_STEP_HALTED);
    case 0xcb:
    {
        uint8_t cb = operand8(memory, &t_cycles, op_pc, opcode_repeat, 0);
        unsigned reg = cb & 7u;
        unsigned group = cb >> 6;
        uint16_t hl = pair_value(cpu, 2);
        uint8_t value = reg == 6 ? mread(memory, &t_cycles, hl)
                                 : *register_by_index(cpu, reg);
        unsigned spent = reg == 6 ? 16u : 8u;
        if (group == 1)
        {
            unsigned bit = (cb >> 3) & 7u;
            uint8_t flags = (uint8_t)((cpu->f & FLAG_C) | FLAG_H);
            if ((value & (uint8_t)(1u << bit)) == 0)
                flags |= FLAG_Z;
            cpu->f = flags;
            spent = reg == 6 ? 12u : 8u;
        }
        else
        {
            if (group == 0)
                value = apply_cb(value, (cb >> 3) & 7u, (cpu->f & FLAG_C) != 0, &cpu->f);
            else
            {
                unsigned bit = (cb >> 3) & 7u;
                uint8_t mask = (uint8_t)(1u << bit);
                value = group == 2 ? (uint8_t)(value & (uint8_t)~mask)
                                   : (uint8_t)(value | mask);
            }
            if (reg == 6)
                mwrite(memory, &t_cycles, hl, value);
            else
                *register_by_index(cpu, reg) = value;
        }
        cpu->pc = end_pc(op_pc, opcode_repeat, 2);
        return finish_step(cpu, memory, &t_cycles, cycles, spent, enable_after, GB_STEP_OK);
    }
    case 0xf3: /* DI */
        cpu->ime = false;
        cpu->ei_delay = 0;
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, 4, enable_after, GB_STEP_OK);
    case 0xfb: /* EI: IME becomes visible after the following instruction */
        if (!cpu->ime)
            cpu->ei_delay = 1;
        cpu->pc = end_pc(op_pc, opcode_repeat, 1);
        return finish_step(cpu, memory, &t_cycles, cycles, 4, enable_after, GB_STEP_OK);
    default:
        return GB_STEP_UNSUPPORTED;
    }
}
