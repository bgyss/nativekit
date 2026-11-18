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

static void test_ym_timer_b_integral_reload_deadline(void)
{
    const uint64_t expected_deadline = 19329543u;
    unsigned reload;

    reset_psg();
    write_ym_register(0, 0x26, 131, 0); /* Ten 36 ms Timer B periods are integral. */
    write_ym_register(0, 0x27, 0x0A, 0); /* Run Timer B and expose overflow. */

    for (reload = 0; reload < 9u; reload++)
        md_advance_to(md_ym_next_event());

    assert(md_ym_next_event() == expected_deadline);
    md_advance_to(expected_deadline - 1u);
    write_ym_register(0, 0x27, 0x2A, expected_deadline - 1u); /* Clear sticky flag. */
    assert((md_ym_read(0, expected_deadline - 1u) & 0x02u) == 0);
    md_advance_to(expected_deadline);
    assert(md_ym_read(0, expected_deadline) & 0x02u);
}

static void test_ym_dac_samples(void)
{
    int16_t samples[2];

    reset_psg();
    write_ym_register(0, 0x2B, 0x80, 0);
    write_ym_register(0, 0x2A, 0xFF, 0);
    md_ym_advance_to(967); /* First sample follows 24 operator slots. */
    assert(md_ym_take_samples(samples, 1) == 1);
    assert(samples[0] == 8128);

    write_ym_register(0, 0x2A, 0x00, 967);
    md_ym_advance_to(1975);
    assert(md_ym_take_samples(samples, 1) == 1);
    assert(samples[0] == -8192);
}

static void test_ym_dac_enable_uses_latched_data(void)
{
    int16_t samples[1];

    reset_psg();
    write_ym_register(0, 0x2A, 0xFF, 0); /* Preload while DAC output is disabled. */
    write_ym_register(0, 0x2B, 0x80, 0);
    md_ym_advance_to(967);
    assert(md_ym_take_samples(samples, 1) == 1);
    assert(samples[0] == 8128);

    write_ym_register(0, 0x2B, 0x00, 967);
    write_ym_register(0, 0x2B, 0x80, 967); /* Re-enable without another data write. */
    md_ym_advance_to(1975);
    assert(md_ym_take_samples(samples, 1) == 1);
    assert(samples[0] == 8128);
}

static void setup_fm_channel(unsigned channel, unsigned algorithm, uint64_t clock)
{
    static const uint8_t operator_offsets[4] = { 0, 4, 8, 12 };
    unsigned bank = channel / 3u;
    unsigned local = channel % 3u;
    unsigned slot;

    for (slot = 0; slot < 4; slot++) {
        uint8_t reg = (uint8_t)(0x30u + local + operator_offsets[slot]);
        write_ym_register(bank, reg, 0x01, clock); /* MUL=1, no detune. */
        write_ym_register(bank, (uint8_t)(reg + 0x10u), 0x00, clock); /* TL=0. */
        write_ym_register(bank, (uint8_t)(reg + 0x20u), 0x1F, clock); /* Fast attack. */
        write_ym_register(bank, (uint8_t)(reg + 0x30u), 0x00, clock); /* D1R=0. */
        write_ym_register(bank, (uint8_t)(reg + 0x40u), 0x00, clock); /* D2R=0. */
        write_ym_register(bank, (uint8_t)(reg + 0x50u), 0x0F, clock); /* SL=0, RR=15. */
        write_ym_register(bank, (uint8_t)(reg + 0x60u), 0x00, clock); /* SSG-EG off. */
    }
    write_ym_register(bank, (uint8_t)(0xA4u + local), 0x22, clock);
    write_ym_register(bank, (uint8_t)(0xA0u + local), 0x69, clock);
    write_ym_register(bank, (uint8_t)(0xB0u + local), (uint8_t)algorithm, clock);
    write_ym_register(bank, (uint8_t)(0xB4u + local), 0xC0, clock); /* Both outputs. */
    write_ym_register(0, 0x28, (uint8_t)(0xF0u | channel), clock);
}

