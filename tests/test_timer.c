#include "gb_check.h"
#include "gb_test_util.h"
#include "gb_timer.h"

static void reset_divider(GbMemory *memory)
{
    gb_memory_write(memory, 0xff04, 0);
}

static uint8_t tima(const GbMemory *memory)
{
    return gb_memory_read(memory, 0xff05);
}

static void arm(GbMemory *memory, uint8_t tac, uint8_t start)
{
    reset_divider(memory);
    gb_memory_write(memory, 0xff06, 0);
    gb_memory_write(memory, 0xff05, start);
    gb_memory_write(memory, 0xff0f, 0);
    gb_memory_write(memory, 0xff07, tac);
}

static int test_div_and_masks(void)
{
    uint8_t rom[0x8000] = {0};
    GbMemory memory;
    GB_REQUIRE(gb_memory_init(&memory, rom, sizeof rom));
    GB_REQUIRE(gb_memory_read(&memory, 0xff04) == 0);
    GB_REQUIRE(gb_memory_read(&memory, 0xff07) == 0xf8);

    gb_timer_advance(&memory, 252);
    GB_REQUIRE(gb_memory_read(&memory, 0xff04) == 0);
    gb_timer_advance(&memory, 4);
    GB_REQUIRE(gb_memory_read(&memory, 0xff04) == 1);
    gb_memory_write(&memory, 0xff04, 0x99);
    GB_REQUIRE(gb_memory_read(&memory, 0xff04) == 0);

    gb_memory_write(&memory, 0xff07, 0x05);
    GB_REQUIRE(gb_memory_read(&memory, 0xff07) == 0xfd);
    gb_memory_write(&memory, 0xff05, 0x12);
    gb_memory_write(&memory, 0xff06, 0x34);
    GB_REQUIRE(tima(&memory) == 0x12);
    GB_REQUIRE(gb_memory_read(&memory, 0xff06) == 0x34);
    /* Reading the registers must not itself move the divider. */
    GB_REQUIRE(gb_memory_read(&memory, 0xff04) == 0);
    return 0;
}

static int test_frequencies(void)
{
    /* Pan Docs periods, in T-cycles: 1024, 16, 64, 256. */
    static const unsigned periods[] = {1024u, 16u, 64u, 256u};
    static const uint8_t tac[] = {0x04, 0x05, 0x06, 0x07};
    uint8_t rom[0x8000] = {0};
    for (int rate = 0; rate < 4; ++rate)
    {
        GbMemory memory;
        GB_REQUIRE(gb_memory_init(&memory, rom, sizeof rom));
        arm(&memory, tac[rate], 0);
        gb_timer_advance(&memory, periods[rate] - 4u);
        GB_REQUIRE(tima(&memory) == 0);
        gb_timer_advance(&memory, 4);
        GB_REQUIRE(tima(&memory) == 1);
        gb_timer_advance(&memory, periods[rate]);
        GB_REQUIRE(tima(&memory) == 2);
    }
    return 0;
}

static int test_edges(void)
{
    uint8_t rom[0x8000] = {0};
    GbMemory memory;
    GB_REQUIRE(gb_memory_init(&memory, rom, sizeof rom));

    /* Bit 9 is high at T-cycle 512. Turning the timer off is a falling edge. */
    arm(&memory, 0x04, 0);
    gb_timer_advance(&memory, 512);
    GB_REQUIRE(tima(&memory) == 0);
    gb_memory_write(&memory, 0xff07, 0x00);
    GB_REQUIRE(tima(&memory) == 1);

    /* Bit 9 set, clock select 01 watches bit 3, which is clear at 512. */
    arm(&memory, 0x04, 0);
    gb_timer_advance(&memory, 512);
    gb_memory_write(&memory, 0xff07, 0x05);
    GB_REQUIRE(tima(&memory) == 1);

    /* Same bit still selected: no extra increment. */
    arm(&memory, 0x04, 0);
    gb_timer_advance(&memory, 512);
    gb_memory_write(&memory, 0xff07, 0x04);
    GB_REQUIRE(tima(&memory) == 0);

    /* DIV reset drops bit 3 while the fast timer is enabled. */
    arm(&memory, 0x05, 10);
    gb_timer_advance(&memory, 8); /* bit 3 rises at 8 and falls at 16 */
    GB_REQUIRE(tima(&memory) == 10);
    gb_memory_write(&memory, 0xff04, 0);
    GB_REQUIRE(tima(&memory) == 11);
    GB_REQUIRE(gb_memory_read(&memory, 0xff04) == 0);
    return 0;
}

