#include "gb_ppu.h"

#include "gb_cpu.h"

enum {
    DOTS_PER_LINE = 456,
    MODE2_DOTS = 80,
    MODE3_BASE_DOTS = 172,
    LINES_PER_FRAME = 154
};

static unsigned mode3_dots(const GbPpu *ppu)
{
    unsigned dots = MODE3_BASE_DOTS + ppu->mode3_stall_dots;
    /* Leave at least one HBlank cycle so a line can still end. */
    if (dots > DOTS_PER_LINE - MODE2_DOTS - 4)
        dots = DOTS_PER_LINE - MODE2_DOTS - 4;
    return dots;
}

int gb_ppu_mode(const GbMemory *memory)
{
    const GbPpu *ppu = &memory->ppu;
    if (!ppu->lcd_on)
        return 0;
    if (ppu->ly >= GB_LCD_HEIGHT)
        return 1;
    if (ppu->dot < MODE2_DOTS)
        return 2;
    if (ppu->dot < MODE2_DOTS + mode3_dots(ppu))
        return 3;
    return 0;
}

static uint8_t shade_of(uint8_t palette, uint8_t color)
{
    return (uint8_t)((palette >> (color * 2)) & 0x03);
}

static uint8_t vram_at(const GbMemory *memory, uint16_t address)
{
    return memory->vram[address - 0x8000];
}

static uint8_t tile_color(const GbMemory *memory, uint16_t tile, unsigned row, unsigned column)
{
    uint16_t base = (uint16_t)(0x8000 + tile * 16u + row * 2u);
    uint8_t low = vram_at(memory, base);
    uint8_t high = vram_at(memory, (uint16_t)(base + 1));
    unsigned bit = 7u - column;
    uint8_t color = (uint8_t)(((low >> bit) & 1u) | (((high >> bit) & 1u) << 1));
    return color;
}

static uint16_t bg_tile_id(uint8_t lcdc, uint8_t index)
{
    if (lcdc & 0x10)
        return index;
    /* 0x8800 addressing: index 0 lives at tile 256 (address 0x9000). */
    return (uint16_t)(256 + (int)(int8_t)index);
}

static void render_scanline(GbMemory *memory)
{
    GbPpu *ppu = &memory->ppu;
    uint8_t lcdc = ppu->lcdc;
    unsigned ly = ppu->ly;
    if (ly >= GB_LCD_HEIGHT)
        return;

    uint8_t raw[GB_LCD_WIDTH];
    uint8_t out[GB_LCD_WIDTH];
    bool window_drawn = false;
    for (unsigned x = 0; x < GB_LCD_WIDTH; ++x)
    {
        uint8_t color = 0;
        if (lcdc & 0x01)
        {
            bool window = (lcdc & 0x20) && ly >= ppu->wy && (int)x + 7 >= (int)ppu->wx;
            unsigned pixel_x;
            unsigned pixel_y;
            uint16_t map;
            if (window)
            {
                window_drawn = true;
                pixel_x = (unsigned)((int)x + 7 - (int)ppu->wx);
                pixel_y = ppu->window_line;
                map = (lcdc & 0x40) ? 0x9c00u : 0x9800u;
            }
            else
            {
                pixel_x = (unsigned)((ppu->scx + x) & 0xff);
                pixel_y = (unsigned)((ppu->scy + ly) & 0xff);
                map = (lcdc & 0x08) ? 0x9c00u : 0x9800u;
            }
            unsigned tile_x = pixel_x / 8u;
            unsigned tile_y = pixel_y / 8u;
            uint16_t map_addr = (uint16_t)(map + tile_y * 32u + tile_x);
            uint8_t index = vram_at(memory, map_addr);
            color = tile_color(memory, bg_tile_id(lcdc, index), pixel_y & 7u, pixel_x & 7u);
        }
        raw[x] = color;
        out[x] = shade_of(ppu->bgp, color);
    }
    if (window_drawn)
        ppu->window_line++;

    if (lcdc & 0x02)
    {
        unsigned height = (lcdc & 0x04) ? 16u : 8u;
        unsigned chosen[10];
        unsigned count = 0;
        for (unsigned i = 0; i < 40 && count < 10; ++i)
        {
            uint8_t sprite_y = memory->oam[i * 4u];
            int top = (int)sprite_y - 16;
            if ((int)ly >= top && (unsigned)((int)ly - top) < height)
                chosen[count++] = i;
        }
        /* Draw low priority first so a smaller X, then an earlier OAM slot, wins. */
        for (unsigned a = 0; a < count; ++a)
        {
            for (unsigned b = a + 1; b < count; ++b)
            {
                unsigned ia = chosen[a];
                unsigned ib = chosen[b];
                uint8_t xa = memory->oam[ia * 4u + 1];
                uint8_t xb = memory->oam[ib * 4u + 1];
                bool a_is_lower = xa > xb || (xa == xb && ia > ib);
                if (!a_is_lower)
                {
                    unsigned swap = chosen[a];
                    chosen[a] = chosen[b];
                    chosen[b] = swap;
                }
            }
        }
        for (unsigned n = 0; n < count; ++n)
        {
            unsigned i = chosen[n];
            uint8_t sprite_y = memory->oam[i * 4u];
            uint8_t sprite_x = memory->oam[i * 4u + 1];
            uint8_t tile = memory->oam[i * 4u + 2];
            uint8_t attr = memory->oam[i * 4u + 3];
            int top = (int)sprite_y - 16;
            int left = (int)sprite_x - 8;
            unsigned row = (unsigned)((int)ly - top);
            if (attr & 0x40)
                row = height - 1u - row;
            uint16_t tile_id = tile;
            if (height == 16u)
            {
                tile_id = (uint16_t)(tile & 0xfeu);
                if (row >= 8u)
                {
                    tile_id++;
                    row -= 8u;
                }
            }
            uint8_t palette = (attr & 0x10) ? ppu->obp1 : ppu->obp0;
            bool behind = (attr & 0x80) != 0 && (lcdc & 0x01) != 0;
            for (unsigned column = 0; column < 8u; ++column)
            {
                int screen_x = left + (int)column;
                if (screen_x < 0 || screen_x >= GB_LCD_WIDTH)
                    continue;
                unsigned source_column = (attr & 0x20) ? 7u - column : column;
                uint8_t color = tile_color(memory, tile_id, row, source_column);
                if (color == 0)
                    continue;
                if (behind && raw[screen_x] != 0)
                    continue;
                out[screen_x] = shade_of(palette, color);
            }
        }
    }

    for (unsigned x = 0; x < GB_LCD_WIDTH; ++x)
        ppu->pixels[ly * GB_LCD_WIDTH + x] = out[x];
}

