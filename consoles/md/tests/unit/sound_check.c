/* SN76489 latch/data, divider, attenuation, and noise-source contracts. */
#include <assert.h>
#include <stdint.h>

#include <nmd/nmd.h>

#include "sound.h"

static const uint8_t rom[0x100] = { 0 };
static nmd_cart cart = {
    .title = "Sound check",
    .region = NMD_NTSC,
    .rom = rom,
    .rom_size = sizeof rom,
};

static void reset_psg(void)
{
    nmd_power(&cart);
    assert(md_psg.attenuation[0] == 0x0F);
    assert(md_psg.attenuation[1] == 0x0F);
    assert(md_psg.attenuation[2] == 0x0F);
    assert(md_psg.attenuation[3] == 0x0F);
}

static void test_psg_latch_data(void)
{
    reset_psg();

    md_psg_write(0x8A, 0); /* Tone 1, low four divider bits. */
    assert(md_psg.tone_period[0] == 0x00A);
    md_psg_write(0x12, 0); /* Continuation supplies divider bits 4-9. */
    assert(md_psg.tone_period[0] == 0x12A);

    md_psg_write(0xC5, 0); /* Tone 3's independent latch. */
    md_psg_write(0x03, 0);
    assert(md_psg.tone_period[2] == 0x035);

    md_psg_write(0x90, 0); /* Attenuation writes are single-byte transfers. */
    md_psg_write(0x3F, 0);
    assert(md_psg.attenuation[0] == 0);
}

static void test_psg_period_zero(void)
{
    int16_t samples[6];
    int count;

    reset_psg();
    md_psg_write(0x90, 0); /* Tone 1 full volume. */
    md_psg_write(0x80, 0); /* Zero is the shortest divider value. */
    md_psg_advance_to(6u * 240u);
    count = md_psg_take_samples(samples, 6);
    assert(count == 6);
    assert(samples[0] > 0);
    assert(samples[0] == samples[2] && samples[2] == samples[4]);
    assert(samples[1] == samples[3] && samples[3] == samples[5]);
    assert(samples[0] != samples[1]);
}

static void test_psg_noise_modes_and_tone3_clock(void)
{
    uint16_t periodic_lfsr;
    uint64_t periodic_shifts;

    reset_psg();
    md_psg_write(0xE0, 0); /* Periodic noise, fixed N/512 source. */
    assert(md_psg.noise_lfsr == 0x8000);
    md_psg_advance_to(240u * 32u * 17u);
    periodic_lfsr = md_psg.noise_lfsr;
    periodic_shifts = md_psg.noise_shift_count;
    assert(periodic_shifts == 17);

    reset_psg();
    md_psg_write(0xE4, 0); /* White noise uses the same source rate. */
    md_psg_advance_to(240u * 32u * 17u);
    assert(md_psg.noise_shift_count == periodic_shifts);
    assert(md_psg.noise_lfsr != periodic_lfsr);

    reset_psg();
    md_psg_write(0xC2, 0); /* Tone 3 divider is two. */
    md_psg_write(0x00, 0);
    md_psg_write(0xE3, 0); /* Noise clock follows Tone 3. */
    assert(md_psg.noise_use_tone);
    md_psg_advance_to(240u * 12u);
    assert(md_psg.noise_shift_count == 3);
}

static void test_psg_attenuation(void)
{
    int16_t samples[2], levels[16];
    int count;
    unsigned attenuation;

    reset_psg();
    for (attenuation = 0; attenuation < 16; attenuation++) {
        md_psg_write((uint8_t)(0x90u | attenuation), 0);
        assert(md_psg.attenuation[0] == attenuation);
    }

    md_psg_write(0x80, 0);
    md_psg_advance_to(2u * 240u);
    count = md_psg_take_samples(samples, 2);
    assert(count == 2);
    assert(samples[0] == 0 && samples[1] == 0); /* Attenuation 15 is mute. */

    for (attenuation = 0; attenuation < 16; attenuation++) {
        reset_psg();
        md_psg_write((uint8_t)(0x90u | attenuation), 0);
        md_psg_write(0x80, 0);
        md_psg_advance_to(240u);
        assert(md_psg_take_samples(&levels[attenuation], 1) == 1);
    }
    for (attenuation = 1; attenuation < 15; attenuation++)
        assert(levels[attenuation - 1] > levels[attenuation]);
    assert(levels[14] > 0 && levels[15] == 0);
}

int main(void)
{
    test_psg_latch_data();
    test_psg_period_zero();
    test_psg_noise_modes_and_tone3_clock();
    test_psg_attenuation();
    return 0;
}
