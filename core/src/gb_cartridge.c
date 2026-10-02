#include "gb_cartridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { GB_HEADER_END = 0x150, GB_MAX_ROM_SIZE = 8 * 1024 * 1024 };

bool gb_cartridge_parse_header(const uint8_t *rom, size_t length,
                               GbCartridgeHeader *out) {
    if (!rom || !out || length < GB_HEADER_END) return false;
    /* Original Game Boy titles occupy up to 16 bytes starting at 0x134. */
    size_t i = 0;
    for (; i < 16 && rom[0x134 + i] != 0; ++i) {
        const uint8_t c = rom[0x134 + i];
        out->title[i] = c >= 32 && c <= 126 ? (char)c : '?';
    }
    out->title[i] = '\0';
    out->cartridge_type = rom[0x147];
    out->rom_size_code = rom[0x148];
    out->ram_size_code = rom[0x149];
    out->header_checksum = rom[0x14d];

    uint8_t checksum = 0;
    for (size_t address = 0x134; address <= 0x14c; ++address) {
        checksum = (uint8_t)(checksum - rom[address] - 1);
    }
    out->checksum_valid = checksum == out->header_checksum;
    return true;
}

bool gb_cartridge_read_file(const char *path, uint8_t **out_rom,
                            size_t *out_length, char *error, size_t error_size) {
    if (!path || !out_rom || !out_length || !error || error_size == 0) return false;
    *out_rom = NULL;
    *out_length = 0;
    FILE *file = fopen(path, "rb");
    if (!file) {
        snprintf(error, error_size, "Cannot open cartridge: %s", path);
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        snprintf(error, error_size, "Cannot measure cartridge file");
        fclose(file);
        return false;
    }
    long file_size = ftell(file);
    if (file_size < GB_HEADER_END || file_size > GB_MAX_ROM_SIZE ||
        fseek(file, 0, SEEK_SET) != 0) {
        snprintf(error, error_size, "Cartridge must be between 336 bytes and 8 MiB");
        fclose(file);
        return false;
    }
    uint8_t *bytes = (uint8_t *)malloc((size_t)file_size);
    if (!bytes) {
        snprintf(error, error_size, "Out of memory reading cartridge");
        fclose(file);
        return false;
    }
    size_t got = fread(bytes, 1, (size_t)file_size, file);
    int close_result = fclose(file);
    if (got != (size_t)file_size || close_result != 0) {
        snprintf(error, error_size, "Could not read complete cartridge file");
        free(bytes);
        return false;
    }
    *out_rom = bytes;
    *out_length = (size_t)file_size;
    return true;
}

size_t gb_cartridge_rom_bytes(uint8_t rom_size_code)
{
    if (rom_size_code > 8)
        return 0;
    return (size_t)0x8000 << rom_size_code;
}

bool gb_cartridge_ram_bytes(uint8_t ram_size_code, size_t *out_bytes)
{
    if (!out_bytes)
        return false;
    switch (ram_size_code)
    {
    case 0:
        *out_bytes = 0;
        return true;
    case 1:
        *out_bytes = 0x800;
        return true;
    case 2:
        *out_bytes = 0x2000;
        return true;
    case 3:
        *out_bytes = 0x8000;
        return true;
    default:
        return false;
    }
}

bool gb_cartridge_save_path(const char *rom_path, char *out, size_t out_size)
{
    if (!rom_path || !out || out_size < 5)
        return false;
    size_t length = strlen(rom_path);
    if (length + 4 >= out_size)
        return false;
    memcpy(out, rom_path, length + 1);
    if (length >= 3 && strcmp(out + length - 3, ".gb") == 0)
        memcpy(out + length - 3, ".sav", 5);
    else if (length >= 4 && strcmp(out + length - 4, ".gbc") == 0)
        memcpy(out + length - 4, ".sav", 5);
    else if (length + 4 < out_size)
        memcpy(out + length, ".sav", 5);
    else
        return false;
    return true;
}
