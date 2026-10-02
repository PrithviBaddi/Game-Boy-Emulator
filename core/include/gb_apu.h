#ifndef GB_APU_H
#define GB_APU_H

#include <stdbool.h>
#include <stdint.h>
#include "gb_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One machine cycle of the four sound channels. The timer clock calls this. */
void gb_apu_on_machine_cycle(GbMemory *memory);
uint8_t gb_apu_read(const GbMemory *memory, uint16_t address);
void gb_apu_write(GbMemory *memory, uint16_t address, uint8_t value);
/* Removes the oldest mixed sample. Returns false when none are waiting. */
bool gb_apu_pull_sample(GbMemory *memory, int16_t *left, int16_t *right);

#ifdef __cplusplus
}
#endif
#endif