static uint8_t visible_ly(const GbPpu *ppu)
{
    if (!ppu->lcd_on)
        return 0;
    /* Line 153 drives LY as 153 for the first 4 dots, then as 0. */
    if (ppu->ly == 153 && ppu->dot >= 4)
        return 0;
    return ppu->ly;
}

static int floor_tile(int map_x)
{
    if (map_x >= 0)
        return map_x / 8;
    return -(((-map_x) + 7) / 8);
}

static unsigned mode3_penalty(const GbMemory *memory)
{
    const GbPpu *ppu = &memory->ppu;
    unsigned extra = (unsigned)(ppu->scx & 7u);
    unsigned ly = ppu->ly;
    bool window = (ppu->lcdc & 0x20) != 0 && ly >= ppu->wy && ppu->wx <= 166;
    if (window)
        extra += 6;
    if ((ppu->lcdc & 0x02) == 0 || ly >= GB_LCD_HEIGHT)
        return extra;

    unsigned height = (ppu->lcdc & 0x04) ? 16u : 8u;
    unsigned order[10];
    unsigned count = 0;
    for (unsigned i = 0; i < 40 && count < 10; ++i)
    {
        int top = (int)memory->oam[i * 4u] - 16;
        if ((int)ly >= top && (unsigned)((int)ly - top) < height)
            order[count++] = i;
    }
    for (unsigned a = 0; a < count; ++a)
    {
        for (unsigned b = a + 1; b < count; ++b)
        {
            uint8_t xa = memory->oam[order[a] * 4u + 1];
            uint8_t xb = memory->oam[order[b] * 4u + 1];
            if (xb < xa || (xb == xa && order[b] < order[a]))
            {
                unsigned swap = order[a];
                order[a] = order[b];
                order[b] = swap;
            }
        }
    }
    int seen_tile[10];
    int seen_kind[10];
    unsigned seen = 0;
    for (unsigned n = 0; n < count; ++n)
    {
        uint8_t sprite_x = memory->oam[order[n] * 4u + 1];
        if (sprite_x == 0)
        {
            extra += 11;
            continue;
        }
        int pixel = (int)sprite_x - 8;
        int window_x = (int)ppu->wx - 7;
        bool in_window = window && pixel >= window_x;
        int map_x = in_window ? pixel - window_x : (int)ppu->scx + pixel;
        int tile = floor_tile(map_x);
        int pos = map_x - tile * 8;
        int kind = in_window ? 1 : 0;
        bool fresh = true;
        for (unsigned s = 0; s < seen; ++s)
        {
            if (seen_tile[s] == tile && seen_kind[s] == kind)
                fresh = false;
        }
        if (fresh)
        {
            int penalty = 7 - pos - 2;
            if (penalty > 0)
                extra += (unsigned)penalty;
            if (seen < 10)
            {
                seen_tile[seen] = tile;
                seen_kind[seen] = kind;
                seen++;
            }
        }
        extra += 6;
    }
    return extra;
}

