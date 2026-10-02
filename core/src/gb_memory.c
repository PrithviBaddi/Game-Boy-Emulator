#include "gb_memory.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "gb_apu.h"
#include "gb_cartridge.h"
#include "gb_cpu.h"
#include "gb_ppu.h"
#include "gb_timer.h"

static bool timer_address(uint16_t address)
{
    return address >= 0xff04 && address <= 0xff07;
}

static bool ppu_address(uint16_t address)
{
    return address == 0xff40 || (address >= 0xff41 && address <= 0xff4b);
}

static bool apu_address(uint16_t address)
{
    return address >= 0xff10 && address <= 0xff3f;
}

static unsigned rom_banks(const GbMemory *memory)
{
    size_t banks = memory->rom_size / 0x4000u;
    return banks == 0 ? 1u : (unsigned)banks;
}

static uint8_t joy_lines(uint8_t select, uint8_t pressed)
{
    uint8_t low = 0x0f;
    if ((select & 0x10) == 0)
    {
        if (pressed & GB_BTN_RIGHT) low = (uint8_t)(low & ~0x01);
        if (pressed & GB_BTN_LEFT) low = (uint8_t)(low & ~0x02);
        if (pressed & GB_BTN_UP) low = (uint8_t)(low & ~0x04);
        if (pressed & GB_BTN_DOWN) low = (uint8_t)(low & ~0x08);
    }
    if ((select & 0x20) == 0)
    {
        if (pressed & GB_BTN_A) low = (uint8_t)(low & ~0x01);
        if (pressed & GB_BTN_B) low = (uint8_t)(low & ~0x02);
        if (pressed & GB_BTN_SELECT) low = (uint8_t)(low & ~0x04);
        if (pressed & GB_BTN_START) low = (uint8_t)(low & ~0x08);
    }
    return low;
}

static void joy_edge(GbMemory *memory, uint8_t old_lines, uint8_t new_lines)
{
    if ((old_lines & (uint8_t)~new_lines) != 0)
        gb_cpu_request_interrupt(memory, GB_INT_JOYPAD);
}

static uint8_t joy_read(const GbMemory *memory)
{
    return (uint8_t)(0xc0 | (memory->cart.joy_select & 0x30) |
                     joy_lines(memory->cart.joy_select, memory->cart.joy_pressed));
}

static void joy_write(GbMemory *memory, uint8_t value)
{
    uint8_t before = joy_lines(memory->cart.joy_select, memory->cart.joy_pressed);
    memory->cart.joy_select = (uint8_t)(value & 0x30);
    joy_edge(memory, before, joy_lines(memory->cart.joy_select, memory->cart.joy_pressed));
}

bool gb_joypad_set_pressed(GbMemory *memory, uint8_t pressed)
{
    if (!memory)
        return false;
    uint8_t old = memory->cart.joy_pressed;
    uint8_t before = joy_lines(memory->cart.joy_select, old);
    memory->cart.joy_pressed = pressed;
    joy_edge(memory, before, joy_lines(memory->cart.joy_select, pressed));
    return (pressed & (uint8_t)~old) != 0;
}

static unsigned mbc1_high_bank(const GbMemory *memory)
{
    unsigned low = memory->cart.rom_bank & 0x1fu;
    if (low == 0)
        low = 1;
    unsigned bank = low | ((unsigned)(memory->cart.ram_bank & 0x03) << 5);
    return bank & (rom_banks(memory) - 1u);
}

