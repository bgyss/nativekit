/* SN76489 register latch, tone/noise dividers, and a bounded mono sample stream. */
#include <stdint.h>
#include <string.h>

#include "sound.h"
#include "nk/fatal.h"

#define PSG_MASTER_CLOCKS_PER_SAMPLE 240u
#define PSG_SAMPLE_CAPACITY 65536u

md_psg_state md_psg;

static int16_t sample_ring[PSG_SAMPLE_CAPACITY];
static uint64_t sample_read;
static uint64_t sample_write;
static double psg_source_rate;

static const int16_t attenuation_level[16] = {
    2340, 1859, 1477, 1173, 932, 740, 588, 467,
    371, 294, 234, 186, 148, 117, 93, 0
};

void md_psg_reset(nmd_region region)
{
    uint64_t master_clock_hz;

    memset(&md_psg, 0, sizeof md_psg);
    memset(sample_ring, 0, sizeof sample_ring);
    sample_read = 0;
    sample_write = 0;
    memset(md_psg.attenuation, 0x0F, sizeof md_psg.attenuation);
    md_psg.noise_lfsr = 0x8000u;
    md_psg.next_tick_clock = 0;
    master_clock_hz = region == NMD_PAL ? 53203395ull : 53693175ull;
    psg_source_rate = (double)master_clock_hz / PSG_MASTER_CLOCKS_PER_SAMPLE;
}

double md_psg_source_rate(void)
{
    return psg_source_rate;
}

static void noise_shift(void)
{
    uint16_t old = md_psg.noise_lfsr;
    uint16_t feedback = old & 1u;

    md_psg.noise_output = (uint8_t)feedback;
    if (md_psg.noise_control & 0x04u)
        feedback ^= (old >> 3) & 1u;
    md_psg.noise_lfsr = (uint16_t)((old >> 1) | (feedback << 15));
    if (!md_psg.noise_lfsr)
        md_psg.noise_lfsr = 0x8000u; /* The feedback network escapes the all-zero state. */
    md_psg.noise_shift_count++;
}

static void noise_tick(void)
{
    if (md_psg.noise_counter)
        md_psg.noise_counter--;
    if (md_psg.noise_counter)
        return;

    md_psg.noise_counter = md_psg.noise_period;
    md_psg.noise_toggle ^= 1u;
    if (md_psg.noise_toggle)
        noise_shift();
}

static int16_t current_sample(void)
{
    int32_t sum = 0;
    unsigned channel;

    for (channel = 0; channel < 3; channel++) {
        if (md_psg.tone_output[channel])
            sum += attenuation_level[md_psg.attenuation[channel] & 0x0Fu];
    }
    if (md_psg.noise_output)
        sum += attenuation_level[md_psg.attenuation[3] & 0x0Fu];
    return (int16_t)sum;
}

static void sample_push(int16_t sample)
{
    if (sample_write - sample_read >= PSG_SAMPLE_CAPACITY)
        nk_fatal("PSG sample stream overflow before audio drain");
    sample_ring[sample_write % PSG_SAMPLE_CAPACITY] = sample;
    sample_write++;
}

static void psg_tick(void)
{
    int tone3_rising = 0;
    unsigned channel;

    for (channel = 0; channel < 3; channel++) {
        uint8_t old_output = md_psg.tone_output[channel];
        if (md_psg.tone_counter[channel])
            md_psg.tone_counter[channel]--;
        if (!md_psg.tone_counter[channel]) {
            md_psg.tone_counter[channel] = md_psg.tone_period[channel];
            md_psg.tone_output[channel] ^= 1u;
        }
        if (channel == 2u && !old_output && md_psg.tone_output[channel])
            tone3_rising = 1;
    }
    if (md_psg.noise_use_tone) {
        if (tone3_rising)
            noise_shift();
    } else {
        noise_tick();
    }
    md_psg.sample = current_sample();
    sample_push(md_psg.sample);
    md_psg.sample_count++;
}

void md_psg_advance_to(uint64_t master_clock)
{
    if (master_clock < md_psg.master_clock)
        nk_fatal("PSG timestamp moved backward");
    /* The PSG tick at a write's clock is observed after that bus write. */
    while (md_psg.next_tick_clock < master_clock) {
        psg_tick();
        if (md_psg.next_tick_clock > UINT64_MAX - PSG_MASTER_CLOCKS_PER_SAMPLE)
            nk_fatal("PSG sample clock overflow");
        md_psg.next_tick_clock += PSG_MASTER_CLOCKS_PER_SAMPLE;
    }
    md_psg.master_clock = master_clock;
}

int md_psg_take_samples(int16_t *samples, int max_samples)
{
    int count = 0;

    if (max_samples < 0 || (max_samples && !samples))
        nk_fatal("invalid PSG sample drain buffer");
    while (sample_read != sample_write && count < max_samples) {
        samples[count++] = sample_ring[sample_read % PSG_SAMPLE_CAPACITY];
        sample_read++;
    }
    return count;
}

void md_psg_write(uint8_t value, uint64_t clock)
{
    uint8_t channel;
    uint8_t volume;
    uint8_t data;

    md_psg_advance_to(clock);
    if (value & 0x80u) {
        channel = (uint8_t)((value >> 5) & 3u);
        volume = (uint8_t)((value >> 4) & 1u);
        data = value & 0x0Fu;
        md_psg.latched_channel = channel;
        md_psg.latched_volume = volume;
        if (volume) {
            md_psg.attenuation[channel] = data;
        } else if (channel < 3u) {
            md_psg.tone_period[channel] = (uint16_t)((md_psg.tone_period[channel] & 0x03F0u) |
                                                     data);
        } else {
            md_psg.noise_control = data & 7u;
            md_psg.noise_use_tone = (uint8_t)((data & 3u) == 3u);
            md_psg.noise_period = md_psg.noise_use_tone ? md_psg.tone_period[2] :
                (uint16_t)(16u << (md_psg.noise_control & 3u));
            md_psg.noise_lfsr = 0x8000u;
        }
    } else if (md_psg.latched_volume) {
        md_psg.attenuation[md_psg.latched_channel] = value & 0x0Fu;
    } else if (md_psg.latched_channel < 3u) {
        channel = md_psg.latched_channel;
        md_psg.tone_period[channel] = (uint16_t)((md_psg.tone_period[channel] & 0x000Fu) |
                                                 ((uint16_t)(value & 0x3Fu) << 4));
    } else {
        md_psg.noise_control = value & 7u;
        md_psg.noise_use_tone = (uint8_t)((md_psg.noise_control & 3u) == 3u);
        md_psg.noise_period = md_psg.noise_use_tone ? md_psg.tone_period[2] :
            (uint16_t)(16u << (md_psg.noise_control & 3u));
        md_psg.noise_lfsr = 0x8000u;
    }
}
