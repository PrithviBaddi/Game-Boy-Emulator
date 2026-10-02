#include "gb_check.h"
#include "gb_test_util.h"

int main(void)
{
    GbTestMachine machine;
    GB_REQUIRE(gb_test_load(&machine, (const uint8_t *)"", 0));
    gb_cpu_init_dmg_post_boot(&machine.cpu);
    GB_REQUIRE(machine.cpu.a == 0x01 && machine.cpu.f == 0xb0);
    GB_REQUIRE(machine.cpu.b == 0x00 && machine.cpu.c == 0x13);
    GB_REQUIRE(machine.cpu.d == 0x00 && machine.cpu.e == 0xd8);
    GB_REQUIRE(gb_test_pair(machine.cpu.h, machine.cpu.l) == 0x014d);
    GB_REQUIRE(machine.cpu.sp == 0xfffe && machine.cpu.pc == 0x0100);
    GB_REQUIRE(!machine.cpu.ime && !machine.cpu.halted && !machine.cpu.stopped);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xff0f) == 0xe0);

    /* VBlank outranks a timer request that is raised at the same time. */
    machine.rom[0x100] = 0x00;
    gb_cpu_init(&machine.cpu);
    gb_memory_write(&machine.memory, 0xffff, 0x1f);
    gb_cpu_request_interrupt(&machine.memory, GB_INT_TIMER);
    gb_cpu_request_interrupt(&machine.memory, GB_INT_VBLANK);
    machine.cpu.ime = true;
    unsigned cycles = 0;
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.pc == 0x0040 && !machine.cpu.ime && cycles == 20);
    GB_REQUIRE(machine.cpu.sp == 0xfffc);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xfffc) == 0x00);
    GB_REQUIRE(gb_memory_read(&machine.memory, 0xfffd) == 0x01);
    GB_REQUIRE((gb_memory_read(&machine.memory, 0xff0f) & 0x1f) == 0x04);

    /* EI lets the next instruction finish before IME is visible. */
    const uint8_t ei_program[] = {0xfb, 0x3e, 0x11, 0x00};
    GB_REQUIRE(gb_test_load(&machine, ei_program, sizeof ei_program));
    gb_memory_write(&machine.memory, 0xffff, 0x01);
    gb_cpu_request_interrupt(&machine.memory, GB_INT_VBLANK);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(!machine.cpu.ime && machine.cpu.pc == 0x0101 && machine.cpu.a == 0);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x11 && machine.cpu.ime && machine.cpu.pc == 0x0103);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.pc == 0x0040 && !machine.cpu.ime);

    /* DI during EI's delay cancels the enable. */
    const uint8_t ei_di[] = {0xfb, 0xf3, 0x00};
    GB_REQUIRE(gb_test_load(&machine, ei_di, sizeof ei_di));
    gb_memory_write(&machine.memory, 0xffff, 0x01);
    gb_cpu_request_interrupt(&machine.memory, GB_INT_VBLANK);
    GbTestRun run = gb_test_run(&machine, 3);
    GB_REQUIRE(run.steps == 3 && run.last == GB_STEP_OK);
    GB_REQUIRE(!machine.cpu.ime && machine.cpu.pc == 0x0103);

    /* IME on at the start of a step takes the interrupt instead of HALT. */
    const uint8_t halt[] = {0x76};
    GB_REQUIRE(gb_test_load(&machine, halt, 1));
    machine.cpu.ime = true;
    gb_memory_write(&machine.memory, 0xffff, 0x08);
    gb_cpu_request_interrupt(&machine.memory, GB_INT_SERIAL);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.pc == 0x0058 && !machine.cpu.halted);

    /* No request: HALT waits, and later steps stay there until IF is set. */
    GB_REQUIRE(gb_test_load(&machine, halt, 1));
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_HALTED);
    GB_REQUIRE(machine.cpu.halted && machine.cpu.pc == 0x0101 && cycles == 4);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_HALTED);
    GB_REQUIRE(machine.cpu.pc == 0x0101);
    machine.cpu.ime = true;
    gb_memory_write(&machine.memory, 0xffff, 0x10);
    gb_cpu_request_interrupt(&machine.memory, GB_INT_JOYPAD);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(!machine.cpu.halted && machine.cpu.pc == 0x0060);

    /* HALT bug: the byte after HALT is both the opcode and its immediate. */
    const uint8_t bug[] = {0x76, 0x3e, 0x99};
    GB_REQUIRE(gb_test_load(&machine, bug, sizeof bug));
    gb_memory_write(&machine.memory, 0xffff, 0x01);
    gb_cpu_request_interrupt(&machine.memory, GB_INT_VBLANK);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(!machine.cpu.halted && machine.cpu.halt_bug && machine.cpu.pc == 0x0101);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.a == 0x3e && machine.cpu.pc == 0x0102 && !machine.cpu.halt_bug);

    const uint8_t stop[] = {0x10, 0x00, 0x00};
    GB_REQUIRE(gb_test_load(&machine, stop, sizeof stop));
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_STOPPED);
    GB_REQUIRE(machine.cpu.stopped && machine.cpu.pc == 0x0102 && cycles == 4);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_STOPPED);
    GB_REQUIRE(machine.cpu.pc == 0x0102);
    machine.cpu.stopped = false; /* joypad wake belongs to a later stage */
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.pc == 0x0103);

    /* RETI enables IME immediately, so the next step can dispatch. */
    const uint8_t reti[] = {0xd9, 0x00};
    GB_REQUIRE(gb_test_load(&machine, reti, sizeof reti));
    machine.cpu.sp = 0xc000;
    gb_memory_write(&machine.memory, 0xc000, 0x01);
    gb_memory_write(&machine.memory, 0xc001, 0x01);
    gb_memory_write(&machine.memory, 0xffff, 0x01);
    gb_cpu_request_interrupt(&machine.memory, GB_INT_VBLANK);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.pc == 0x0101 && machine.cpu.ime);
    GB_REQUIRE(gb_cpu_step(&machine.cpu, &machine.memory, &cycles) == GB_STEP_OK);
    GB_REQUIRE(machine.cpu.pc == 0x0040 && !machine.cpu.ime);

    return 0;
}