static unsigned mapped_rom_bank(const GbMemory *memory, uint16_t address)
{
    unsigned count = rom_banks(memory);
    if (memory->cart.mapper == GB_MAPPER_ROM)
        return address < 0x4000 ? 0u : (count > 1u ? 1u : 0u);
    if (address < 0x4000)
    {
        if (memory->cart.mapper == GB_MAPPER_MBC1 && memory->cart.mode != 0)
            return (((unsigned)memory->cart.ram_bank & 0x03u) << 5) & (count - 1u);
        return 0;
    }
    if (memory->cart.mapper == GB_MAPPER_MBC1)
        return mbc1_high_bank(memory);
    if (memory->cart.mapper == GB_MAPPER_MBC5)
    {
        unsigned bank = ((unsigned)memory->cart.rom_bank_hi << 8) | memory->cart.rom_bank;
        return bank & (count - 1u);
    }
    unsigned bank = memory->cart.rom_bank & 0x7fu;
    if (bank == 0)
        bank = 1;
    return bank & (count - 1u);
}

static uint8_t read_rom(const GbMemory *memory, uint16_t address)
{
    size_t offset = (size_t)mapped_rom_bank(memory, address) * 0x4000u + (address & 0x3fffu);
    if (!memory->rom || offset >= memory->rom_size)
        return 0xff;
    return memory->rom[offset];
}

static void rtc_catch_up(GbMemory *memory)
{
    if (!memory->cart.has_rtc || (memory->cart.rtc[4] & 0x40) != 0)
        return;
    time_t now = time(NULL);
    if (memory->cart.rtc_unix == 0)
        memory->cart.rtc_unix = (int64_t)now;
    int64_t elapsed = (int64_t)now - memory->cart.rtc_unix;
    if (elapsed <= 0)
        return;
    memory->cart.rtc_unix = (int64_t)now;
    int64_t seconds = memory->cart.rtc[0] + elapsed;
    int64_t minutes = memory->cart.rtc[1] + seconds / 60;
    int64_t hours = memory->cart.rtc[2] + minutes / 60;
    int64_t days = ((((int64_t)memory->cart.rtc[4] & 0x01) << 8) | memory->cart.rtc[3]) + hours / 24;
    memory->cart.rtc[0] = (uint8_t)(seconds % 60);
    memory->cart.rtc[1] = (uint8_t)(minutes % 60);
    memory->cart.rtc[2] = (uint8_t)(hours % 24);
    if (days > 511)
    {
        days %= 512;
        memory->cart.rtc[4] = (uint8_t)((memory->cart.rtc[4] & 0xc0) | 0x80 | ((days >> 8) & 1));
    }
    else
        memory->cart.rtc[4] = (uint8_t)((memory->cart.rtc[4] & 0xc0) | ((days >> 8) & 1));
    memory->cart.rtc[3] = (uint8_t)days;
}

static bool ram_accessible(const GbMemory *memory)
{
    if (!memory->cart.has_ram && !(memory->cart.has_rtc && memory->cart.ram_bank >= 0x08))
        return false;
    if (memory->cart.mapper == GB_MAPPER_ROM)
        return memory->cart.has_ram;
    return memory->cart.ram_enable;
}

static bool external_offset(const GbMemory *memory, uint16_t address, size_t *offset)
{
    if (memory->cart.mapper == GB_MAPPER_MBC3 && memory->cart.ram_bank >= 0x08)
        return false;
    if (!memory->cart.has_ram || !ram_accessible(memory))
        return false;
    unsigned bank = 0;
    if (memory->cart.mapper == GB_MAPPER_MBC1 && memory->cart.mode != 0)
        bank = memory->cart.ram_bank & 0x03u;
    else if (memory->cart.mapper == GB_MAPPER_MBC3)
        bank = memory->cart.ram_bank & 0x03u;
    else if (memory->cart.mapper == GB_MAPPER_MBC5)
        bank = memory->cart.ram_bank & 0x0fu;
    if (memory->cart.ram_bytes <= 0x800u)
        *offset = address & 0x7ffu;
    else
    {
        unsigned banks = (unsigned)(memory->cart.ram_bytes / 0x2000u);
        if (banks == 0)
            return false;
        *offset = (size_t)(bank % banks) * 0x2000u + (address & 0x1fffu);
    }
    return *offset < memory->cart.ram_bytes && *offset < sizeof memory->cart.eram;
}

