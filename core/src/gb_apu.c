#include "gb_apu.h"

/* Approximate DMG audio. The four channels, the 512 Hz sequencer, length,
 * envelope, and channel 1 sweep follow Pan Docs. What is not exact: the
 * sequencer is not tied to DIV, duty is recentered around zero, and a sample
 * is emitted every 128 T-cycles (32768 Hz) instead of a hardware DAC model. */

static const uint8_t k_duty[4] = {0x01, 0x81, 0x87, 0x7e};
static const int k_noise_div[8] = {8, 16, 32, 48, 64, 80, 96, 112};

static bool apu_range(uint16_t address)
{
    return address >= 0xff10 && address <= 0xff3f;
}

static void power_off(GbApu *apu)
{
    uint8_t wave[16];
    for (int i = 0; i < 16; ++i)
        wave[i] = apu->wave[i];
    *apu = (GbApu){0};
    for (int i = 0; i < 16; ++i)
        apu->wave[i] = wave[i];
    apu->lfsr = 0x7fff;
}

static uint32_t square_period(uint16_t freq)
{
    unsigned hz = 2048u - (freq & 0x7ffu);
    return hz * 4u;
}

static uint32_t wave_period(uint16_t freq)
{
    unsigned hz = 2048u - (freq & 0x7ffu);
    return hz * 2u;
}

static uint32_t noise_period(const GbApu *apu)
{
    unsigned shift = apu->noise_shift & 0x0fu;
    unsigned base = (unsigned)k_noise_div[apu->noise_divisor & 7];
    if (shift >= 16)
        shift = 15;
    return base << shift;
}

static void sweep_calc(GbMemory *memory, bool *overflow)
{
    GbApu *apu = &memory->apu;
    uint16_t next = apu->sweep_shadow >> apu->sweep_shift;
    if (apu->sweep_negate)
    {
        if (next > apu->sweep_shadow)
            next = 0;
        else
            next = (uint16_t)(apu->sweep_shadow - next);
    }
    else
        next = (uint16_t)(apu->sweep_shadow + next);
    *overflow = next > 2047;
    if (!*overflow && apu->sweep_shift != 0)
    {
        apu->sweep_shadow = next;
        apu->freq[0] = next;
    }
}

static void trigger(GbMemory *memory, int channel)
{
    GbApu *apu = &memory->apu;
    if (!apu->power || !apu->dac[channel])
    {
        apu->on[channel] = false;
        return;
    }
    apu->on[channel] = true;
    if (apu->length_timer[channel] == 0)
        apu->length_timer[channel] = channel == 2 ? 256 : 64;
    apu->volume[channel] = apu->env_start[channel];
    apu->env_timer[channel] = apu->env_period[channel];
    if (channel == 2)
    {
        apu->period_timer[channel] = wave_period(apu->freq[channel]);
        apu->wave_pos = 0;
    }
    else if (channel == 3)
    {
        apu->period_timer[channel] = noise_period(apu);
        apu->lfsr = 0x7fff;
    }
    else
    {
        apu->period_timer[channel] = square_period(apu->freq[channel]);
        apu->duty_pos[channel] = 0;
    }
    if (channel == 0)
    {
        apu->sweep_shadow = apu->freq[0];
        apu->sweep_timer = apu->sweep_period == 0 ? 8 : apu->sweep_period;
        apu->sweep_enabled = apu->sweep_period != 0 || apu->sweep_shift != 0;
        if (apu->sweep_shift != 0)
        {
            bool overflow = false;
            sweep_calc(memory, &overflow);
            if (overflow)
                apu->on[0] = false;
        }
    }
}

static void clock_length(GbApu *apu)
{
    for (int i = 0; i < 4; ++i)
    {
        if (!apu->length_on[i] || apu->length_timer[i] == 0)
            continue;
        apu->length_timer[i]--;
        if (apu->length_timer[i] == 0)
            apu->on[i] = false;
    }
}

