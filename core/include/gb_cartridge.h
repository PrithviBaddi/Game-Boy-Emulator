#ifndef GB_CARTRIDGE_H
#define GB_CARTRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char title[17];
    uint8_t cartridge_type;
    uint8_t rom_size_code;
    uint8_t ram_size_code;
    uint8_t header_checksum;
    bool checksum_valid;
} GbCartridgeHeader;

/* Read header fields only; this does not execute or emulate a cartridge. */
bool gb_cartridge_parse_header(const uint8_t *rom, size_t length,
                               GbCartridgeHeader *out);

/* Allocates a ROM buffer owned by the caller; free it with free(). */
bool gb_cartridge_read_file(const char *path, uint8_t **out_rom,
                            size_t *out_length, char *error, size_t error_size);

/* Header ROM size in bytes, or 0 when the code is not a supported size. */
size_t gb_cartridge_rom_bytes(uint8_t rom_size_code);
/* Header RAM size in bytes. Returns false for an unknown code. */
bool gb_cartridge_ram_bytes(uint8_t ram_size_code, size_t *out_bytes);
/* Save lives beside the ROM: game.gb becomes game.sav. */
bool gb_cartridge_save_path(const char *rom_path, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
#endif