static void assert_ym_algorithm_audible(unsigned algorithm)
{
    int16_t samples[32];
    unsigned audible = 0;
    unsigned i;

    reset_psg();
    setup_fm_channel(0, algorithm, 0);
    md_ym_advance_to(32u * 1008u + 1u);
    assert(md_ym_take_samples(samples, 32) == 32);
    for (i = 0; i < 32; i++)
        if (samples[i] != 0)
            audible++;
    assert(audible > 8);
}

static void test_ym_algorithm_0(void)
{
    assert_ym_algorithm_audible(0);
}

static void test_ym_algorithm_1(void)
{
    assert_ym_algorithm_audible(1);
}

static void test_ym_algorithm_2(void)
{
    assert_ym_algorithm_audible(2);
}

static void test_ym_algorithm_3(void)
{
    assert_ym_algorithm_audible(3);
}

static void test_ym_algorithm_4(void)
{
    assert_ym_algorithm_audible(4);
}

static void test_ym_algorithm_5(void)
{
    assert_ym_algorithm_audible(5);
}

static void test_ym_algorithm_6(void)
{
    assert_ym_algorithm_audible(6);
}

static void test_ym_algorithm_7(void)
{
    assert_ym_algorithm_audible(7);
}

static void test_ym_envelope_key_on_off(void)
{
    int16_t attack[32], release[128];
    int attack_peak = 0;
    int release_peak = 0;
    unsigned i;
    uint64_t key_off_clock = 32u * 1008u + 1u;

    reset_psg();
    setup_fm_channel(0, 7, 0);
    md_ym_advance_to(key_off_clock);
    assert(md_ym_take_samples(attack, 32) == 32);
    for (i = 0; i < 32; i++) {
        int value = attack[i] < 0 ? -attack[i] : attack[i];
        if (value > attack_peak)
            attack_peak = value;
    }
    write_ym_register(0, 0x28, 0x00, key_off_clock);
    md_ym_advance_to(key_off_clock + 128u * 1008u);
    assert(md_ym_take_samples(release, 128) == 128);
    for (i = 96; i < 128; i++) {
        int value = release[i] < 0 ? -release[i] : release[i];
        if (value > release_peak)
            release_peak = value;
    }
    assert(attack_peak > 0);
    assert(release_peak < attack_peak);
}

static void test_ym_channel3_and_lfo(void)
{
    int16_t normal[128], special_lfo[128];
    unsigned i;
    int differs = 0;

    reset_psg();
    setup_fm_channel(2, 7, 0);
    md_ym_advance_to(128u * 1008u + 1u);
    assert(md_ym_take_samples(normal, 128) == 128);

    reset_psg();
    setup_fm_channel(2, 7, 0);
    write_ym_register(0, 0x27, 0x40, 0); /* Channel 3 special frequencies. */
    write_ym_register(0, 0xA8, 0x58, 0);
    write_ym_register(0, 0xAC, 0x18, 0);
    write_ym_register(0, 0xA9, 0x70, 0);
    write_ym_register(0, 0xAD, 0x20, 0);
    write_ym_register(0, 0xAA, 0x88, 0);
    write_ym_register(0, 0xAE, 0x28, 0);
    write_ym_register(0, 0x22, 0x0F, 0); /* Enable fastest LFO. */
    write_ym_register(0, 0xB6, 0xF7, 0); /* Full AMS/FMS and both outputs. */
    md_ym_advance_to(128u * 1008u + 1u);
    assert(md_ym_take_samples(special_lfo, 128) == 128);

    for (i = 0; i < 128; i++)
        differs |= normal[i] != special_lfo[i];
    assert(differs);
}