static void clock_envelope(GbApu *apu)
{
    for (int i = 0; i < 4; ++i)
    {
        if (i == 2 || apu->env_period[i] == 0 || !apu->on[i])
            continue;
        if (apu->env_timer[i] > 0)
            apu->env_timer[i]--;
        if (apu->env_timer[i] != 0)
            continue;
        apu->env_timer[i] = apu->env_period[i];
        if (apu->env_add[i] && apu->volume[i] < 15)
            apu->volume[i]++;
        else if (!apu->env_add[i] && apu->volume[i] > 0)
            apu->volume[i]--;
    }
}

static void clock_sweep(GbMemory *memory)
{
    GbApu *apu = &memory->apu;
    if (!apu->sweep_enabled || apu->sweep_period == 0)
        return;
    if (apu->sweep_timer > 0)
        apu->sweep_timer--;
    if (apu->sweep_timer != 0)
        return;
    apu->sweep_timer = apu->sweep_period;
    bool overflow = false;
    sweep_calc(memory, &overflow);
    if (overflow)
        apu->on[0] = false;
}

static void sequencer(GbMemory *memory)
{
    GbApu *apu = &memory->apu;
    apu->frame_cycles = (uint16_t)(apu->frame_cycles + 4);
    if (apu->frame_cycles < 8192)
        return;
    apu->frame_cycles = (uint16_t)(apu->frame_cycles - 8192);
    switch (apu->frame_step)
    {
    case 0:
    case 4:
        clock_length(apu);
        break;
    case 2:
    case 6:
        clock_length(apu);
        clock_sweep(memory);
        break;
    case 7:
        clock_envelope(apu);
        break;
    default:
        break;
    }
    apu->frame_step = (uint8_t)((apu->frame_step + 1) & 7);
}

static void tick_channel(GbApu *apu, int channel)
{
    if (!apu->on[channel])
        return;
    if (apu->period_timer[channel] > 4)
    {
        apu->period_timer[channel] -= 4;
        return;
    }
    if (channel == 2)
    {
        apu->period_timer[channel] += wave_period(apu->freq[channel]);
        apu->wave_pos = (uint8_t)((apu->wave_pos + 1) & 31);
    }
    else if (channel == 3)
    {
        apu->period_timer[channel] += noise_period(apu);
        uint16_t bit = (uint16_t)((apu->lfsr ^ (apu->lfsr >> 1)) & 1u);
        apu->lfsr = (uint16_t)((apu->lfsr >> 1) | (bit << 14));
        if (apu->noise_short)
            apu->lfsr = (uint16_t)((apu->lfsr & (uint16_t)~0x40u) | (bit << 6));
    }
    else
    {
        apu->period_timer[channel] += square_period(apu->freq[channel]);
        apu->duty_pos[channel] = (uint8_t)((apu->duty_pos[channel] + 1) & 7);
    }
}

static int channel_level(const GbApu *apu, int channel)
{
    if (!apu->power || !apu->on[channel] || !apu->dac[channel])
        return 0;
    if (channel == 2)
    {
        uint8_t byte = apu->wave[apu->wave_pos >> 1];
        int nibble = (apu->wave_pos & 1) ? (byte & 0x0f) : (byte >> 4);
        static const int shift[4] = {4, 0, 1, 2};
        nibble >>= shift[apu->wave_level & 3];
        return nibble - 8;
    }
    int amp = 0;
    if (channel == 3)
        amp = (apu->lfsr & 1u) ? 0 : (int)apu->volume[channel];
    else if ((k_duty[apu->duty[channel] & 3] >> apu->duty_pos[channel]) & 1)
        amp = (int)apu->volume[channel];
    else
        amp = -(int)apu->volume[channel];
    return amp;
}