static uint8_t read_external(GbMemory *memory, uint16_t address)
{
    if (memory->cart.mapper == GB_MAPPER_MBC3 && memory->cart.ram_bank >= 0x08 &&
        memory->cart.ram_bank <= 0x0c && memory->cart.ram_enable)
    {
        rtc_catch_up(memory);
        return memory->cart.rtc_latched[memory->cart.ram_bank - 0x08];
    }
    size_t offset = 0;
    if (!external_offset(memory, address, &offset))
        return 0xff;
    return memory->cart.eram[offset];
}

static void write_external(GbMemory *memory, uint16_t address, uint8_t value)
{
    if (memory->cart.mapper == GB_MAPPER_MBC3 && memory->cart.ram_bank >= 0x08 &&
        memory->cart.ram_bank <= 0x0c && memory->cart.ram_enable)
    {
        unsigned index = memory->cart.ram_bank - 0x08u;
        if (index == 4)
            value = (uint8_t)(value & 0xc1);
        memory->cart.rtc[index] = value;
        if (index < 4)
            memory->cart.rtc[index] = index == 0 || index == 1 ? (uint8_t)(value % 60)
                                    : index == 2                ? (uint8_t)(value % 24)
                                                                : value;
        return;
    }
    size_t offset = 0;
    if (!external_offset(memory, address, &offset))
        return;
    memory->cart.eram[offset] = value;
}

static void write_mapper(GbMemory *memory, uint16_t address, uint8_t value)
{
    if (memory->cart.mapper == GB_MAPPER_ROM)
        return;
    if (address < 0x2000)
        memory->cart.ram_enable = (value & 0x0f) == 0x0a;
    else if (address < 0x4000)
    {
        if (memory->cart.mapper == GB_MAPPER_MBC5)
        {
            if (address < 0x3000)
                memory->cart.rom_bank = value;
            else
                memory->cart.rom_bank_hi = (uint8_t)(value & 0x01);
        }
        else if (memory->cart.mapper == GB_MAPPER_MBC1)
            memory->cart.rom_bank = (uint8_t)(value & 0x1f);
        else
            memory->cart.rom_bank = (uint8_t)(value & 0x7f);
    }
    else if (address < 0x6000)
    {
        if (memory->cart.mapper == GB_MAPPER_MBC1)
            memory->cart.ram_bank = (uint8_t)(value & 0x03);
        else if (memory->cart.mapper == GB_MAPPER_MBC5)
            memory->cart.ram_bank = (uint8_t)(value & (memory->cart.rumble ? 0x07 : 0x0f));
        else
            memory->cart.ram_bank = (uint8_t)(value & 0x0f);
    }
    else if (memory->cart.mapper == GB_MAPPER_MBC1)
        memory->cart.mode = (uint8_t)(value & 0x01);
    else if (memory->cart.mapper == GB_MAPPER_MBC3)
    {
        if (memory->cart.rtc_latch_prev == 0 && value == 1)
        {
            rtc_catch_up(memory);
            memcpy(memory->cart.rtc_latched, memory->cart.rtc, sizeof memory->cart.rtc);
        }
        memory->cart.rtc_latch_prev = value;
    }
}

