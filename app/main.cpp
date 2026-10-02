#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "gb_apu.h"
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

bool ends_with(const char *path, const char *suffix)
{
    const size_t path_len = std::strlen(path);
    const size_t suffix_len = std::strlen(suffix);
    if (path_len < suffix_len) return false;
    for (size_t i = 0; i < suffix_len; ++i) {
        char a = path[path_len - suffix_len + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

bool format_rejected(const char *path, const uint8_t *rom, size_t length)
{
    if (ends_with(path, ".nes") || ends_with(path, ".sfc") || ends_with(path, ".smc") ||
        ends_with(path, ".gba") || ends_with(path, ".md") || ends_with(path, ".zip")) {
        std::cerr << path << " is not a Game Boy ROM. Pocketglass runs .gb and compatible .gbc cartridges.\n";
        return true;
    }
    if (length >= 4 && rom[0] == 'N' && rom[1] == 'E' && rom[2] == 'S' && rom[3] == 0x1a) {
        std::cerr << path << " is an NES image, not a Game Boy cartridge.\n";
        return true;
    }
    return false;
}

/* 5-wide glyphs, bit 4 is the left pixel. Rows are top to bottom. */
const uint8_t *glyph(char ch)
{
    static const uint8_t space[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t A[7] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    static const uint8_t B[7] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
    static const uint8_t C[7] = {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e};
    static const uint8_t E[7] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
    static const uint8_t H[7] = {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    static const uint8_t K[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    static const uint8_t L[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
    static const uint8_t M[7] = {0x11, 0x1b, 0x15, 0x11, 0x11, 0x11, 0x11};
    static const uint8_t N[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const uint8_t O[7] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    static const uint8_t P[7] = {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
    static const uint8_t R[7] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
    static const uint8_t S[7] = {0x0e, 0x11, 0x10, 0x0e, 0x01, 0x11, 0x0e};
    static const uint8_t T[7] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t U[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    static const uint8_t V[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04};
    static const uint8_t W[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11};
    static const uint8_t X[7] = {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11};
    static const uint8_t Y[7] = {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t Z[7] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f};
    static const uint8_t D[7] = {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    switch (ch) {
    case 'A': return A; case 'B': return B; case 'C': return C; case 'D': return D;
    case 'E': return E; case 'H': return H; case 'K': return K; case 'L': return L;
    case 'M': return M; case 'N': return N; case 'O': return O; case 'P': return P;
    case 'R': return R; case 'S': return S; case 'T': return T; case 'U': return U;
    case 'V': return V; case 'W': return W; case 'X': return X; case 'Y': return Y;
    case 'Z': return Z;
    default: return space;
    }
}

void paint_overlay(uint8_t *rgb, bool paused, bool help, bool muted)
{
    auto dot = [&](int x, int y, uint8_t shade) {
        if (x < 0 || y < 0 || x >= GB_LCD_WIDTH || y >= GB_LCD_HEIGHT) return;
        const int i = (y * GB_LCD_WIDTH + x) * 3;
        rgb[i] = rgb[i + 1] = rgb[i + 2] = shade;
    };
    auto text = [&](int x, int y, const char *s) {
        for (int i = 0; s[i]; ++i) {
            const uint8_t *rows = glyph(s[i]);
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (rows[row] & (0x10 >> col))
                        dot(x + i * 6 + col, y + row, 8);
        }
    };
    if (!paused && !help && !muted) return;
    const int band = (help || paused) ? 78 : 12;
    for (int y = 0; y < band; ++y)
        for (int x = 0; x < GB_LCD_WIDTH; ++x)
            dot(x, y, 40);
    int y = 2;
    if (paused) { text(4, y, "PAUSED"); y += 9; }
    else if (muted) { text(4, y, "MUTED"); y += 9; }
    if (help || paused) {
        text(4, y, "P PAUSE  H HELP");
        text(4, y + 9, "M MUTE");
        text(4, y + 18, "ARROWS Z A X B");
        text(4, y + 27, "ENTER START");
        text(4, y + 36, "BKSP SELECT");
    }
}

void drain_audio(GbMemory *memory, SDL_AudioStream *stream, bool play)
{
    int16_t chunk[1024 * 2];
    int count = 0;
    int16_t left = 0;
    int16_t right = 0;
    while (count < 1024 && gb_apu_pull_sample(memory, &left, &right)) {
        chunk[count * 2] = left;
        chunk[count * 2 + 1] = right;
        ++count;
    }
    if (play && stream && count > 0)
        SDL_PutAudioStreamData(stream, chunk, count * 4);
}

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
    if (ends_with(path, ".nes") || ends_with(path, ".sfc") || ends_with(path, ".smc") ||
        ends_with(path, ".gba") || ends_with(path, ".md") || ends_with(path, ".zip")) {
        format_rejected(path, nullptr, 0);
        return false;
    }
    if (!gb_cartridge_read_file(path, &rom, &length, error, sizeof error)) {
        std::cerr << error << '\n';
        return false;
    }
    if (format_rejected(path, rom, length)) {
        std::free(rom);
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
    std::cout << "P pauses, H shows the controls, M mutes. Close the window to stop and save.\n";

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) && !SDL_Init(SDL_INIT_VIDEO)) {
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
    SDL_AudioSpec audio_spec{};
    audio_spec.freq = 32768;
    audio_spec.format = SDL_AUDIO_S16;
    audio_spec.channels = 2;
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, nullptr, nullptr);
    if (stream)
        SDL_ResumeAudioStreamDevice(stream);
    else
        std::cerr << "Audio device unavailable; sound stays silent. " << SDL_GetError() << '\n';

    uint8_t rgb[GB_LCD_WIDTH * GB_LCD_HEIGHT * 3] = {};
    uint8_t held = 0;
    bool running = true;
    bool cpu_alive = true;
    bool stop_noted = false;
    bool paused = false;
    bool help = false;
    bool muted = false;
    bool saw_frame = false;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                held = 0;
            else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_P) paused = !paused;
                else if (event.key.key == SDLK_H) help = !help;
                else if (event.key.key == SDLK_M) muted = !muted;
                else held = button_mask(event.key.key, true, held);
            } else if (event.type == SDL_EVENT_KEY_UP && !event.key.repeat)
                held = button_mask(event.key.key, false, held);
        }
        if (stream)
            (paused || muted) ? SDL_PauseAudioStreamDevice(stream) : SDL_ResumeAudioStreamDevice(stream);
        if (gb_joypad_set_pressed(&memory, held) && cpu.stopped)
            gb_cpu_leave_stop(&cpu);
        if (paused) {
            paint_overlay(rgb, true, true, muted);
            SDL_UpdateTexture(texture, nullptr, rgb, GB_LCD_WIDTH * 3);
            SDL_RenderTexture(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
            SDL_Delay(16);
            continue;
        }
        if (cpu.stopped) {
            if (!stop_noted) {
                std::cout << "STOP is waiting for a button.\n";
                stop_noted = true;
            }
            drain_audio(&memory, stream, false);
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
                saw_frame = true;
                paint_overlay(rgb, false, help, muted);
                SDL_UpdateTexture(texture, nullptr, rgb, GB_LCD_WIDTH * 3);
                SDL_RenderTexture(renderer, texture, nullptr, nullptr);
                SDL_RenderPresent(renderer);
                gb_ppu_acknowledge_frame(&memory);
                drain_audio(&memory, stream, !muted);
                SDL_Delay(16);
                break;
            }
        }
        if (!saw_frame)
            SDL_Delay(16);
    }
    if (battery && !gb_memory_store_save(&memory, save_path, error, sizeof error))
        std::cerr << error << '\n';
    if (stream)
        SDL_DestroyAudioStream(stream);
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
    std::cout << "Close the window to exit. Pass a ROM-only, MBC1, MBC3, or MBC5 cartridge to run it.\n";
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