static void push_sample(GbApu *apu)
{
    int left = 0;
    int right = 0;
    for (int i = 0; i < 4; ++i)
    {
        int level = channel_level(apu, i);
        if (apu->nr51 & (uint8_t)(1u << i))
            right += level;
        if (apu->nr51 & (uint8_t)(1u << (i + 4)))
            left += level;
    }
    int right_vol = (apu->nr50 & 7) + 1;
    int left_vol = ((apu->nr50 >> 4) & 7) + 1;
    left *= left_vol * 64;
    right *= right_vol * 64;
    if (left > 32767) left = 32767;
    if (left < -32768) left = -32768;
    if (right > 32767) right = 32767;
    if (right < -32768) right = -32768;
    if (apu->sample_count == 1024)
        apu->sample_count--;
    unsigned index = apu->sample_write % 1024u;
    apu->sample_left[index] = (int16_t)left;
    apu->sample_right[index] = (int16_t)right;
    apu->sample_write = (index + 1u) % 1024u;
    apu->sample_count++;
}

void gb_apu_on_machine_cycle(GbMemory *memory)
{
    if (!memory)
        return;
    GbApu *apu = &memory->apu;
    if (apu->power)
    {
        sequencer(memory);
        for (int i = 0; i < 4; ++i)
            tick_channel(apu, i);
    }
    apu->sample_div++;
    if (apu->sample_div >= 32)
    {
        apu->sample_div = 0;
        push_sample(apu);
    }
}

bool gb_apu_pull_sample(GbMemory *memory, int16_t *left, int16_t *right)
{
    if (!memory || memory->apu.sample_count == 0)
        return false;
    unsigned index = (memory->apu.sample_write + 1024u - memory->apu.sample_count) % 1024u;
    if (left)
        *left = memory->apu.sample_left[index];
    if (right)
        *right = memory->apu.sample_right[index];
    memory->apu.sample_count--;
    return true;
}

static int square_index(uint16_t address)
{
    if (address <= 0xff14) return 0;
    if (address >= 0xff16 && address <= 0xff19) return 1;
    return -1;
}

uint8_t gb_apu_read(const GbMemory *memory, uint16_t address)
{
    const GbApu *apu = &memory->apu;
    if (address >= 0xff30 && address <= 0xff3f)
        return apu->wave[address - 0xff30];
    if (address == 0xff26)
    {
        uint8_t value = 0x70;
        if (apu->power)
            value = (uint8_t)(value | 0x80);
        for (int i = 0; i < 4; ++i)
            if (apu->on[i])
                value = (uint8_t)(value | (1u << i));
        return value;
    }
    if (!apu->power)
        return 0;
    if (address == 0xff24) return apu->nr50;
    if (address == 0xff25) return apu->nr51;
    if (address == 0xff10)
        return (uint8_t)(0x80 | (apu->sweep_period << 4) | (apu->sweep_negate ? 0x08 : 0) | apu->sweep_shift);
    if (address == 0xff11 || address == 0xff16)
    {
        int channel = address == 0xff11 ? 0 : 1;
        return (uint8_t)(0x3f | (apu->duty[channel] << 6));
    }
    if (address == 0xff12 || address == 0xff17 || address == 0xff21)
    {
        int channel = address == 0xff12 ? 0 : address == 0xff17 ? 1 : 3;
        return (uint8_t)((apu->env_start[channel] << 4) | (apu->env_add[channel] ? 0x08 : 0) | apu->env_period[channel]);
    }
    if (address == 0xff1a)
        return (uint8_t)(0x7f | (apu->dac[2] ? 0x80 : 0));
    if (address == 0xff1c)
        return (uint8_t)(0x9f | ((apu->wave_level & 3) << 5));
    if (address == 0xff20)
        return 0xff;
    if (address == 0xff22)
        return (uint8_t)((apu->noise_shift << 4) | (apu->noise_short ? 0x08 : 0) | apu->noise_divisor);
    return 0xff;
}