static bool kind_of(uint8_t type, GbMapper *mapper, bool *ram, bool *battery, bool *rtc, bool *rumble)
{
    *ram = *battery = *rtc = *rumble = false;
    switch (type)
    {
    case 0x00: *mapper = GB_MAPPER_ROM; return true;
    case 0x08: *mapper = GB_MAPPER_ROM; *ram = true; return true;
    case 0x09: *mapper = GB_MAPPER_ROM; *ram = true; *battery = true; return true;
    case 0x01: *mapper = GB_MAPPER_MBC1; return true;
    case 0x02: *mapper = GB_MAPPER_MBC1; *ram = true; return true;
    case 0x03: *mapper = GB_MAPPER_MBC1; *ram = true; *battery = true; return true;
    case 0x0f: *mapper = GB_MAPPER_MBC3; *battery = true; *rtc = true; return true;
    case 0x10: *mapper = GB_MAPPER_MBC3; *ram = true; *battery = true; *rtc = true; return true;
    case 0x11: *mapper = GB_MAPPER_MBC3; return true;
    case 0x12: *mapper = GB_MAPPER_MBC3; *ram = true; return true;
    case 0x13: *mapper = GB_MAPPER_MBC3; *ram = true; *battery = true; return true;
    case 0x19: *mapper = GB_MAPPER_MBC5; return true;
    case 0x1a: *mapper = GB_MAPPER_MBC5; *ram = true; return true;
    case 0x1b: *mapper = GB_MAPPER_MBC5; *ram = true; *battery = true; return true;
    case 0x1c: *mapper = GB_MAPPER_MBC5; *rumble = true; return true;
    case 0x1d: *mapper = GB_MAPPER_MBC5; *ram = true; *rumble = true; return true;
    case 0x1e: *mapper = GB_MAPPER_MBC5; *ram = true; *battery = true; *rumble = true; return true;
    default: return false;
    }
}

static bool set_error(char *error, size_t error_size, const char *text)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", text);
    return false;
}

bool gb_memory_init(GbMemory *memory, const uint8_t *rom, size_t rom_size)
{
    if (!memory || !rom || rom_size != 0x8000)
        return false;
    memset(memory, 0, sizeof *memory);
    memory->rom = rom;
    memory->rom_size = rom_size;
    memory->cart.mapper = GB_MAPPER_ROM;
    memory->cart.rtc_unix = (int64_t)time(NULL);
    return true;
}

bool gb_memory_load_cartridge(GbMemory *memory, const uint8_t *rom, size_t rom_size,
                              char *error, size_t error_size)
{
    GbCartridgeHeader header;
    if (!gb_cartridge_parse_header(rom, rom_size, &header))
        return set_error(error, error_size, "Cartridge header is incomplete.");
    if (!header.checksum_valid)
        return set_error(error, error_size, "Cartridge header checksum is invalid.");
    if (rom_size > 0x143 && rom[0x143] == 0xc0)
        return set_error(error, error_size,
                         "This is a Game Boy Color-only cartridge. Pocketglass runs original DMG games.");
    GbMapper mapper = GB_MAPPER_ROM;
    bool ram = false, battery = false, rtc = false, rumble = false;
    if (!kind_of(header.cartridge_type, &mapper, &ram, &battery, &rtc, &rumble))
    {
        char text[120];
        snprintf(text, sizeof text,
                 "Unsupported cartridge type 0x%02X. Supported types are ROM-only, ROM+RAM, MBC1, MBC3, and MBC5.",
                 header.cartridge_type);
        return set_error(error, error_size, text);
    }
    size_t expected = gb_cartridge_rom_bytes(header.rom_size_code);
    if (expected == 0 || rom_size != expected)
        return set_error(error, error_size, "ROM file size does not match the header.");
    size_t ram_bytes = 0;
    if (!gb_cartridge_ram_bytes(header.ram_size_code, &ram_bytes))
        return set_error(error, error_size, "RAM size code is not supported.");
    if (ram && ram_bytes == 0)
        return set_error(error, error_size, "Header asks for external RAM but the RAM size is 0.");
    if (!ram && ram_bytes != 0 && !rtc)
        return set_error(error, error_size, "Header RAM size does not match the cartridge type.");
    if (mapper == GB_MAPPER_MBC1 && ram_bytes > 0x8000u)
        return set_error(error, error_size, "MBC1 RAM is larger than 32 KiB.");
    if (!memory)
        return set_error(error, error_size, "No memory to map.");
    memset(memory, 0, sizeof *memory);
    memory->rom = rom;
    memory->rom_size = rom_size;
    memory->cart.mapper = mapper;
    memory->cart.has_ram = ram;
    memory->cart.battery = battery;
    memory->cart.has_rtc = rtc;
    memory->cart.rumble = rumble;
    memory->cart.ram_bytes = ram ? (uint32_t)ram_bytes : 0;
    memory->cart.rtc_unix = (int64_t)time(NULL);
    if (mapper == GB_MAPPER_ROM && ram)
        memory->cart.ram_enable = true;
    /* MBC5 powers up showing bank 1 in the high window. A later write of 0
     * really does select bank 0, which MBC1 does not. */
    if (mapper == GB_MAPPER_MBC5)
        memory->cart.rom_bank = 1;
    return true;
}

