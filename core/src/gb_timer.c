#include "gb_timer.h"

#include "gb_cpu.h"
#include "gb_ppu.h"

/* Falling edges of these bits of the T-cycle counter increment TIMA.
 * Periods are 1024, 16, 64, and 256 T-cycles, which is 256, 4, 16, and 64
 * machine cycles: the four TAC frequencies in Pan Docs. */
static const int k_timer_bit[4] = {9, 3, 5, 7};

static bool timer_signal(uint16_t counter, uint8_t tac)
{
    if ((tac & 0x04) == 0)
        return false;
    return (counter & (uint16_t)(1u << k_timer_bit[tac & 3])) != 0;
}

static void increment_tima(GbTimer *timer)
{
    if (timer->tima == 0xff)
    {
        timer->tima = 0;
        timer->overflow_pending = true;
    }
    else
        timer->tima++;
}

static void machine_cycle(GbMemory *memory)
{
    GbTimer *timer = &memory->timer;
    timer->reloading = false;
    if (timer->overflow_pending)
    {
        timer->tima = timer->tma;
        timer->overflow_pending = false;
        timer->reloading = true;
        gb_cpu_request_interrupt(memory, GB_INT_TIMER);
    }

    uint16_t previous = timer->counter;
    timer->counter = (uint16_t)(timer->counter + 4);
    if (timer_signal(previous, timer->tac) && !timer_signal(timer->counter, timer->tac))
        increment_tima(timer);
    gb_ppu_on_machine_cycle(memory);
}

void gb_timer_advance(GbMemory *memory, unsigned t_cycles)
{
    if (!memory || t_cycles == 0)
        return;
    unsigned pending = (unsigned)memory->timer.phase + t_cycles;
    memory->timer.phase = (uint8_t)(pending % 4u);
    pending /= 4u;
    while (pending--)
        machine_cycle(memory);
}

uint8_t gb_timer_read(const GbMemory *memory, uint16_t address)
{
    const GbTimer *timer = &memory->timer;
    switch (address)
    {
    case 0xff04:
        return (uint8_t)(timer->counter >> 8);
    case 0xff05:
        return timer->tima;
    case 0xff06:
        return timer->tma;
    default:
        /* Unused TAC bits read as 1 on a DMG. */
        return (uint8_t)(timer->tac | 0xf8);
    }
}

void gb_timer_write(GbMemory *memory, uint16_t address, uint8_t value)
{
    GbTimer *timer = &memory->timer;
    switch (address)
    {
    case 0xff04:
        /* Clearing the counter drops the selected bit if it was set. */
        if (timer_signal(timer->counter, timer->tac))
            increment_tima(timer);
        timer->counter = 0;
        timer->phase = 0;
        break;
    case 0xff05:
        if (timer->reloading)
            break; /* the reload overwrites a write in this machine cycle */
        timer->overflow_pending = false;
        timer->tima = value;
        break;
    case 0xff06:
        timer->tma = value;
        if (timer->reloading)
            timer->tima = value;
        break;
    default:
    {
        uint8_t next = (uint8_t)(value & 0x07);
        bool old_signal = timer_signal(timer->counter, timer->tac);
        bool new_signal = timer_signal(timer->counter, next);
        timer->tac = next;
        /* DMG: turning the timer off while its bit is high, or pointing TAC
         * at a bit that is currently low, is itself a falling edge. */
        if (old_signal && !new_signal)
            increment_tima(timer);
        break;
    }
    }
}
