#include "gb_check.h"
#include "gb_cartridge.h"
#include "gb_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void checksum(uint8_t *rom)
{
    uint8_t sum = 0;
    for (size_t i = 0x134; i <= 0x14c; ++i)
        sum = (uint8_t)(sum - rom[i] - 1);
    rom[0x14d] = sum;
}

static uint8_t *blank(size_t size, uint8_t type, uint8_t rom_code, uint8_t ram_code)
{
    uint8_t *rom = calloc(1, size);
    if (!rom)
        return NULL;
    memcpy(rom + 0x134, "MBC TEST", 8);
    rom[0x147] = type;
    rom[0x148] = rom_code;
    rom[0x149] = ram_code;
    checksum(rom);
    return rom;
}

static int test_banks(void)
{
    uint8_t *rom = blank(0x10000, 0x01, 1, 0);
    GB_REQUIRE(rom != NULL);
    rom[0x0000] = 0x10;
    rom[0x4000] = 0x21;
    rom[0x8000] = 0x32;
    rom[0xc000] = 0x43;
    GbMemory memory;
    char error[120];
    GB_REQUIRE(gb_memory_load_cartridge(&memory, rom, 0x10000, error, sizeof error));
    GB_REQUIRE(gb_memory_read(&memory, 0x0000) == 0x10);
    gb_memory_write(&memory, 0x2000, 0x00);
    GB_REQUIRE(gb_memory_read(&memory, 0x4000) == 0x21);
    gb_memory_write(&memory, 0x2000, 0x03);
    GB_REQUIRE(gb_memory_read(&memory, 0x4000) == 0x43);
    gb_memory_write(&memory, 0x2000, 0x04);
    GB_REQUIRE(gb_memory_read(&memory, 0x4000) == 0x10);
    free(rom);

    rom = blank(0x100000, 0x03, 5, 3);
    GB_REQUIRE(rom != NULL);
    rom[0x21 * 0x4000] = 0xab;
    rom[0x20 * 0x4000] = 0xcd;
    GB_REQUIRE(gb_memory_load_cartridge(&memory, rom, 0x100000, error, sizeof error));
    gb_memory_write(&memory, 0x4000, 0x01);
    gb_memory_write(&memory, 0x2000, 0x00);
    GB_REQUIRE(gb_memory_read(&memory, 0x4000) == 0xab);
    gb_memory_write(&memory, 0x6000, 0x01);
    GB_REQUIRE(gb_memory_read(&memory, 0x0000) == 0xcd);

    gb_memory_write(&memory, 0x0000, 0x00);
    gb_memory_write(&memory, 0xa000, 0x55);
    GB_REQUIRE(gb_memory_read(&memory, 0xa000) == 0xff);
    GB_REQUIRE(memory.cart.eram[0] == 0);
    gb_memory_write(&memory, 0x0000, 0x0a);
    gb_memory_write(&memory, 0x6000, 0x00);
    gb_memory_write(&memory, 0x4000, 0x00);
    gb_memory_write(&memory, 0xa000, 0x55);
    GB_REQUIRE(gb_memory_read(&memory, 0xa000) == 0x55);
    gb_memory_write(&memory, 0x6000, 0x01);
    gb_memory_write(&memory, 0x4000, 0x01);
    gb_memory_write(&memory, 0xa000, 0x66);
    GB_REQUIRE(gb_memory_read(&memory, 0xa000) == 0x66);
    gb_memory_write(&memory, 0x6000, 0x00);
    GB_REQUIRE(gb_memory_read(&memory, 0xa000) == 0x55);
    free(rom);
    return 0;
}