static void render_channel3_special_envelope(uint8_t base_block_fnum_high,
                                             int16_t samples[128])
{
    static const uint8_t operator_offsets[4] = { 0, 4, 8, 12 };
    unsigned slot;

    reset_psg();
    write_ym_register(0, 0x27, 0x40, 0); /* Channel 3 special frequencies. */
    for (slot = 0; slot < 4; slot++) {
        uint8_t reg = (uint8_t)(0x32u + operator_offsets[slot]);
        write_ym_register(0, reg, 0x01, 0); /* MUL=1. */
        write_ym_register(0, (uint8_t)(reg + 0x10u), slot == 1u ? 0 : 0x7F, 0);
        write_ym_register(0, (uint8_t)(reg + 0x20u), 0x15, 0); /* RS=0 bits, AR=21. */
        write_ym_register(0, (uint8_t)(reg + 0x30u), 0, 0);
        write_ym_register(0, (uint8_t)(reg + 0x40u), 0, 0);
        write_ym_register(0, (uint8_t)(reg + 0x50u), 0x0F, 0);
        write_ym_register(0, (uint8_t)(reg + 0x60u), 0, 0);
    }
    write_ym_register(0, 0xA6, base_block_fnum_high, 0);
    write_ym_register(0, 0xA2, 0x69, 0);
    write_ym_register(0, 0xB2, 7, 0); /* All unmuted operators are carriers. */
    write_ym_register(0, 0xB6, 0xC0, 0);
    write_ym_register(0, 0xAC, 0x00, 0);
    write_ym_register(0, 0xA8, 0x58, 0); /* Special OP3: block 0. */
    write_ym_register(0, 0xAD, 0x38, 0);
    write_ym_register(0, 0xA9, 0x70, 0); /* Special OP2: block 7. */
    write_ym_register(0, 0xAE, 0x08, 0);
    write_ym_register(0, 0xAA, 0x88, 0); /* Special OP1: block 1. */
    write_ym_register(0, 0x28, 0xF2, 0); /* Key on channel 3. */
    md_ym_advance_to(128u * 1008u + 1u);
    assert(md_ym_take_samples(samples, 128) == 128);
}

static void test_ym_channel3_special_envelope_key_scaling(void)
{
    int16_t block4[128], block7[128];
    unsigned i;

    render_channel3_special_envelope(0x24, block4);
    render_channel3_special_envelope(0x3C, block7);
    for (i = 0; i < 128; i++)
        assert(block4[i] == block7[i]);
}

static void test_ym_stereo_pan(void)
{
    int16_t left_only[32u * 2u];
    int16_t right_only[32u * 2u];
    int32_t left_energy = 0;
    int32_t left_leakage = 0;
    int32_t right_energy = 0;
    int32_t right_leakage = 0;
    unsigned i;

    reset_psg();
    setup_fm_channel(0, 7, 0);
    write_ym_register(0, 0xB4, 0x80, 0);
    md_ym_advance_to(32u * 1008u + 1u);
    assert(md_ym_take_stereo_samples(left_only, 32) == 32);

    reset_psg();
    setup_fm_channel(0, 7, 0);
    write_ym_register(0, 0xB4, 0x40, 0);
    md_ym_advance_to(32u * 1008u + 1u);
    assert(md_ym_take_stereo_samples(right_only, 32) == 32);

    for (i = 0; i < 32; i++) {
        int32_t left = left_only[i * 2u];
        int32_t right = left_only[i * 2u + 1u];
        int32_t panned_right = right_only[i * 2u + 1u];
        int32_t panned_left = right_only[i * 2u];
        left_energy += left < 0 ? -left : left;
        left_leakage += right < 0 ? -right : right;
        right_energy += panned_right < 0 ? -panned_right : panned_right;
        right_leakage += panned_left < 0 ? -panned_left : panned_left;
    }
    assert(left_energy > left_leakage);
    assert(right_energy > right_leakage);
}