static bool stat_sources(const GbMemory *memory)
{
    const GbPpu *ppu = &memory->ppu;
    if (!ppu->lcd_on)
        return false;
    int mode = gb_ppu_mode(memory);
    bool lyc = visible_ly(ppu) == ppu->lyc;
    return (mode == 0 && (ppu->stat & 0x08)) || (mode == 1 && (ppu->stat & 0x10)) ||
           (mode == 2 && (ppu->stat & 0x20)) || (lyc && (ppu->stat & 0x40));
}

static void refresh_stat(GbMemory *memory)
{
    bool now = stat_sources(memory);
    if (now && !memory->ppu.stat_line)
        gb_cpu_request_interrupt(memory, GB_INT_STAT);
    memory->ppu.stat_line = now;
}

static void next_line(GbMemory *memory)
{
    GbPpu *ppu = &memory->ppu;
    ppu->dot = 0;
    ppu->mode3_latched = false;
    ppu->mode3_stall_dots = 0;
    ppu->ly++;
    if (ppu->ly == LINES_PER_FRAME)
    {
        ppu->ly = 0;
        ppu->window_line = 0;
    }
    if (ppu->ly == GB_LCD_HEIGHT)
        gb_cpu_request_interrupt(memory, GB_INT_VBLANK);
    if (ppu->ly == GB_LCD_HEIGHT)
        ppu->frame_ready = true;
}

static void step_lcd(GbMemory *memory)
{
    GbPpu *ppu = &memory->ppu;
    if (!ppu->lcd_on)
        return;
    int before = gb_ppu_mode(memory);
    ppu->last_mode = (uint8_t)before;
    ppu->dot = (uint16_t)(ppu->dot + 4);
    if (ppu->dot >= DOTS_PER_LINE)
        next_line(memory);
    if (ppu->lcd_on && ppu->ly < GB_LCD_HEIGHT && ppu->dot >= MODE2_DOTS && !ppu->mode3_latched)
    {
        ppu->mode3_stall_dots = (uint16_t)mode3_penalty(memory);
        ppu->mode3_latched = true;
    }
    /* Render on the cycle that lands in HBlank, so the completed line is
     * already in the buffer when a register read first sees mode 0.
     * The STAT line is checked after the line change. */
    if (before == 3 && gb_ppu_mode(memory) == 0)
        render_scanline(memory);
    refresh_stat(memory);
}

static void step_dma(GbMemory *memory)
{
    GbPpu *ppu = &memory->ppu;
    if (!ppu->dma_active)
        return;
    /* call_timing reads the CALL target on the cycle that copies the last
     * byte, and that read must still see OAM as locked. The transfer then
     * stays active for one more machine cycle before the CPU is allowed in. */
    if (ppu->dma_wait)
    {
        ppu->dma_wait = false;
        return;
    }
    if (ppu->dma_finish)
    {
        ppu->dma_active = false;
        ppu->dma_finish = false;
        return;
    }
    uint16_t source = (uint16_t)(((uint16_t)ppu->dma_page << 8) | ppu->dma_offset);
    memory->oam[ppu->dma_offset] = gb_memory_dma_peek(memory, source);
    ppu->dma_offset++;
    if (ppu->dma_offset == 0xa0)
        ppu->dma_finish = true;
}

void gb_ppu_on_machine_cycle(GbMemory *memory)
{
    if (!memory)
        return;
    step_dma(memory);
    step_lcd(memory);
}

void gb_ppu_apply_dmg_post_boot(GbMemory *memory)
{
    GbPpu *ppu = &memory->ppu;
    ppu->lcdc = 0x91;
    ppu->bgp = 0xfc;
    ppu->obp0 = 0xff;
    ppu->obp1 = 0xff;
    ppu->lcd_on = true;
    ppu->ly = 0;
    ppu->dot = 0;
    ppu->last_mode = 2;
    ppu->window_line = 0;
    ppu->frame_ready = false;
}

