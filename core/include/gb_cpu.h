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
    bool stopped;   /* STOP; gb_cpu_leave_stop clears this after a button press */
    bool ime;       /* master interrupt enable; RETI sets this immediately */
    bool halt_bug;  /* next opcode fetch does not advance PC */
    bool locked;    /* a documented illegal opcode has hard-locked the CPU */
    uint8_t ei_delay; /* 1 means IME turns on after the next instruction */
} GbCpu;

typedef enum {
    GB_STEP_OK,
    GB_STEP_HALTED,
    GB_STEP_UNSUPPORTED,
    GB_STEP_ILLEGAL,
    GB_STEP_STOPPED
} GbStepResult;

/* Bits in IF (0xFF0F) and IE (0xFFFF). Stage 3 devices request these. */
typedef enum {
    GB_INT_VBLANK = 0,
    GB_INT_STAT = 1,
    GB_INT_TIMER = 2,
    GB_INT_SERIAL = 3,
    GB_INT_JOYPAD = 4
} GbInterrupt;

/* Diagnostic starting state. Registers are zero, PC is 0x0100, SP is 0xFFFE.
 * This is not the post-boot state and this project does not include Nintendo's
 * boot ROM. */
void gb_cpu_init(GbCpu *cpu);
/* Documented DMG register values after the boot ROM, without running that ROM
 * or initializing video/IO memory. */
void gb_cpu_init_dmg_post_boot(GbCpu *cpu);
/* Leave the stopped state so the next step executes again. */
void gb_cpu_leave_stop(GbCpu *cpu);
/* Sets one IF bit. Devices in later stages call this; the CPU clears the bit
 * when it dispatches that interrupt. */
void gb_cpu_request_interrupt(GbMemory *memory, GbInterrupt source);
/* Executes one instruction, or dispatches one pending interrupt when IME is
 * set. cycles is T-cycles. An illegal opcode locks the CPU and leaves PC
 * unchanged. Unsupported means the opcode is legal but not implemented. */
GbStepResult gb_cpu_step(GbCpu *cpu, GbMemory *memory, unsigned *cycles);

#ifdef __cplusplus
}
#endif
#endif
