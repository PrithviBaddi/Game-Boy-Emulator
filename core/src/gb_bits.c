#include "gb_bits.h"

bool gb_bit_is_set(uint8_t byte, unsigned bit_index) {
    return bit_index < 8 && ((byte >> bit_index) & 1u) != 0;
}

uint8_t gb_set_bit(uint8_t byte, unsigned bit_index) {
    return bit_index < 8 ? (uint8_t)(byte | (1u << bit_index)) : byte;
}

uint8_t gb_clear_bit(uint8_t byte, unsigned bit_index) {
    return bit_index < 8 ? (uint8_t)(byte & ~(1u << bit_index)) : byte;
}
