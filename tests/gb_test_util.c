#include "gb_test_util.h"

#include <string.h>

void gb_test_reset(GbTestMachine *machine)
{
    uint8_t rom_copy[0x8000];
    memcpy(rom_copy, machine->rom, sizeof rom_copy);
    memset(machine, 0, sizeof *machine);
    memcpy(machine->rom, rom_copy, sizeof machine->rom);
    gb_memory_init(&machine->memory, machine->rom, sizeof machine->rom);
    gb_cpu_init(&machine->cpu);
}

bool gb_test_place(GbTestMachine *machine, uint16_t address, const uint8_t *code,
                   size_t length)
{
    if (!machine || !code || (size_t)address + length > sizeof machine->rom)
        return false;
    memcpy(machine->rom + address, code, length);
    return true;
}

bool gb_test_load(GbTestMachine *machine, const uint8_t *code, size_t length)
{
    if (!machine)
        return false;
    memset(machine, 0, sizeof *machine);
    if (!gb_test_place(machine, 0x0100, code, length))
        return false;
    if (!gb_memory_init(&machine->memory, machine->rom, sizeof machine->rom))
        return false;
    gb_cpu_init(&machine->cpu);
    return true;
}

GbTestRun gb_test_run(GbTestMachine *machine, unsigned max_steps)
{
    GbTestRun run = {0, 0, GB_STEP_OK, false};
    for (unsigned step = 0; step < max_steps; ++step)
    {
        unsigned cycles = 0;
        run.last = gb_cpu_step(&machine->cpu, &machine->memory, &cycles);
        run.steps += 1;
        run.cycles += cycles;
        if (run.last != GB_STEP_OK)
            return run;
    }
    run.hit_limit = true;
    return run;
}

uint16_t gb_test_pair(uint8_t high, uint8_t low)
{
    return (uint16_t)(((uint16_t)high << 8) | low);
}
