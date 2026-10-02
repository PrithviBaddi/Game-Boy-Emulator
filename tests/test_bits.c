#include "gb_check.h"
#include <stdint.h>
#include "gb_bits.h"

int main(void) {
    GB_REQUIRE(gb_set_bit(0, 7) == 0x80);
    GB_REQUIRE(gb_bit_is_set(0x80, 7));
    GB_REQUIRE(!gb_bit_is_set(0x80, 6));
    GB_REQUIRE(gb_clear_bit(0xff, 7) == 0x7f);
    GB_REQUIRE(gb_set_bit(0x12, 8) == 0x12);
    GB_REQUIRE(!gb_bit_is_set(0xff, 8));
    return 0;
}
