#ifndef GB_CPU_H
#define GB_CPU_H

#include <stdbool.h>
#include <stdint.h>
#include "gb_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t a, f, b, c, d, e, h, l;
    uint16_t sp, pc;
    bool halted;
} GbCpu;

typedef enum {
    GB_STEP_OK,
    GB_STEP_HALTED,
    GB_STEP_UNSUPPORTED
} GbStepResult;

/* Diagnostic starting state, not yet a complete post-boot hardware state. */
void gb_cpu_init(GbCpu *cpu);
/* Executes one supported opcode. Unsupported opcodes leave PC unchanged. */
GbStepResult gb_cpu_step(GbCpu *cpu, GbMemory *memory, unsigned *cycles);

#ifdef __cplusplus
}
#endif
#endif
