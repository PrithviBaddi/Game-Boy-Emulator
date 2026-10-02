#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdlib>
#include <iostream>
#include "gb_bits.h"
#include "gb_cartridge.h"
#include "gb_memory.h"

int main(int argc, char **argv) {
    if (argc > 2) {
        std::cerr << "Usage: pocketglass [path/to/cartridge.gb]\n";
        return 2;
    }
    if (argc == 2) {
        uint8_t *rom = nullptr;
        size_t length = 0;
        char error[160];
        if (!gb_cartridge_read_file(argv[1], &rom, &length, error, sizeof error)) {
            std::cerr << error << '\n';
            return 1;
        }
        GbCartridgeHeader header;
        const bool parsed = gb_cartridge_parse_header(rom, length, &header);
        if (!parsed) {
            std::cerr << "Cartridge header is incomplete.\n";
            std::free(rom);
            return 1;
        }
        std::cout << "Cartridge: " << header.title << '\n';
        std::cout << "File size: " << length << " bytes\n";
        std::cout << "Type code: " << static_cast<unsigned>(header.cartridge_type)
                  << ", ROM size code: " << static_cast<unsigned>(header.rom_size_code)
                  << ", RAM size code: " << static_cast<unsigned>(header.ram_size_code) << '\n';
        std::cout << "Header checksum: "
                  << (header.checksum_valid ? "valid" : "INVALID") << '\n';
        if (header.checksum_valid && header.cartridge_type == 0 &&
            header.rom_size_code == 0 && length == 0x8000) {
            GbMemory memory;
            if (gb_memory_init(&memory, rom, length)) {
                std::cout << "ROM-only memory map ready. Byte at entry address 0x0100: "
                          << static_cast<unsigned>(gb_memory_read(&memory, 0x0100)) << '\n';
            }
        } else {
            std::cout << "Memory mapping currently supports valid 32 KiB ROM-only cartridges.\n";
        }
        std::free(rom);
        std::cout << "The game cannot run yet; the CPU has not been implemented.\n";
    }
    // A 3x scale makes the future Game Boy's 160x144 pixels easy to see.
    constexpr int width = 160 * 3;
    constexpr int height = 144 * 3;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init: " << SDL_GetError() << '\n';
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("Pocketglass - setup window", width, height, 0);
    if (!window) {
        std::cerr << "SDL_CreateWindow: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // The C core sets a bit; the C++ front end reads it and chooses a color.
    const uint8_t flags = gb_set_bit(0, 7);
    const bool green = gb_bit_is_set(flags, 7);
    std::cout << "C core called from C++: bit 7 = " << green << '\n';
    std::cout << "Close the window to exit. No game is running yet.\n";
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
        }
        SDL_SetRenderDrawColor(renderer, green ? 89 : 30, 124, 73, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