static int test_rom_ram_and_errors(void)
{
    uint8_t *rom = blank(0x8000, 0x08, 0, 2);
    GB_REQUIRE(rom != NULL);
    GbMemory memory;
    char error[160];
    GB_REQUIRE(gb_memory_load_cartridge(&memory, rom, 0x8000, error, sizeof error));
    gb_memory_write(&memory, 0xa000, 0x42);
    GB_REQUIRE(gb_memory_read(&memory, 0xa000) == 0x42);
    gb_memory_write(&memory, 0xa7ff, 0x99);
    GB_REQUIRE(gb_memory_read(&memory, 0xa7ff) == 0x99);
    free(rom);

    rom = blank(0x8000, 0x19, 0, 0);
    GB_REQUIRE(!gb_memory_load_cartridge(&memory, rom, 0x8000, error, sizeof error));
    free(rom);

    rom = blank(0x8000, 0x01, 1, 0);
    GB_REQUIRE(!gb_memory_load_cartridge(&memory, rom, 0x8000, error, sizeof error));
    free(rom);

    rom = blank(0x8000, 0x00, 0, 0);
    rom[0x134] ^= 1;
    GB_REQUIRE(!gb_memory_load_cartridge(&memory, rom, 0x8000, error, sizeof error));
    free(rom);

    const char *tiny = "build-mbc-tiny.gb";
    FILE *file = fopen(tiny, "wb");
    GB_REQUIRE(file != NULL);
    GB_REQUIRE(fwrite("short", 1, 5, file) == 5);
    fclose(file);
    uint8_t *loaded = NULL;
    size_t length = 0;
    GB_REQUIRE(!gb_cartridge_read_file(tiny, &loaded, &length, error, sizeof error));
    remove(tiny);

    const char *huge = "build-mbc-huge.gb";
    file = fopen(huge, "wb");
    GB_REQUIRE(file != NULL);
    GB_REQUIRE(ftruncate(fileno(file), 8 * 1024 * 1024 + 1) == 0);
    fclose(file);
    GB_REQUIRE(!gb_cartridge_read_file(huge, &loaded, &length, error, sizeof error));
    remove(huge);
    return 0;
}

static int test_save_round_trip(void)
{
    uint8_t *rom = blank(0x8000, 0x03, 0, 2);
    GB_REQUIRE(rom != NULL);
    GbMemory memory;
    char error[160];
    GB_REQUIRE(gb_memory_load_cartridge(&memory, rom, 0x8000, error, sizeof error));
    gb_memory_write(&memory, 0x0000, 0x0a);
    gb_memory_write(&memory, 0xa123, 0x77);
    char path[64];
    GB_REQUIRE(gb_cartridge_save_path("catch.gb", path, sizeof path));
    GB_REQUIRE(strcmp(path, "catch.sav") == 0);
    const char *save = "build-mbc-test.sav";
    GB_REQUIRE(gb_memory_store_save(&memory, save, error, sizeof error));

    GbMemory again;
    GB_REQUIRE(gb_memory_load_cartridge(&again, rom, 0x8000, error, sizeof error));
    GB_REQUIRE(gb_memory_load_save(&again, save, error, sizeof error));
    gb_memory_write(&again, 0x0000, 0x0a);
    GB_REQUIRE(gb_memory_read(&again, 0xa123) == 0x77);

    FILE *bad = fopen(save, "wb");
    GB_REQUIRE(fwrite("bad", 1, 3, bad) == 3);
    fclose(bad);
    GB_REQUIRE(!gb_memory_load_save(&again, save, error, sizeof error));
    again.cart.eram[0] = 0x01;
    GB_REQUIRE(!gb_memory_store_save(&again, save, error, sizeof error));
    FILE *check = fopen(save, "rb");
    char kept[4] = {0};
    GB_REQUIRE(fread(kept, 1, 3, check) == 3);
    fclose(check);
    GB_REQUIRE(memcmp(kept, "bad", 3) == 0);
    remove(save);
    free(rom);
    return 0;
}

static int test_rtc_latch(void)
{
    uint8_t *rom = blank(0x8000, 0x0f, 0, 0);
    GB_REQUIRE(rom != NULL);
    GbMemory memory;
    char error[120];
    GB_REQUIRE(gb_memory_load_cartridge(&memory, rom, 0x8000, error, sizeof error));
    gb_memory_write(&memory, 0x0000, 0x0a);
    gb_memory_write(&memory, 0x4000, 0x0c);
    gb_memory_write(&memory, 0xa000, 0x40);
    gb_memory_write(&memory, 0x4000, 0x08);
    gb_memory_write(&memory, 0xa000, 0x21);
    gb_memory_write(&memory, 0x6000, 0x00);
    gb_memory_write(&memory, 0x6000, 0x01);
    GB_REQUIRE(gb_memory_read(&memory, 0xa000) == 0x21);
    free(rom);
    return 0;
}

int main(void)
{
    if (test_banks() != 0) return 1;
    if (test_rom_ram_and_errors() != 0) return 1;
    if (test_save_round_trip() != 0) return 1;
    if (test_rtc_latch() != 0) return 1;
    return 0;
}