static void write_square(GbMemory *memory, int channel, uint16_t address, uint8_t value)
{
    GbApu *apu = &memory->apu;
    unsigned offset = channel == 0 ? 0xff10u : 0xff15u;
    unsigned reg = address - offset;
    if (channel == 1 && reg == 0)
        return;
    if (reg == 0 && channel == 0)
    {
        apu->sweep_period = (uint8_t)((value >> 4) & 7);
        apu->sweep_negate = (value & 0x08) != 0;
        apu->sweep_shift = (uint8_t)(value & 7);
        return;
    }
    if (reg == 1)
    {
        apu->duty[channel] = (uint8_t)(value >> 6);
        apu->length_timer[channel] = (uint16_t)(64 - (value & 0x3f));
        return;
    }
    if (reg == 2)
    {
        apu->env_start[channel] = (uint8_t)(value >> 4);
        apu->env_add[channel] = (value & 0x08) != 0;
        apu->env_period[channel] = (uint8_t)(value & 7);
        apu->dac[channel] = (value & 0xf8) != 0;
        if (!apu->dac[channel])
            apu->on[channel] = false;
        return;
    }
    if (reg == 3)
    {
        apu->freq[channel] = (uint16_t)((apu->freq[channel] & 0x700) | value);
        return;
    }
    apu->freq[channel] = (uint16_t)((apu->freq[channel] & 0xff) | ((value & 7) << 8));
    apu->length_on[channel] = (value & 0x40) != 0;
    if (value & 0x80)
        trigger(memory, channel);
}

void gb_apu_write(GbMemory *memory, uint16_t address, uint8_t value)
{
    if (!memory || !apu_range(address))
        return;
    GbApu *apu = &memory->apu;
    if (address == 0xff26)
    {
        bool power = (value & 0x80) != 0;
        if (!power)
            power_off(apu);
        else if (!apu->power)
        {
            apu->power = true;
            apu->lfsr = 0x7fff;
            apu->frame_step = 0;
            apu->frame_cycles = 0;
        }
        return;
    }
    if (address >= 0xff30)
    {
        apu->wave[address - 0xff30] = value;
        return;
    }
    if (!apu->power)
        return;
    if (address == 0xff24)
    {
        apu->nr50 = value;
        return;
    }
    if (address == 0xff25)
    {
        apu->nr51 = value;
        return;
    }
    int square = square_index(address);
    if (square >= 0)
    {
        write_square(memory, square, address, value);
        return;
    }
    if (address == 0xff1a)
    {
        apu->dac[2] = (value & 0x80) != 0;
        if (!apu->dac[2])
            apu->on[2] = false;
        return;
    }
    if (address == 0xff1b)
    {
        apu->length_timer[2] = (uint16_t)(256 - value);
        return;
    }
    if (address == 0xff1c)
    {
        apu->wave_level = (uint8_t)((value >> 5) & 3);
        return;
    }
    if (address == 0xff1d)
    {
        apu->freq[2] = (uint16_t)((apu->freq[2] & 0x700) | value);
        return;
    }
    if (address == 0xff1e)
    {
        apu->freq[2] = (uint16_t)((apu->freq[2] & 0xff) | ((value & 7) << 8));
        apu->length_on[2] = (value & 0x40) != 0;
        if (value & 0x80)
            trigger(memory, 2);
        return;
    }
    if (address == 0xff20)
    {
        apu->length_timer[3] = (uint16_t)(64 - (value & 0x3f));
        return;
    }
    if (address == 0xff21)
    {
        apu->env_start[3] = (uint8_t)(value >> 4);
        apu->env_add[3] = (value & 0x08) != 0;
        apu->env_period[3] = (uint8_t)(value & 7);
        apu->dac[3] = (value & 0xf8) != 0;
        if (!apu->dac[3])
            apu->on[3] = false;
        return;
    }
    if (address == 0xff22)
    {
        apu->noise_shift = (uint8_t)(value >> 4);
        apu->noise_short = (value & 0x08) != 0;
        apu->noise_divisor = (uint8_t)(value & 7);
        return;
    }
    if (address == 0xff23)
    {
        apu->length_on[3] = (value & 0x40) != 0;
        if (value & 0x80)
            trigger(memory, 3);
    }
}
