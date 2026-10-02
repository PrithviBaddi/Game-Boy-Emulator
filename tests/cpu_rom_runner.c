#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gb_cpu.h"

/* Headless runner for independent CPU ROMs.
 * Blargg cpu_instrs prints ASCII by writing SB (0xFF01) and then 0x81 to SC
 * (0xFF02). The readme says that is the link-port transcript of the screen.
 * Mooneye tests load the Fibonacci sequence 3,5,8,13,21,34 into BC DE HL and
 * execute LD B,B; failure loads 0x42 into those six registers and does the
 * same. See the Mooneye Test Suite pass/fail section. */

enum
{
    MODE_BLARGG,
    MODE_MOONEYE
};

static int load_rom(const char *path, uint8_t **rom_out, size_t *size_out)
{
    FILE *file = fopen(path, "rb");
    if (!file)
    {
        fprintf(stderr, "BLOCKED %s unreadable\n", path);
        return 2;
    }
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return 2;
    }
    long length = ftell(file);
    if (length <= 0 || length > 0x8000)
    {
        fclose(file);
        fprintf(stderr, "BLOCKED %s size %ld is outside the 32 KiB ROM-only bus\n",
                path, length);
        return 2;
    }
    rewind(file);
    uint8_t *rom = calloc(0x8000, 1);
    if (!rom)
    {
        fclose(file);
        return 2;
    }
    if (fread(rom, 1, (size_t)length, file) != (size_t)length)
    {
        free(rom);
        fclose(file);
        fprintf(stderr, "BLOCKED %s short read\n", path);
        return 2;
    }
    fclose(file);
    /* Bytes past the file are 0x00. Tests that are already 32 KiB are unchanged. */
    *rom_out = rom;
    *size_out = (size_t)length;
    return 0;
}

static int mooneye_signature(const GbCpu *cpu)
{
    if (cpu->b == 3 && cpu->c == 5 && cpu->d == 8 && cpu->e == 13 && cpu->h == 21 &&
        cpu->l == 34)
        return 1;
    if (cpu->b == 0x42 && cpu->c == 0x42 && cpu->d == 0x42 && cpu->e == 0x42 &&
        cpu->h == 0x42 && cpu->l == 0x42)
        return -1;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3 || (strcmp(argv[1], "--blargg") != 0 && strcmp(argv[1], "--mooneye") != 0))
    {
        fprintf(stderr, "usage: cpu_rom_runner --blargg|--mooneye rom.gb\n");
        return 2;
    }
    int mode = strcmp(argv[1], "--blargg") == 0 ? MODE_BLARGG : MODE_MOONEYE;
    const char *path = argv[2];
    uint8_t *rom = NULL;
    size_t file_size = 0;
    int loaded = load_rom(path, &rom, &file_size);
    if (loaded != 0)
        return loaded;

    GbMemory memory;
    GbCpu cpu;
    if (!gb_memory_init(&memory, rom, 0x8000))
    {
        free(rom);
        fprintf(stderr, "BLOCKED %s memory init failed\n", path);
        return 2;
    }
    /* These ROMs are written to start where the boot ROM would have left the CPU. */
    gb_cpu_init_dmg_post_boot(&cpu);

    char serial[8192];
    size_t serial_len = 0;
    serial[0] = '\0';
    const unsigned cycle_limit = 300000000u;
    unsigned total_cycles = 0;
    unsigned steps = 0;
    int status = 2;
    const char *reason = "cycle limit";

    while (total_cycles < cycle_limit && steps < cycle_limit)
    {
        uint16_t address = cpu.pc;
        uint8_t opcode = gb_memory_read(&memory, address);
        unsigned cycles = 0;
        GbStepResult result = gb_cpu_step(&cpu, &memory, &cycles);
        total_cycles += cycles;
        steps += 1;

        if ((memory.io[0x02] & 0x80) != 0)
        {
            if (serial_len + 1 < sizeof serial)
            {
                serial[serial_len++] = (char)memory.io[0x01];
                serial[serial_len] = '\0';
            }
            memory.io[0x02] &= 0x7f;
        }

        if (mode == MODE_MOONEYE && result == GB_STEP_OK && opcode == 0x40 &&
            cpu.pc == (uint16_t)(address + 1))
        {
            int signature = mooneye_signature(&cpu);
            if (signature > 0)
            {
                status = 0;
                reason = "fibonacci signature";
                break;
            }
            if (signature < 0)
            {
                status = 1;
                reason = "failure signature 0x42";
                break;
            }
        }
        if (mode == MODE_BLARGG && serial_len >= 6)
        {
            if (strstr(serial, "Failed") != NULL)
            {
                status = 1;
                reason = "serial reported Failed";
                break;
            }
            if (strstr(serial, "Passed") != NULL)
            {
                status = 0;
                reason = "serial reported Passed";
                break;
            }
        }
        if (result == GB_STEP_ILLEGAL || result == GB_STEP_UNSUPPORTED)
        {
            status = 1;
            reason = result == GB_STEP_ILLEGAL ? "illegal opcode" : "unsupported opcode";
            fprintf(stderr, "opcode 0x%02x at 0x%04x\n", opcode, address);
            break;
        }
    }

    const char *word = status == 0 ? "PASS" : status == 1 ? "FAIL" : "TIMEOUT";
    printf("%s %s cycles %u steps %u (%s)\n", word, path, total_cycles, steps, reason);
    if (serial_len > 0)
    {
        fputs("--- serial ---\n", stdout);
        fwrite(serial, 1, serial_len, stdout);
        if (serial[serial_len - 1] != '\n')
            fputc('\n', stdout);
    }
    free(rom);
    return status == 0 ? 0 : 1;
}
