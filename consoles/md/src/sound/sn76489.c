/* SN76489 serial register protocol; tone and noise generation follow later. */
#include <stdint.h>

#include "sound.h"
#include "nk/fatal.h"

static uint16_t tone_period[3];
static uint8_t attenuation[4];
static uint8_t noise_control;
static uint8_t latched_channel;
static uint8_t latched_volume;
static uint64_t current_clock;

void md_psg_reset(void)
{
    unsigned i;
    for (i = 0; i < 3; i++)
        tone_period[i] = 0;
    for (i = 0; i < 4; i++)
        attenuation[i] = 0x0Fu;
    noise_control = 0;
    latched_channel = 0;
    latched_volume = 0;
    current_clock = 0;
}

void md_psg_write(uint8_t value, uint64_t clock)
{
    uint8_t channel;
    uint8_t volume;
    uint8_t data;

    if (clock < current_clock)
        nk_fatal("PSG timestamp moved backward");
    current_clock = clock;

    if (value & 0x80u) {
        channel = (uint8_t)((value >> 5) & 3u);
        volume = (uint8_t)((value >> 4) & 1u);
        data = value & 0x0Fu;
        latched_channel = channel;
        latched_volume = volume;
        if (volume) {
            attenuation[channel] = data;
        } else if (channel < 3u) {
            tone_period[channel] = (uint16_t)((tone_period[channel] & 0x03F0u) | data);
        } else {
            noise_control = data & 7u;
        }
    } else if (!latched_volume && latched_channel < 3u) {
        tone_period[latched_channel] = (uint16_t)((tone_period[latched_channel] & 0x000Fu) |
                                                  ((uint16_t)(value & 0x3Fu) << 4));
    } else if (latched_volume) {
        attenuation[latched_channel] = value & 0x0Fu;
    }
}