static int render_sound_audio(nmd_region region, int use_psg, int use_ym,
                              int16_t *samples, int max_frames)
{
    uint64_t lines = region == NMD_PAL ? 312u : 262u;
    uint64_t end_clock = 2u * 3420u * lines;

    md_sound_reset(region);
    if (use_psg) {
        md_psg_write(0x90, 0);
        md_psg_write(0x84, 0);
        md_psg_write(0x12, 0);
    }
    if (use_ym)
        setup_fm_channel(0, 7, 0);
    md_sound_advance_to(end_clock);
    return md_sound_audio_take(samples, max_frames);
}

static void test_sound_startup_and_counts(void)
{
    int16_t samples[2048u * 2u];
    nmd_region region;

    md_sound_reset(NMD_NTSC);
    assert(md_sound_audio_queued() == 0);
    assert(md_sound_audio_take(samples, 1) == 0);
    md_sound_advance_to(4u * 1008u);
    assert(md_sound_audio_queued() == 0); /* Both resamplers need startup taps. */
    md_sound_advance_to(64u * 1008u);
    assert(md_sound_audio_queued() > 0);

    for (region = NMD_NTSC; region <= NMD_PAL; region++) {
        uint64_t master_clock_hz = region == NMD_PAL ? 53203395ull : 53693175ull;
        uint64_t lines = region == NMD_PAL ? 312u : 262u;
        uint64_t expected = 2u * 3420u * lines * 44100u / master_clock_hz;
        int count = render_sound_audio(region, 1, 1, samples, 2048);

        assert((uint64_t)count <= expected);
        assert((uint64_t)count + 64u >= expected);
    }
}

static void test_sound_determinism_and_mix(void)
{
    int16_t baseline[2048u * 2u], ym[2048u * 2u];
    int16_t psg[2048u * 2u], both[2048u * 2u];
    int16_t repeated[2048u * 2u];
    int baseline_count, ym_count, psg_count, both_count, repeated_count;
    int i;

    baseline_count = render_sound_audio(NMD_NTSC, 0, 0, baseline, 2048);
    ym_count = render_sound_audio(NMD_NTSC, 0, 1, ym, 2048);
    psg_count = render_sound_audio(NMD_NTSC, 1, 0, psg, 2048);
    both_count = render_sound_audio(NMD_NTSC, 1, 1, both, 2048);
    repeated_count = render_sound_audio(NMD_NTSC, 1, 1, repeated, 2048);
    assert(baseline_count == ym_count && ym_count == psg_count && psg_count == both_count);
    assert(both_count == repeated_count);
    for (i = 0; i < both_count * 2; i++) {
        int sum = ym[i] + psg[i] - baseline[i];
        int16_t expected = (int16_t)(sum > INT16_MAX ? INT16_MAX :
                                     sum < INT16_MIN ? INT16_MIN : sum);
        assert(both[i] == expected);
        assert(repeated[i] == both[i]);
    }
}

static int impulse_peak_frame(const int16_t *samples, int frames)
{
    int peak = 0;
    int peak_frame = -1;
    int frame;

    for (frame = 0; frame < frames; frame++) {
        int value = samples[frame * 2];
        int magnitude = value < 0 ? -value : value;
        if (magnitude > peak) {
            peak = magnitude;
            peak_frame = frame;
        }
    }
    return peak_frame;
}

