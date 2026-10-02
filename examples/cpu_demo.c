#include <stdio.h>
#include <stdint.h>
#include "gb_cpu.h"

int main(void)
{
    uint8_t rom[0x8000] = {0};
    const uint8_t program[] = {0x3e, 0x40, 0x06, 0x05, 0x0e, 0x07, 0x80,
                               0xea, 0x00, 0xc0, 0x76};
    for (unsigned i = 0; i < sizeof program; ++i)
        rom[0x100 + i] = program[i];
    GbMemory memory;
    GbCpu cpu;
    if (!gb_memory_init(&memory, rom, sizeof rom))
        return 1;
    gb_cpu_init(&cpu);
    unsigned total_cycles = 0;
    for (unsigned step = 0; step < 10; ++step)
    {
        uint16_t address = cpu.pc;
        uint8_t opcode = gb_memory_read(&memory, address);
        unsigned cycles;
        GbStepResult result = gb_cpu_step(&cpu, &memory, &cycles);
        if (result == GB_STEP_UNSUPPORTED)
        {
            fprintf(stderr, "Unsupported opcode 0x%02x at 0x%04x\n", opcode, address);
            return 1;
        }
        total_cycles += cycles;
        printf("PC 0x%04x  opcode 0x%02x  A 0x%02x  cycles %u\n",
               address, opcode, cpu.a, cycles);
        if (result == GB_STEP_HALTED)
        {
            printf("RAM[0xC000] = %u (expected 69); total cycles = %u\n",
                   gb_memory_read(&memory, 0xc000), total_cycles);
            return gb_memory_read(&memory, 0xc000) == 69 ? 0 : 1;
        }
    }
    fprintf(stderr, "Program failed to halt after 10 steps\n");
    return 1;
}
