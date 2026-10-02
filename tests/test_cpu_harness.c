#include "gb_check.h"
#include "gb_test_util.h"

int main(void)
{
    GbTestMachine machine;
    /* LD A,0x40; LD B,0x05; ADD A,B; LD (0xC000),A; HALT */
    const uint8_t program[] = {0x3e, 0x40, 0x06, 0x05, 0x80,
                               0xea, 0x00, 0xc0, 0x76};
    GB_REQUIRE(gb_test_load(&machine, program, sizeof program));

    gb_cpu_init(&machine.cpu);
    GB_REQUIRE(machine.cpu.a == 0 && machine.cpu.f == 0);
    GB_REQUIRE(machine.cpu.b == 0 && machine.cpu.c == 0);
    GB_REQUIRE(machine.cpu.d == 0 && machine.cpu.e == 0);
    GB_REQUIRE(machine.cpu.h == 0 && machine.cpu.l == 0);
    GB_REQUIRE(machine.cpu.sp == 0xfffe);
    GB_REQUIRE(machine.cpu.pc == 0x0100);
    GB_REQUIRE(!machine.cpu.halted);

    GbTestRun run = gb_test_run(&machine, 8);
    GB_REQUIRE(!run.hit_limit);
    GB_REQUIRE(run.steps == 5);
    GB_REQUIRE(run.last == GB_STEP_HALTED);
    GB_REQUIRE(run.cycles == 40);
    GB_REQUIRE(machine.cpu.a == 0x45);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xc000) == 0x45);
    GB_REQUIRE(machine.cpu.pc == 0x0109);

    /* A backward jump must still stop at the instruction limit. */
    const uint8_t loop[] = {0x18, 0xfe}; /* JR -2 */
    GB_REQUIRE(gb_test_load(&machine, loop, sizeof loop));
    run = gb_test_run(&machine, 20);
    GB_REQUIRE(run.hit_limit);
    GB_REQUIRE(run.steps == 20);
    GB_REQUIRE(run.cycles == 20 * 12);

    return 0;
}
