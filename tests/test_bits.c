#include <assert.h>
#include <stdint.h>
#include "gb_bits.h"

int main(void) {
    assert(gb_set_bit(0, 7) == 0x80);
    assert(gb_bit_is_set(0x80, 7));
    assert(!gb_bit_is_set(0x80, 6));
    assert(gb_clear_bit(0xff, 7) == 0x7f);
    assert(gb_set_bit(0x12, 8) == 0x12);
    assert(!gb_bit_is_set(0xff, 8));
    return 0;
}
