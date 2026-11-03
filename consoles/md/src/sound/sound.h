/* Shared timestamped interfaces for the Mega Drive sound chips. */
#ifndef NMD_SOUND_H
#define NMD_SOUND_H

#include <stdint.h>

#include "nmd/cart.h"

void md_ym_reset(void);
uint8_t md_ym_read(uint8_t port, uint64_t clock);
void md_ym_write(uint8_t port, uint8_t value, uint64_t clock);

typedef struct md_psg_state {
    uint16_t tone_period[3];
    uint16_t tone_counter[3];
    uint8_t attenuation[4];
    uint8_t tone_output[3];
    uint8_t noise_control;
    uint8_t noise_output;
    uint8_t noise_toggle;
    uint8_t noise_use_tone;
    uint8_t latched_channel;
    uint8_t latched_volume;
    uint16_t noise_lfsr;
    uint16_t noise_period;
    uint16_t noise_counter;
    uint64_t master_clock;
    uint64_t next_tick_clock;
    uint64_t noise_shift_count;
    uint64_t sample_count;
    int16_t sample;
} md_psg_state;

extern md_psg_state md_psg;

void md_psg_reset(nmd_region region);
void md_psg_write(uint8_t value, uint64_t clock);
void md_psg_advance_to(uint64_t master_clock);
int md_psg_take_samples(int16_t *samples, int max_samples);
double md_psg_source_rate(void);

#endif