uint8_t gb_ppu_read(const GbMemory *memory, uint16_t address)
{
    const GbPpu *ppu = &memory->ppu;
    switch (address)
    {
    case 0xff40:
        return ppu->lcdc;
    case 0xff41:
    {
        uint8_t value = (uint8_t)((ppu->stat & 0x78) | 0x80);
        value = (uint8_t)(value | (gb_ppu_mode(memory) & 0x03));
        if (visible_ly(ppu) == ppu->lyc)
            value = (uint8_t)(value | 0x04);
        return value;
    }
    case 0xff42:
        return ppu->scy;
    case 0xff43:
        return ppu->scx;
    case 0xff44:
        return visible_ly(ppu);
    case 0xff45:
        return ppu->lyc;
    case 0xff46:
        return ppu->dma;
    case 0xff47:
        return ppu->bgp;
    case 0xff48:
        return ppu->obp0;
    case 0xff49:
        return ppu->obp1;
    case 0xff4a:
        return ppu->wy;
    default:
        return ppu->wx;
    }
}

static void lcd_off(GbPpu *ppu)
{
    ppu->lcd_on = false;
    ppu->ly = 0;
    ppu->dot = 0;
    ppu->last_mode = 0;
    ppu->window_line = 0;
    ppu->mode3_latched = false;
    ppu->mode3_stall_dots = 0;
    ppu->stat_line = false;
}

void gb_ppu_write(GbMemory *memory, uint16_t address, uint8_t value)
{
    GbPpu *ppu = &memory->ppu;
    switch (address)
    {
    case 0xff40:
        if ((ppu->lcdc & 0x80) && (value & 0x80) == 0)
            lcd_off(ppu);
        else if ((ppu->lcdc & 0x80) == 0 && (value & 0x80))
        {
            /* The first line starts at LY 0 in mode 2. There is no extra
             * blanking delay before that mode; the STAT line is recomputed. */
            ppu->lcd_on = true;
            ppu->ly = 0;
            ppu->dot = 0;
            ppu->last_mode = 2;
            ppu->window_line = 0;
            ppu->mode3_latched = false;
            ppu->mode3_stall_dots = 0;
            ppu->stat_line = false;
        }
        ppu->lcdc = value;
        refresh_stat(memory);
        break;
    case 0xff41:
        ppu->stat = (uint8_t)(value & 0x78);
        refresh_stat(memory);
        break;
    case 0xff42:
        ppu->scy = value;
        break;
    case 0xff43:
        ppu->scx = value;
        break;
    case 0xff44:
        break;
    case 0xff45:
        ppu->lyc = value;
        refresh_stat(memory);
        break;
    case 0xff46:
    ppu->dma = value;
    ppu->dma_page = value;
    ppu->dma_offset = 0;
    ppu->dma_active = true;
    ppu->dma_wait = true;
    ppu->dma_finish = false;
        break;
    case 0xff47:
        ppu->bgp = value;
        break;
    case 0xff48:
        ppu->obp0 = value;
        break;
    case 0xff49:
        ppu->obp1 = value;
        break;
    case 0xff4a:
        ppu->wy = value;
        break;
    default:
        ppu->wx = value;
        break;
    }
}

bool gb_ppu_cpu_can_access(const GbMemory *memory, uint16_t address)
{
    int mode = gb_ppu_mode(memory);
    if (address >= 0x8000 && address <= 0x9fff)
        return mode != 3;
    if (address >= 0xfe00 && address <= 0xfe9f)
        return mode != 2 && mode != 3 && !memory->ppu.dma_active;
    return true;
}

bool gb_ppu_dma_blocks_cpu(const GbMemory *memory, uint16_t address)
{
    (void)memory;
    (void)address;
    /* OAM itself is locked by gb_ppu_cpu_can_access. Echo RAM, ROM, and work
     * RAM stay readable: call_timing fetches a CALL from $FDFE during DMA. */
    return false;
}

const uint8_t *gb_ppu_pixels(const GbMemory *memory)
{
    return memory->ppu.pixels;
}

bool gb_ppu_frame_ready(const GbMemory *memory)
{
    return memory->ppu.frame_ready;
}

void gb_ppu_acknowledge_frame(GbMemory *memory)
{
    memory->ppu.frame_ready = false;
}