uint8_t gb_memory_dma_peek(const GbMemory *memory, uint16_t address)
{
    if (address >= 0xe000)
        address = (uint16_t)(address - 0x2000);
    if (address < 0x8000)
        return read_rom(memory, address);
    if (address < 0xa000)
        return memory->vram[address - 0x8000];
    if (address < 0xc000)
    {
        /* DMA must not mutate the clock, so the const path reports open bus
         * for the RTC window and reads RAM only when it is enabled. */
        GbMemory *mutable_memory = (GbMemory *)memory;
        size_t offset = 0;
        if (!external_offset(mutable_memory, address, &offset))
            return 0xff;
        return memory->cart.eram[offset];
    }
    if (address < 0xe000)
        return memory->wram[address - 0xc000];
    return 0xff;
}

uint8_t gb_memory_read(const GbMemory *memory, uint16_t address)
{
    if (gb_ppu_dma_blocks_cpu(memory, address))
        return 0xff;
    if ((address >= 0x8000 && address <= 0x9fff) || (address >= 0xfe00 && address <= 0xfe9f))
    {
        if (!gb_ppu_cpu_can_access(memory, address))
            return 0xff;
    }
    if (address < 0x8000)
        return read_rom(memory, address);
    if (address < 0xa000)
        return memory->vram[address - 0x8000];
    if (address < 0xc000)
        return read_external((GbMemory *)memory, address);
    if (address < 0xe000)
        return memory->wram[address - 0xc000];
    if (address < 0xfe00)
        return memory->wram[address - 0xe000];
    if (address < 0xfea0)
        return memory->oam[address - 0xfe00];
    if (address < 0xff00)
        return 0xff;
    if (address < 0xff80)
    {
        if (address == 0xff00)
            return joy_read(memory);
        if (timer_address(address))
            return gb_timer_read(memory, address);
        if (ppu_address(address))
            return gb_ppu_read(memory, address);
        if (apu_address(address))
            return gb_apu_read(memory, address);
        uint8_t value = memory->io[address - 0xff00];
        if (address == 0xff0f)
            return (uint8_t)(value | 0xe0);
        return value;
    }
    if (address < 0xffff)
        return memory->hram[address - 0xff80];
    return memory->interrupt_enable;
}

void gb_memory_write(GbMemory *memory, uint16_t address, uint8_t value)
{
    if (gb_ppu_dma_blocks_cpu(memory, address))
        return;
    if (address < 0x8000)
    {
        write_mapper(memory, address, value);
        return;
    }
    if (address < 0xa000)
    {
        if (!gb_ppu_cpu_can_access(memory, address))
            return;
        memory->vram[address - 0x8000] = value;
        return;
    }
    if (address < 0xc000)
    {
        write_external(memory, address, value);
        return;
    }
    if (address < 0xe000)
        memory->wram[address - 0xc000] = value;
    else if (address < 0xfe00)
        memory->wram[address - 0xe000] = value;
    else if (address < 0xfea0)
    {
        if (!gb_ppu_cpu_can_access(memory, address))
            return;
        memory->oam[address - 0xfe00] = value;
    }
    else if (address < 0xff00)
        return;
    else if (address < 0xff80)
    {
        if (address == 0xff00)
        {
            joy_write(memory, value);
            return;
        }
        if (timer_address(address))
        {
            gb_timer_write(memory, address, value);
            return;
        }
        if (ppu_address(address))
        {
            gb_ppu_write(memory, address, value);
            return;
        }
        if (apu_address(address))
        {
            gb_apu_write(memory, address, value);
            return;
        }
        if (address == 0xff0f)
            value &= 0x1f;
        memory->io[address - 0xff00] = value;
    }
    else if (address < 0xffff)
        memory->hram[address - 0xff80] = value;
    else
        memory->interrupt_enable = value;
}

