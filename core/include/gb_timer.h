#ifndef GB_TIMER_H
#define GB_TIMER_H

#include <stdint.h>
#include "gb_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Advance the divider by t_cycles. The hardware unit is one machine cycle
 * (4 T-cycles). A remainder shorter than a machine cycle is kept in the timer
 * until a later call completes it. The CPU reports T-cycles and converts here. */
void gb_timer_advance(GbMemory *memory, unsigned t_cycles);
uint8_t gb_timer_read(const GbMemory *memory, uint16_t address);
void gb_timer_write(GbMemory *memory, uint16_t address, uint8_t value);

#ifdef __cplusplus
}
#endif
#endif
