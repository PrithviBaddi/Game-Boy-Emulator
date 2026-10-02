#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "gb_bits.h"
#include "gb_cartridge.h"
#include "gb_cpu.h"
#include "gb_memory.h"
#include "gb_ppu.h"

namespace {

constexpr int scale = 3;
const uint8_t shade_red[4] = {15, 48, 139, 155};
const uint8_t shade_green[4] = {56, 98, 172, 188};
const uint8_t shade_blue[4] = {15, 48, 15, 15};

void report_stop(const GbCpu *cpu, const GbMemory *memory, GbStepResult result)
{
    const unsigned pc = cpu->pc;
    const unsigned opcode = gb_memory_read(memory, static_cast<uint16_t>(pc));
    if (result == GB_STEP_ILLEGAL)
        std::cerr << "Illegal opcode " << std::hex << opcode
                  << " at " << pc << std::dec << "; execution stopped.\n";
    else if (result == GB_STEP_UNSUPPORTED)
        std::cerr << "Unsupported opcode " << std::hex << opcode
                  << " at " << pc << std::dec << "; execution stopped.\n";
    else if (result == GB_STEP_STOPPED)
        std::cerr << "STOP at " << std::hex << pc << std::dec
                  << ". Joypad wake is not implemented, so the CPU stays stopped.\n";
}

bool present_rom(const char *path)
{
    uint8_t *rom = nullptr;
    size_t length = 0;
    char error[160];
    if (!gb_cartridge_read_file(path, &rom, &length, error, sizeof error)) {
        std::cerr << error << '\n';
        return false;
    }
    GbCartridgeHeader header;
    if (!gb_cartridge_parse_header(rom, length, &header)) {
        std::cerr << "Cartridge header is incomplete.\n";
        std::free(rom);
        return false;
    }
    std::cout << "Cartridge: " << header.title << '\n';
    std::cout << "File size: " << length << " bytes\n";
    std::cout << "Type code: " << static_cast<unsigned>(header.cartridge_type)
              << ", ROM size code: " << static_cast<unsigned>(header.rom_size_code)
              << ", RAM size code: " << static_cast<unsigned>(header.ram_size_code) << '\n';
    std::cout << "Header checksum: " << (header.checksum_valid ? "valid" : "INVALID") << '\n';
    if (!(header.checksum_valid && header.cartridge_type == 0 &&
          header.rom_size_code == 0 && length == 0x8000)) {
        std::cout << "Memory mapping currently supports valid 32 KiB ROM-only cartridges.\n";
        std::free(rom);
        return false;
    }

    GbMemory memory;
    if (!gb_memory_init(&memory, rom, length)) {
        std::cerr << "Could not map the cartridge.\n";
        std::free(rom);
        return false;
    }
    GbCpu cpu;
    gb_cpu_init_dmg_post_boot(&cpu);
    gb_ppu_apply_dmg_post_boot(&memory);
    std::cout << "Running. Close the window to stop.\n";

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init: " << SDL_GetError() << '\n';
        std::free(rom);
        return false;
    }
    SDL_Window *window = SDL_CreateWindow("Pocketglass", GB_LCD_WIDTH * scale, GB_LCD_HEIGHT * scale, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    SDL_Texture *texture = renderer
        ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                            GB_LCD_WIDTH, GB_LCD_HEIGHT)
        : nullptr;
    if (!texture) {
        std::cerr << "SDL: " << SDL_GetError() << '\n';
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        std::free(rom);
        return false;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    uint8_t rgb[GB_LCD_WIDTH * GB_LCD_HEIGHT * 3];
    bool running = true;
    bool cpu_alive = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
        }
        for (int steps = 0; running && cpu_alive && steps < 20000; ++steps) {
            unsigned spent = 0;
            GbStepResult result = gb_cpu_step(&cpu, &memory, &spent);
            if (result == GB_STEP_ILLEGAL || result == GB_STEP_UNSUPPORTED || result == GB_STEP_STOPPED) {
                report_stop(&cpu, &memory, result);
                cpu_alive = false;
                break;
            }
            if (gb_ppu_frame_ready(&memory)) {
                const uint8_t *shades = gb_ppu_pixels(&memory);
                for (int i = 0; i < GB_LCD_WIDTH * GB_LCD_HEIGHT; ++i) {
                    const uint8_t shade = shades[i] & 3u;
                    rgb[i * 3] = shade_red[shade];
                    rgb[i * 3 + 1] = shade_green[shade];
                    rgb[i * 3 + 2] = shade_blue[shade];
                }
                SDL_UpdateTexture(texture, nullptr, rgb, GB_LCD_WIDTH * 3);
                SDL_RenderTexture(renderer, texture, nullptr, nullptr);
                SDL_RenderPresent(renderer);
                gb_ppu_acknowledge_frame(&memory);
                SDL_Delay(16);
                break;
            }
        }
        if (cpu_alive && !gb_ppu_frame_ready(&memory))
            SDL_Delay(16);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::free(rom);
    return true;
}

void setup_window()
{
    constexpr int width = GB_LCD_WIDTH * scale;
    constexpr int height = GB_LCD_HEIGHT * scale;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init: " << SDL_GetError() << '\n';
        return;
    }
    SDL_Window *window = SDL_CreateWindow("Pocketglass - setup window", width, height, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        std::cerr << "SDL: " << SDL_GetError() << '\n';
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }
    const uint8_t flags = gb_set_bit(0, 7);
    std::cout << "C core called from C++: bit 7 = " << static_cast<unsigned>(gb_bit_is_set(flags, 7))
              << '\n';
    std::cout << "Close the window to exit. Pass a 32 KiB ROM-only cartridge to run it.\n";
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
        }
        SDL_SetRenderDrawColor(renderer, 89, 124, 73, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc > 2) {
        std::cerr << "Usage: pocketglass [path/to/cartridge.gb]\n";
        return 2;
    }
    if (argc == 2)
        return present_rom(argv[1]) ? 0 : 1;
    setup_window();
    return 0;
}
