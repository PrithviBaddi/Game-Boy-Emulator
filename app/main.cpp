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

void report_fault(const GbCpu *cpu, const GbMemory *memory, GbStepResult result)
{
    const unsigned pc = cpu->pc;
    const unsigned opcode = gb_memory_read(memory, static_cast<uint16_t>(pc));
    if (result == GB_STEP_ILLEGAL)
        std::cerr << "Illegal opcode " << std::hex << opcode
                  << " at " << pc << std::dec << "; execution stopped.\n";
    else if (result == GB_STEP_UNSUPPORTED)
        std::cerr << "Unsupported opcode " << std::hex << opcode
                  << " at " << pc << std::dec << "; execution stopped.\n";
}

uint8_t button_mask(SDL_Keycode key, bool down, uint8_t mask)
{
    uint8_t bit = 0;
    switch (key) {
    case SDLK_RIGHT: bit = GB_BTN_RIGHT; break;
    case SDLK_LEFT: bit = GB_BTN_LEFT; break;
    case SDLK_UP: bit = GB_BTN_UP; break;
    case SDLK_DOWN: bit = GB_BTN_DOWN; break;
    case SDLK_Z: bit = GB_BTN_A; break;
    case SDLK_X: bit = GB_BTN_B; break;
    case SDLK_RSHIFT:
    case SDLK_BACKSPACE: bit = GB_BTN_SELECT; break;
    case SDLK_RETURN: bit = GB_BTN_START; break;
    default: break;
    }
    if (!bit) return mask;
    return down ? (uint8_t)(mask | bit) : (uint8_t)(mask & (uint8_t)~bit);
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

    GbMemory memory;
    if (!gb_memory_load_cartridge(&memory, rom, length, error, sizeof error)) {
        std::cerr << error << '\n';
        std::free(rom);
        return false;
    }
    char save_path[1024];
    const bool battery = memory.cart.battery;
    if (battery) {
        if (!gb_cartridge_save_path(path, save_path, sizeof save_path)) {
            std::cerr << "Save path is too long.\n";
            std::free(rom);
            return false;
        }
        if (!gb_memory_load_save(&memory, save_path, error, sizeof error))
            std::cerr << error << '\n';
        else
            std::cout << "Battery save: " << save_path << '\n';
    }
    GbCpu cpu;
    gb_cpu_init_dmg_post_boot(&cpu);
    gb_ppu_apply_dmg_post_boot(&memory);
    std::cout << "Running. Arrows move, Z is A, X is B, Enter is Start, Backspace or Right Shift is Select.\n";
    std::cout << "Close the window to stop.\n";

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
    uint8_t held = 0;
    bool running = true;
    bool cpu_alive = true;
    bool stop_noted = false;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                held = 0;
            else if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                     !event.key.repeat)
                held = button_mask(event.key.key, event.key.down, held);
        }
        if (gb_joypad_set_pressed(&memory, held) && cpu.stopped)
            gb_cpu_leave_stop(&cpu);
        if (cpu.stopped) {
            if (!stop_noted) {
                std::cout << "STOP is waiting for a button.\n";
                stop_noted = true;
            }
            SDL_Delay(16);
            continue;
        }
        stop_noted = false;
        for (int steps = 0; running && cpu_alive && steps < 20000; ++steps) {
            unsigned spent = 0;
            GbStepResult result = gb_cpu_step(&cpu, &memory, &spent);
            if (result == GB_STEP_ILLEGAL || result == GB_STEP_UNSUPPORTED) {
                report_fault(&cpu, &memory, result);
                cpu_alive = false;
                break;
            }
            if (result == GB_STEP_STOPPED)
                break;
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
    if (battery && !gb_memory_store_save(&memory, save_path, error, sizeof error))
        std::cerr << error << '\n';
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
    std::cout << "Close the window to exit. Pass a ROM-only, MBC1, or MBC3 cartridge to run it.\n";
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
