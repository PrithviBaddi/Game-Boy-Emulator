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

#ifdef __cplusplus
}
#endif
#endif
