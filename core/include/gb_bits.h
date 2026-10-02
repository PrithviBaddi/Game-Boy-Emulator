#ifndef GB_BITS_H
#define GB_BITS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The Game Boy moves data in bytes. A byte has bits numbered 0 through 7. */
bool gb_bit_is_set(uint8_t byte, unsigned bit_index);
uint8_t gb_set_bit(uint8_t byte, unsigned bit_index);
uint8_t gb_clear_bit(uint8_t byte, unsigned bit_index);

#ifdef __cplusplus
}
#endif

#endif