static int test_overflow(void)
{
    uint8_t rom[0x8000] = {0};
    GbMemory memory;
    GB_REQUIRE(gb_memory_init(&memory, rom, sizeof rom));
    arm(&memory, 0x05, 0xff);
    gb_memory_write(&memory, 0xff06, 0x3c);

    gb_timer_advance(&memory, 12);
    GB_REQUIRE(tima(&memory) == 0xff);
    gb_timer_advance(&memory, 4);
    GB_REQUIRE(tima(&memory) == 0x00);
    GB_REQUIRE((gb_memory_read(&memory, 0xff0f) & 0x04) == 0);

    /* A TIMA write in the zero window cancels the reload and the interrupt. */
    gb_memory_write(&memory, 0xff05, 0x55);
    gb_timer_advance(&memory, 4);
    GB_REQUIRE(tima(&memory) == 0x55);
    GB_REQUIRE((gb_memory_read(&memory, 0xff0f) & 0x04) == 0);

    arm(&memory, 0x05, 0xff);
    gb_memory_write(&memory, 0xff06, 0x3c);
    gb_timer_advance(&memory, 16);
    GB_REQUIRE(tima(&memory) == 0x00);
    gb_timer_advance(&memory, 4);
    GB_REQUIRE(tima(&memory) == 0x3c);
    GB_REQUIRE((gb_memory_read(&memory, 0xff0f) & 0x04) != 0);

    /* During the reload cycle the TIMA write is lost and a TMA write copies. */
    gb_memory_write(&memory, 0xff05, 0x11);
    GB_REQUIRE(tima(&memory) == 0x3c);
    gb_memory_write(&memory, 0xff06, 0x77);
    GB_REQUIRE(tima(&memory) == 0x77);
    return 0;
}

static int test_cpu_div_phase(void)
{
    /* Matches Mooneye div_timing: LD A,(HL) reads DIV on its second cycle. */
    uint8_t early[2 + 61 + 1];
    early[0] = 0xaf; /* XOR A */
    early[1] = 0x77; /* LD (HL),A */
    for (int i = 0; i < 61; ++i)
        early[2 + i] = 0x00;
    early[63] = 0x7e; /* LD A,(HL) */

    GbTestMachine machine;
    GB_REQUIRE(gb_test_load(&machine, early, sizeof early));
    machine.cpu.h = 0xff;
    machine.cpu.l = 0x04;
    GbTestRun run = gb_test_run(&machine, 64);
    GB_REQUIRE(run.steps == 64 && run.last == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x00);

    uint8_t on_time[2 + 62 + 1];
    on_time[0] = 0xaf;
    on_time[1] = 0x77;
    for (int i = 0; i < 62; ++i)
        on_time[2 + i] = 0x00;
    on_time[64] = 0x7e;
    GB_REQUIRE(gb_test_load(&machine, on_time, sizeof on_time));
    machine.cpu.h = 0xff;
    machine.cpu.l = 0x04;
    run = gb_test_run(&machine, 65);
    GB_REQUIRE(run.steps == 65 && machine.cpu.a == 0x01);
    return 0;
}

