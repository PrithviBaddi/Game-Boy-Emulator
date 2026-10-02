#ifndef GB_TEST_UTIL_H
#define GB_TEST_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gb_cpu.h"

/* Headless machine for short synthetic programs. rom[] is the 32 KiB cartridge
 * image the memory bus borrows; it must outlive every gb_cpu_step call. */
typedef struct
{
    GbCpu cpu;
    GbMemory memory;
    uint8_t rom[0x8000];
} GbTestMachine;

typedef struct
{
    unsigned steps;
    unsigned cycles;
    GbStepResult last;
    bool hit_limit;
} GbTestRun;

void gb_test_reset(GbTestMachine *machine);
bool gb_test_place(GbTestMachine *machine, uint16_t address, const uint8_t *code,
                   size_t length);
/* Clears CPU and ROM, then copies code to the cartridge entry point 0x0100. */
bool gb_test_load(GbTestMachine *machine, const uint8_t *code, size_t length);
/* Stops on halt, stop, illegal, or unsupported, and always by max_steps. */
GbTestRun gb_test_run(GbTestMachine *machine, unsigned max_steps);
uint16_t gb_test_pair(uint8_t high, uint8_t low);

#endif