enum { RTC_TRAILER = 18 };

bool gb_memory_load_save(GbMemory *memory, const char *path, char *error, size_t error_size)
{
    if (!memory || !memory->cart.battery)
        return set_error(error, error_size, "This cartridge has no battery.");
    FILE *file = fopen(path, "rb");
    if (!file)
        return true;
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return set_error(error, error_size, "Cannot measure the save file.");
    }
    long length = ftell(file);
    rewind(file);
    size_t ram = memory->cart.ram_bytes;
    int with_rtc = memory->cart.has_rtc ? RTC_TRAILER : 0;
    if (length != (long)ram && length != (long)ram + with_rtc)
    {
        fclose(file);
        memory->cart.save_blocked = true;
        return set_error(error, error_size, "Save length does not match this cartridge. The file was left unchanged.");
    }
    if (ram && fread(memory->cart.eram, 1, ram, file) != ram)
    {
        fclose(file);
        memset(memory->cart.eram, 0, sizeof memory->cart.eram);
        memory->cart.save_blocked = true;
        return set_error(error, error_size, "Save file could not be read. Existing RAM was not kept.");
    }
    if (with_rtc && length == (long)ram + RTC_TRAILER)
    {
        uint8_t trailer[RTC_TRAILER];
        if (fread(trailer, 1, sizeof trailer, file) == sizeof trailer)
        {
            memcpy(memory->cart.rtc_latched, trailer, 5);
            memcpy(memory->cart.rtc, trailer + 5, 5);
            int64_t stamp = 0;
            for (int i = 0; i < 8; ++i)
                stamp |= (int64_t)trailer[10 + i] << (8 * i);
            memory->cart.rtc_unix = stamp;
        }
    }
    fclose(file);
    return true;
}

bool gb_memory_store_save(const GbMemory *memory, const char *path, char *error, size_t error_size)
{
    if (!memory || !memory->cart.battery)
        return set_error(error, error_size, "This cartridge has no battery.");
    if (memory->cart.save_blocked)
        return set_error(error, error_size, "Save was not written, so the existing file stays as it is.");
    char temporary[1024];
    if (strlen(path) + 5 >= sizeof temporary)
        return set_error(error, error_size, "Save path is too long.");
    snprintf(temporary, sizeof temporary, "%s.tmp", path);
    FILE *file = fopen(temporary, "wb");
    if (!file)
        return set_error(error, error_size, "Cannot create the temporary save.");
    size_t ram = memory->cart.ram_bytes;
    bool ok = ram == 0 || fwrite(memory->cart.eram, 1, ram, file) == ram;
    if (ok && memory->cart.has_rtc)
    {
        GbMemory *mutable_memory = (GbMemory *)memory;
        rtc_catch_up(mutable_memory);
        uint8_t trailer[RTC_TRAILER] = {0};
        memcpy(trailer, memory->cart.rtc_latched, 5);
        memcpy(trailer + 5, memory->cart.rtc, 5);
        int64_t stamp = memory->cart.rtc_unix;
        for (int i = 0; i < 8; ++i)
            trailer[10 + i] = (uint8_t)(stamp >> (8 * i));
        ok = fwrite(trailer, 1, sizeof trailer, file) == sizeof trailer;
    }
    if (ok)
        ok = fflush(file) == 0;
    int closed = fclose(file);
    if (!ok || closed != 0)
    {
        remove(temporary);
        return set_error(error, error_size, "Save failed before replacing the previous file.");
    }
    if (rename(temporary, path) != 0)
    {
        remove(temporary);
        return set_error(error, error_size, "Could not replace the previous save. It was left in place.");
    }
    return true;
}