static void test_sound_mixer_source_phase_alignment(void)
{
    static const double master_clock_hz = 53693175.0;
    int16_t ym_impulse[128u * 2u] = { 0 };
    int16_t ym_silence[128u * 2u] = { 0 };
    int16_t psg_impulse[512u * 2u] = { 0 };
    int16_t psg_silence[512u * 2u] = { 0 };
    int16_t ym_output[128u * 2u], psg_output[128u * 2u];
    md_sound_mixer *mixer;
    int ym_frames, psg_frames;
    int ym_peak, psg_peak;

    ym_impulse[0] = ym_impulse[1] = 20000;
    psg_impulse[0] = psg_impulse[1] = 20000;
    mixer = md_sound_mixer_create(master_clock_hz / 1008.0,
                                  master_clock_hz / 240.0,
                                  MD_SOUND_OUTPUT_RATE);
    assert(mixer);
    assert(md_sound_mixer_push_ym(mixer, ym_impulse, 128) == 128);
    assert(md_sound_mixer_push_psg(mixer, psg_silence, 512) == 512);
    ym_frames = md_sound_mixer_take(mixer, ym_output, 128);
    ym_peak = impulse_peak_frame(ym_output, ym_frames);
    md_sound_mixer_destroy(mixer);

    mixer = md_sound_mixer_create(master_clock_hz / 1008.0,
                                  master_clock_hz / 240.0,
                                  MD_SOUND_OUTPUT_RATE);
    assert(mixer);
    assert(md_sound_mixer_push_ym(mixer, ym_silence, 128) == 128);
    assert(md_sound_mixer_push_psg(mixer, psg_impulse, 512) == 512);
    psg_frames = md_sound_mixer_take(mixer, psg_output, 128);
    psg_peak = impulse_peak_frame(psg_output, psg_frames);
    md_sound_mixer_destroy(mixer);

    assert(ym_frames > 0 && psg_frames > 0);
    assert(ym_peak == psg_peak + 1); /* YM's first source frame is at MCLK 966. */
}

static void test_sound_queue_full_wrap_and_empty(void)
{
    static int16_t half[MD_SOUND_QUEUE_CAPACITY];
    static int16_t full[MD_SOUND_QUEUE_CAPACITY * 2u];
    uint64_t frame_clocks = 3420u * 262u;
    uint64_t clock = 0;
    unsigned i;

    md_sound_reset(NMD_NTSC);
    md_psg_write(0x90, 0);
    md_psg_write(0x84, 0);
    md_psg_write(0x12, 0);
    setup_fm_channel(0, 7, 0);
    for (i = 0; i < 40u && md_sound_audio_queued() < MD_SOUND_QUEUE_CAPACITY; i++) {
        clock += frame_clocks;
        md_sound_advance_to(clock);
    }
    assert(md_sound_audio_queued() == MD_SOUND_QUEUE_CAPACITY);
    assert(md_sound_audio_take(half, (int)(MD_SOUND_QUEUE_CAPACITY / 2u)) ==
           (int)(MD_SOUND_QUEUE_CAPACITY / 2u));
    assert(md_sound_audio_queued() == MD_SOUND_QUEUE_CAPACITY / 2u);
    for (i = 0; i < 40u && md_sound_audio_queued() < MD_SOUND_QUEUE_CAPACITY; i++) {
        clock += frame_clocks;
        md_sound_advance_to(clock);
    }
    assert(md_sound_audio_queued() == MD_SOUND_QUEUE_CAPACITY);
    assert(md_sound_audio_take(full, (int)MD_SOUND_QUEUE_CAPACITY) ==
           (int)MD_SOUND_QUEUE_CAPACITY);
    assert(md_sound_audio_queued() == 0);
    assert(md_sound_audio_take(full, 1) == 0);
}

static void reset_audio_queue_from_take_hook(void)
{
    md_sound_audio_take_test_hook = NULL;
    md_sound_reset(NMD_NTSC);
    md_psg_write(0x90, 0);
    md_psg_write(0x84, 0);
    md_psg_write(0x12, 0);
    md_sound_advance_to(64u * 1008u); /* The real-time producer drops during handoff. */
}

static void test_sound_reset_during_audio_drain(void)
{
    int16_t samples[16u * 2u];
    int frames;

    md_sound_reset(NMD_NTSC);
    md_psg_write(0x90, 0);
    md_psg_write(0x84, 0);
    md_psg_write(0x12, 0);
    md_sound_advance_to(64u * 1008u);
    assert(md_sound_audio_queued() > 16);

    md_sound_audio_take_test_hook = reset_audio_queue_from_take_hook;
    frames = md_sound_audio_take(samples, 16);
    assert(frames == 0); /* The drain snapshot belongs to the previous power epoch. */
    assert(md_sound_audio_queued() == 0);

    md_sound_advance_to(2u * 64u * 1008u);
    frames = md_sound_audio_take(samples, 16);
    assert(frames > 0);
}

