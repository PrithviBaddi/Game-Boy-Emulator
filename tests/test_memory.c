#include <assert.h>
#include <stdint.h>
#include "gb_memory.h"

int main(void) {
    uint8_t rom[0x8000] = {0};
    rom[0x100] = 0x42;
    rom[0x7fff] = 0x99;
    GbMemory m;
    assert(!gb_memory_init(&m, rom, 0x7fff));
    assert(gb_memory_init(&m, rom, sizeof rom));
    assert(gb_memory_read(&m, 0x100) == 0x42);
    assert(gb_memory_read(&m, 0x7fff) == 0x99);
    gb_memory_write(&m, 0x100, 0xff);
    assert(gb_memory_read(&m, 0x100) == 0x42); /* ROM stays read-only */

    gb_memory_write(&m, 0xc123, 0x5a);
    assert(gb_memory_read(&m, 0xe123) == 0x5a); /* echo mirrors work RAM */
    gb_memory_write(&m, 0xfdff, 0x71);
    assert(gb_memory_read(&m, 0xddff) == 0x71);
    gb_memory_write(&m, 0x8000, 0xab);
    assert(gb_memory_read(&m, 0x8000) == 0xab);
    gb_memory_write(&m, 0xfe9f, 0x30);
    assert(gb_memory_read(&m, 0xfe9f) == 0x30);
    gb_memory_write(&m, 0xff80, 0x22);
    assert(gb_memory_read(&m, 0xff80) == 0x22);
    gb_memory_write(&m, 0xffff, 0x01);
    assert(gb_memory_read(&m, 0xffff) == 0x01);
    gb_memory_write(&m, 0xa000, 0xbe);
    assert(gb_memory_read(&m, 0xa000) == 0xff);
    assert(gb_memory_read(&m, 0xfea0) == 0xff);
    return 0;
}
