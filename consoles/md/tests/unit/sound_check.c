/* YM2612 ports, timers, DAC, and SN76489 sound contracts. */
#include <assert.h>
#include <limits.h>
#include <stdint.h>

#include <nmd/nmd.h>

#include "md_internal.h"
#include "sound.h"

static const uint8_t rom[0x100] = { 0 };
static nmd_cart cart = {
    .title = "Sound check",
    .region = NMD_NTSC,
    .rom = rom,
    .rom_size = sizeof rom,
};

static void reset_region(nmd_region region)
{
    cart.region = region;
    nmd_power(&cart);
    assert(md_psg.attenuation[0] == 0x0F);
    assert(md_psg.attenuation[1] == 0x0F);
    assert(md_psg.attenuation[2] == 0x0F);
    assert(md_psg.attenuation[3] == 0x0F);
}

static void reset_psg(void)
{
    reset_region(NMD_NTSC);
}

static void write_ym_register(unsigned bank, uint8_t address, uint8_t value,
                              uint64_t clock)
{
    uint8_t base = (uint8_t)(bank ? 2u : 0u);
    md_ym_write(base, address, clock);
    md_ym_write((uint8_t)(base + 1u), value, clock);
}

static uint64_t clocks_for_microseconds(uint64_t microseconds, nmd_region region)
{
    uint64_t master_clock_hz = region == NMD_PAL ? 53203395ull : 53693175ull;
    uint64_t numerator = microseconds * master_clock_hz;
    return (numerator + 999999ull) / 1000000ull;
}

static void test_ym_ports_busy(void)
{
    uint64_t data_write_clock = 84;
    uint64_t busy_start = data_write_clock + 42u;
    uint64_t busy_end = busy_start + 32u * 42u;

    reset_psg();
    assert(md_ym_read(0, 0) == 0);

    md_ym_write(0, 0x30, 0);
    assert((md_ym_read(0, 0) & 0x80u) == 0); /* Address writes stay ready. */
    md_ym_write(1, 0xA5, 0);
    assert(md_ym.registers[0][0x30] == 0xA5);

    md_ym_write(2, 0x31, 56);
    md_ym_write(3, 0xB6, data_write_clock);
    assert(md_ym.address_latch[0] == 0x30);
    assert(md_ym.address_latch[1] == 0x31);
    assert(md_ym.registers[1][0x31] == 0xB6);
    assert((md_ym_read(0, busy_start - 1u) & 0x80u) == 0);
    assert(md_ym_read(0, busy_start) & 0x80u);
    assert(md_ym_read(1, busy_end - 1u) & 0x80u);
    assert((md_ym_read(0, busy_end) & 0x80u) == 0);
}

static void test_ym_timer_a(void)
{
    uint64_t period = clocks_for_microseconds(18, NMD_NTSC);
    uint64_t irq_count = md_irq_taken;

    reset_psg();
    write_ym_register(0, 0x24, 0xFF, 0);
    write_ym_register(0, 0x25, 0x03, 0); /* Timer A = 1023. */
    write_ym_register(0, 0x27, 0x05, 0); /* Load and expose overflow A. */

    md_advance_to(period - 1u);
    assert((md_ym_read(0, period - 1u) & 0x01u) == 0);
    md_advance_to(period);
    assert(md_ym_read(0, period) & 0x01u);
    assert(md_ym_read(0, period) & 0x01u); /* Reading status does not clear it. */
    assert(md_irq_taken == irq_count); /* The Genesis does not route YM IRQ to Z80. */

    write_ym_register(0, 0x27, 0x15, period); /* Write-one reset, keep Timer A running. */
    assert((md_ym_read(0, period) & 0x01u) == 0);
}

static void test_ym_timer_pal_period(void)
{
    uint64_t period = clocks_for_microseconds(18, NMD_PAL);

    reset_region(NMD_PAL);
    write_ym_register(0, 0x24, 0xFF, 0);
    write_ym_register(0, 0x25, 0x03, 0);
    write_ym_register(0, 0x27, 0x05, 0);

    md_advance_to(period - 1u);
    assert((md_ym_read(0, period - 1u) & 0x01u) == 0);
    md_advance_to(period);
    assert(md_ym_read(0, period) & 0x01u);
    reset_psg();
}

static void test_ym_timer_b_status(void)
{
    uint64_t period = clocks_for_microseconds(576, NMD_NTSC);

    reset_psg();
    write_ym_register(0, 0x26, 0xFE, 0); /* Two Timer B units. */
    write_ym_register(0, 0x27, 0x0A, 0); /* Load and expose overflow B. */

    md_advance_to(period - 1u);
    assert((md_ym_read(0, period - 1u) & 0x02u) == 0);
    md_advance_to(period);
    assert(md_ym_read(0, period) & 0x02u);
    assert(md_ym_read(0, period) & 0x02u);

    write_ym_register(0, 0x27, 0x2A, period); /* Reset B without stopping it. */
    assert((md_ym_read(0, period) & 0x02u) == 0);
}

static void test_ym_dac_samples(void)
{
    int16_t samples[2];

    reset_psg();
    write_ym_register(0, 0x2B, 0x80, 0);
    write_ym_register(0, 0x2A, 0xFF, 0);
    md_ym_advance_to(1);
    assert(md_ym_take_samples(samples, 1) == 1);
    assert(samples[0] == 8128);

    write_ym_register(0, 0x2A, 0x00, 1);
    md_ym_advance_to(1009);
    assert(md_ym_take_samples(samples, 1) == 1);
    assert(samples[0] == -8192);
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
    test_ym_ports_busy();
    test_ym_timer_a();
    test_ym_timer_pal_period();
    test_ym_timer_b_status();
    test_ym_dac_samples();
    test_psg_latch_data();
    test_psg_period_zero();
    test_psg_noise_modes_and_tone3_clock();
    test_psg_attenuation();
    return 0;
}