static void test_sound_long_advance_drains_chip_rings(void)
{
    md_sound_reset(NMD_NTSC);
    md_psg_write(0x90, 0);
    md_psg_write(0x84, 0);
    md_psg_write(0x12, 0);
    setup_fm_channel(0, 7, 0);
    md_sound_advance_to(70000u * 240u);
    assert(md_sound_audio_queued() > 0);
    assert(md_sound_audio_queued() < MD_SOUND_QUEUE_CAPACITY);
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
    assert(md_psg.attenuation[0] == 15); /* Data-only continuation mutes the latched channel. */
}

static void test_psg_latch_continuation_across_frame(void)
{
    const uint64_t frame_clock = 3420u * 262u;

    reset_psg();
    md_psg_write(0x90, frame_clock - 20u); /* Select channel 0 volume before VBlank. */
    md_psg_advance_to(frame_clock + 1u);
    md_psg_write(0x3F, frame_clock + 1u); /* The latch persists into the next frame. */
    assert(md_psg.attenuation[0] == 15);
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

    reset_psg();
    md_psg_write(0xC4, 0); /* Tone 3 has a four-sample period. */
    md_psg_write(0x00, 0);
    md_psg_write(0xE0, 0); /* Start the independent fixed noise divider. */
    md_psg_advance_to(7u * 240u + 1u); /* Eight tone ticks; next tick raises tone 3. */
    periodic_shifts = md_psg.noise_shift_count;
    md_psg_write(0xE3, 7u * 240u + 1u); /* Switch to tone-3 clock while out of phase. */
    md_psg_advance_to(8u * 240u + 1u);
    assert(md_psg.noise_shift_count == periodic_shifts + 1u);

    reset_psg();
    md_psg_write(0xC4, 0);
    md_psg_write(0x00, 0);
    md_psg_write(0xE0, 0);
    md_psg_advance_to(3420u * 262u + 1u);
    for (periodic_shifts = 0; periodic_shifts < 8u; periodic_shifts++) {
        if (!md_psg.tone_output[2] && md_psg.tone_counter[2] == 1u)
            break;
        md_psg_advance_to(md_psg.next_tick_clock + 1u);
    }
    assert(!md_psg.tone_output[2] && md_psg.tone_counter[2] == 1u);
    periodic_shifts = md_psg.noise_shift_count;
    md_psg_write(0xE3, md_psg.master_clock); /* Switch one source tick before a rising edge. */
    md_psg_advance_to(md_psg.next_tick_clock + 1u);
    assert(md_psg.noise_shift_count == periodic_shifts + 1u);
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
    test_ym_timer_b_integral_reload_deadline();
    test_ym_dac_samples();
    test_ym_dac_enable_uses_latched_data();
    test_ym_algorithm_0();
    test_ym_algorithm_1();
    test_ym_algorithm_2();
    test_ym_algorithm_3();
    test_ym_algorithm_4();
    test_ym_algorithm_5();
    test_ym_algorithm_6();
    test_ym_algorithm_7();
    test_ym_envelope_key_on_off();
    test_ym_channel3_and_lfo();
    test_ym_channel3_special_envelope_key_scaling();
    test_ym_stereo_pan();
    test_sound_startup_and_counts();
    test_sound_determinism_and_mix();
    test_sound_mixer_source_phase_alignment();
    test_sound_queue_full_wrap_and_empty();
    test_sound_reset_during_audio_drain();
    test_sound_long_advance_drains_chip_rings();
    test_psg_latch_data();
    test_psg_latch_continuation_across_frame();
    test_psg_period_zero();
    test_psg_noise_modes_and_tone3_clock();
    test_psg_attenuation();
    return 0;
}
