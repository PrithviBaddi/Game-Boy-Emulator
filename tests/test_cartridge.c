#include "gb_check.h"
#include <stdint.h>
#include <string.h>
#include "gb_cartridge.h"

int main(void) {
    uint8_t rom[0x150] = {0};
    memcpy(rom + 0x134, "POCKETGLASS", 11);
    rom[0x147] = 0; /* ROM-only cartridge */
    rom[0x148] = 0; /* 32 KiB declared ROM size */
    uint8_t checksum = 0;
    for (size_t i = 0x134; i <= 0x14c; ++i) {
        checksum = (uint8_t)(checksum - rom[i] - 1);
    }
    rom[0x14d] = checksum;
    GbCartridgeHeader info;
    GB_REQUIRE(gb_cartridge_parse_header(rom, sizeof rom, &info));
    GB_REQUIRE(strcmp(info.title, "POCKETGLASS") == 0);
    GB_REQUIRE(info.cartridge_type == 0);
    GB_REQUIRE(info.checksum_valid);
    GB_REQUIRE(!gb_cartridge_parse_header(rom, 0x14f, &info));
    rom[0x134] ^= 1;
    GB_REQUIRE(gb_cartridge_parse_header(rom, sizeof rom, &info));
    GB_REQUIRE(!info.checksum_valid);
    return 0;
}