static int test_execution_halt_and_interrupt(void)
{
    GbTestMachine machine;
    uint8_t nops[64];
    for (int i = 0; i < 64; ++i)
        nops[i] = 0x00;
    GB_REQUIRE(gb_test_load(&machine, nops, 63));
    GbTestRun run = gb_test_run(&machine, 63);
    GB_REQUIRE(run.steps == 63 && gb_memory_read(&machine.memory, 0xff04) == 0);
    GB_REQUIRE(gb_test_load(&machine, nops, 64));
    run = gb_test_run(&machine, 64);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xff04) == 1);

    /* Four NOPs reach the first TIMA overflow of the /16 timer; the fifth
     * reloads TMA and requests the interrupt. */
    uint8_t five[] = {0x00, 0x00, 0x00, 0x00, 0x00};
    GB_REQUIRE(gb_test_load(&machine, five, sizeof five));
    arm(&machine.memory, 0x05, 0xff);
    gb_memory_write(&machine.memory, 0xff06, 0xab);
    run = gb_test_run(&machine, 4);
    GB_REQUIRE(run.steps == 4 && tima(&machine.memory) == 0x00);
    GB_REQUIRE((gb_memory_read(&machine.memory, 0xff0f) & 0x04) == 0);
    unsigned cycles = 0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(cycles == 4 && tima(&machine.memory) == 0xab);
    GB_REQUIRE((gb_memory_read(&machine.memory, 0xff0f) & 0x04) != 0);

    /* HALT keeps the divider running. */
    const uint8_t halt[] = {0x76};
    GB_REQUIRE(gb_test_load(&machine, halt, 1));
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_HALTED);
    reset_divider(&machine.memory);
    for (int i = 0; i < 63; ++i)
        GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_HALTED);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xff04) == 0);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_HALTED);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xff04) == 1 && cycles == 4);

    /* STOP resets DIV and later idle steps do not advance it. */
    const uint8_t stop[] = {0x10, 0x00};
    GB_REQUIRE(gb_test_load(&machine, stop, sizeof stop));
    gb_timer_advance(&machine.memory, 256);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xff04) == 1);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_STOPPED);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xff04) == 0 && cycles == 4);
    for (int i = 0; i < 80; ++i)
        GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_STOPPED);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xff04) == 0);

    /* Interrupt service is 20 T-cycles of divider time, and it goes to 0x0050. */
    GB_REQUIRE(gb_test_load(&machine, nops, 60));
    run = gb_test_run(&machine, 60);
    GB_REQUIRE(run.cycles == 240 && gb_memory_read(&machine.memory, 0xff04) == 0);
    gb_memory_write(&machine.memory, 0xffff, 0x04);
    gb_cpu_request_interrupt(&machine.memory, GB_INT_TIMER);
    machine.cpu.ime = true;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.pc == 0x0050 && cycles == 20 && !machine.cpu.ime);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xff04) == 1);

    /* EI still finishes the next instruction before a pending timer request. */
    const uint8_t ei_nop[] = {0xfb, 0x00, 0x00};
    GB_REQUIRE(gb_test_load(&machine, ei_nop, sizeof ei_nop));
    gb_memory_write(&machine.memory, 0xffff, 0x04);
    gb_memory_write(&machine.memory, 0xff0f, 0x04);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(!machine.cpu.ime && machine.cpu.pc == 0x0101);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.ime && machine.cpu.pc == 0x0102);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.pc == 0x0050 && !machine.cpu.ime);

    const uint8_t reti[] = {0xd9};
    GB_REQUIRE(gb_test_load(&machine, reti, 1));
    machine.cpu.sp = 0xc000;
    gb_memory_write(&machine.memory, 0xc000, 0x00);
    gb_memory_write(&machine.memory, 0xc001, 0x02);
    gb_memory_write(&machine.memory, 0xffff, 0x04);
    gb_memory_write(&machine.memory, 0xff0f, 0x04);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.ime && machine.cpu.pc == 0x0200);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.pc == 0x0050 && !machine.cpu.ime);
    return 0;
}

static int test_pop_sees_div(void)
{
    /* POP reads the low byte on the cycle after the opcode. Reset DIV, wait
     * 62 NOPs, and the low byte of POP at FF04 is the increment. */
    const uint8_t prefix[] = {
        0x21, 0x04, 0xff, /* LD HL,FF04 */
        0x31, 0x04, 0xff, /* LD SP,FF04 */
        0xaf,             /* XOR A */
        0x77,             /* LD (HL),A  resets DIV */
    };
    uint8_t body[sizeof prefix + 62 + 1];
    for (size_t i = 0; i < sizeof prefix; ++i)
        body[i] = prefix[i];
    for (int i = 0; i < 62; ++i)
        body[sizeof prefix + (size_t)i] = 0x00;
    body[sizeof prefix + 62] = 0xc1; /* POP BC */

    GbTestMachine machine;
    GB_REQUIRE(gb_test_load(&machine, body, sizeof body));
    /* 4 setup instructions + 62 NOPs + POP. */
    GbTestRun run = gb_test_run(&machine, 67);
    GB_REQUIRE(run.steps == 67 && run.last == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.c == 0x01);
    return 0;
}

int main(void)
{
    if (test_div_and_masks() || test_frequencies() || test_edges() || test_overflow() ||
        test_cpu_div_phase() || test_execution_halt_and_interrupt() || test_pop_sees_div())
        return 1;
    return 0;
}
