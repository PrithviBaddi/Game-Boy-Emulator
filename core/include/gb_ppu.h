#ifndef GB_PPU_H
#define GB_PPU_H

#include <stdbool.h>
#include <stdint.h>
#include "gb_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One machine cycle of the LCD and of OAM DMA. The timer clock calls this. */
void gb_ppu_on_machine_cycle(GbMemory *memory);
/* Register values a DMG leaves after its boot ROM, with the LCD running at
 * the start of line 0. This is not a cycle-accurate boot, and LY is 0. */
void gb_ppu_apply_dmg_post_boot(GbMemory *memory);
uint8_t gb_ppu_read(const GbMemory *memory, uint16_t address);
void gb_ppu_write(GbMemory *memory, uint16_t address, uint8_t value);
/* CPU view of VRAM and OAM. Internal rendering and DMA do not use these. */
bool gb_ppu_cpu_can_access(const GbMemory *memory, uint16_t address);
/* True while an OAM DMA transfer blocks everything outside FF80–FFFE. */
bool gb_ppu_dma_blocks_cpu(const GbMemory *memory, uint16_t address);
const uint8_t *gb_ppu_pixels(const GbMemory *memory);
bool gb_ppu_frame_ready(const GbMemory *memory);
void gb_ppu_acknowledge_frame(GbMemory *memory);
int gb_ppu_mode(const GbMemory *memory);

#ifdef __cplusplus
}
#endif
#endif
