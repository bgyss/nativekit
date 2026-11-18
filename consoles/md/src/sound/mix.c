/* Bounded resampling, digital mixing, and host audio queue for nativemd. */
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nk/fatal.h"
#include "nk/resample.h"
#include "sound.h"

#define MD_SOUND_PENDING_CAPACITY 8192u
#define MD_SOUND_MIX_BLOCK 2048
#define MD_SOUND_ADVANCE_CHUNK_CLOCKS (MD_SOUND_MIX_BLOCK * 240u)
#define MD_AUDIO_READER_IDLE 0u
#define MD_AUDIO_READER_ACTIVE 1u
#define MD_AUDIO_RESET_PENDING 2u

struct md_sound_mixer {
    Resampler *ym;
    Resampler *psg;
    int16_t ym_pending[MD_SOUND_PENDING_CAPACITY * 2u];
    int16_t psg_pending[MD_SOUND_PENDING_CAPACITY * 2u];
    uint32_t ym_read;
    uint32_t ym_write;
    uint32_t psg_read;
    uint32_t psg_write;
};

static md_sound_mixer *active_mixer;
static int16_t audio_ring[MD_SOUND_QUEUE_CAPACITY * 2u];
static atomic_uint audio_read;
static atomic_uint audio_write;
static atomic_uint audio_reader_state;
#ifdef NMD_TESTING
void (*md_sound_audio_take_test_hook)(void);
#endif
static uint64_t sound_clock;

static int16_t float_to_s16(float sample)
{
    if (sample >= 1.0f)
        return INT16_MAX;
    if (sample <= -1.0f)
        return INT16_MIN;
    return (int16_t)(sample * INT16_MAX);
}

static int16_t clamp_s16(int32_t sample)
{
    if (sample > INT16_MAX)
        return INT16_MAX;
    if (sample < INT16_MIN)
        return INT16_MIN;
    return (int16_t)sample;
}

static uint32_t pending_count(uint32_t read, uint32_t write)
{
    return write - read;
}

static void pull_source(Resampler *resampler, int16_t *pending,
                        uint32_t *read, uint32_t *write)
{
    float output[MD_SOUND_MIX_BLOCK * 2u];

    while (pending_count(*read, *write) < MD_SOUND_PENDING_CAPACITY) {
        uint32_t free_frames = MD_SOUND_PENDING_CAPACITY - pending_count(*read, *write);
        int request = free_frames > MD_SOUND_MIX_BLOCK ?
            MD_SOUND_MIX_BLOCK : (int)free_frames;
        int frames = resample_pull(resampler, output, request);
        int i;

        if (frames <= 0)
            return;
        for (i = 0; i < frames; i++) {
            uint32_t index = *write % MD_SOUND_PENDING_CAPACITY;
            pending[index * 2u] = float_to_s16(output[i * 2]);
            pending[index * 2u + 1u] = float_to_s16(output[i * 2 + 1]);
            (*write)++;
        }
    }
}

static int push_source(Resampler *resampler, int16_t *pending,
                       uint32_t *read, uint32_t *write,
                       const int16_t *samples, int frames)
{
    int accepted_total = 0;

    while (accepted_total < frames) {
        int accepted = resample_push(resampler,
            samples + (size_t)accepted_total * 2u, frames - accepted_total);
        accepted_total += accepted;
        pull_source(resampler, pending, read, write);
        if (!accepted)
            break;
    }
    return accepted_total;
}

md_sound_mixer *md_sound_mixer_create(double ym_rate, double psg_rate,
                                      double output_rate)
{
    md_sound_mixer *mixer;
    int16_t silence[(MD_SOUND_RESAMPLER_PREROLL + MD_SOUND_YM_START_OFFSET) * 2u] = { 0 };

    if (!(ym_rate > 0.0) || !(psg_rate > 0.0) || !(output_rate > 0.0))
        return NULL;
    mixer = (md_sound_mixer *)calloc(1, sizeof *mixer);
    if (!mixer)
        return NULL;
    mixer->ym = resample_new(ym_rate, output_rate);
    mixer->psg = resample_new(psg_rate, output_rate);
    if (!mixer->ym || !mixer->psg) {
        resample_free(mixer->ym);
        resample_free(mixer->psg);
        free(mixer);
        return NULL;
    }
    /* Remove the resampler's 15-input-frame lookahead from both source clocks.
       YM's first sample is at MCLK 966, one source frame after PSG's MCLK 0. */
    if (resample_push(mixer->ym, silence,
                      (int)(MD_SOUND_RESAMPLER_PREROLL + MD_SOUND_YM_START_OFFSET)) !=
            (int)(MD_SOUND_RESAMPLER_PREROLL + MD_SOUND_YM_START_OFFSET) ||
        resample_push(mixer->psg, silence, (int)MD_SOUND_RESAMPLER_PREROLL) !=
            (int)MD_SOUND_RESAMPLER_PREROLL) {
        resample_free(mixer->ym);
        resample_free(mixer->psg);
        free(mixer);
        return NULL;
    }
    return mixer;
}

void md_sound_mixer_destroy(md_sound_mixer *mixer)
{
    if (!mixer)
        return;
    if (mixer->ym)
        resample_free(mixer->ym);
    if (mixer->psg)
        resample_free(mixer->psg);
    free(mixer);
}

int md_sound_mixer_push_ym(md_sound_mixer *mixer, const int16_t *samples,
                           int frames)
{
    if (!mixer || frames < 0 || (frames && !samples))
        nk_fatal("invalid YM2612 mix input");
    return push_source(mixer->ym, mixer->ym_pending, &mixer->ym_read,
                       &mixer->ym_write, samples, frames);
}

int md_sound_mixer_push_psg(md_sound_mixer *mixer, const int16_t *samples,
                            int frames)
{
    if (!mixer || frames < 0 || (frames && !samples))
        nk_fatal("invalid PSG mix input");
    return push_source(mixer->psg, mixer->psg_pending, &mixer->psg_read,
                       &mixer->psg_write, samples, frames);
}

int md_sound_mixer_take(md_sound_mixer *mixer, int16_t *samples,
                        int max_frames)
{
    uint32_t ym_available, psg_available;
    int frames, i;

    if (!mixer || max_frames < 0 || (max_frames && !samples))
        nk_fatal("invalid mixed audio drain buffer");
    pull_source(mixer->ym, mixer->ym_pending, &mixer->ym_read, &mixer->ym_write);
    pull_source(mixer->psg, mixer->psg_pending, &mixer->psg_read, &mixer->psg_write);
    ym_available = pending_count(mixer->ym_read, mixer->ym_write);
    psg_available = pending_count(mixer->psg_read, mixer->psg_write);
    frames = (int)(ym_available < psg_available ? ym_available : psg_available);
    if (frames > max_frames)
        frames = max_frames;

    for (i = 0; i < frames; i++) {
        uint32_t ym_index = mixer->ym_read % MD_SOUND_PENDING_CAPACITY;
        uint32_t psg_index = mixer->psg_read % MD_SOUND_PENDING_CAPACITY;
        samples[i * 2] = clamp_s16((int32_t)mixer->ym_pending[ym_index * 2u] +
                                   mixer->psg_pending[psg_index * 2u]);
        samples[i * 2 + 1] = clamp_s16((int32_t)mixer->ym_pending[ym_index * 2u + 1u] +
                                       mixer->psg_pending[psg_index * 2u + 1u]);
        mixer->ym_read++;
        mixer->psg_read++;
    }
    return frames;
}

void md_sound_reset(nmd_region region)
{
    unsigned reader_state = atomic_exchange_explicit(&audio_reader_state,
        MD_AUDIO_RESET_PENDING, memory_order_acq_rel);
    uint32_t write;

    md_ym_reset(region);
    md_psg_reset(region);
    md_sound_mixer_destroy(active_mixer);
    active_mixer = md_sound_mixer_create(md_ym_source_rate(), md_psg_source_rate(),
                                         MD_SOUND_OUTPUT_RATE);
    if (!active_mixer)
        nk_fatal("cannot create sound resamplers");
    if (reader_state == MD_AUDIO_READER_IDLE) {
        write = atomic_load_explicit(&audio_write, memory_order_acquire);
        atomic_store_explicit(&audio_read, write, memory_order_release);
        atomic_store_explicit(&audio_reader_state, MD_AUDIO_READER_IDLE,
                              memory_order_release);
    }
    sound_clock = 0;
}

static void queue_output(const int16_t *samples, int frames)
{
    uint32_t write = atomic_load_explicit(&audio_write, memory_order_relaxed);
    uint32_t read = atomic_load_explicit(&audio_read, memory_order_acquire);
    int i;

    if (atomic_load_explicit(&audio_reader_state, memory_order_acquire) ==
        MD_AUDIO_RESET_PENDING)
        return; /* Let a drain that crossed reset retire before reusing ring slots. */

    for (i = 0; i < frames; i++) {
        if (write - read >= MD_SOUND_QUEUE_CAPACITY)
            break; /* The real-time producer drops new frames instead of waiting. */
        audio_ring[(write % MD_SOUND_QUEUE_CAPACITY) * 2u] = samples[i * 2];
        audio_ring[(write % MD_SOUND_QUEUE_CAPACITY) * 2u + 1u] = samples[i * 2 + 1];
        write++;
    }
    atomic_store_explicit(&audio_write, write, memory_order_release);
}

static void sound_process_at(uint64_t master_clock)
{
    int16_t ym_samples[MD_SOUND_MIX_BLOCK * 2u];
    int16_t psg_samples[MD_SOUND_MIX_BLOCK];
    int16_t psg_stereo[MD_SOUND_MIX_BLOCK * 2u];
    int16_t mixed[MD_SOUND_MIX_BLOCK * 2u];
    int frames, i;

    if (!active_mixer)
        nk_fatal("sound mixer advanced before reset");
    md_ym_advance_to(master_clock);
    md_psg_advance_to(master_clock);
    sound_clock = master_clock;

    while ((frames = md_ym_take_stereo_samples(ym_samples, MD_SOUND_MIX_BLOCK)) > 0) {
        if (md_sound_mixer_push_ym(active_mixer, ym_samples, frames) != frames)
            nk_fatal("sound mixer could not accept YM2612 samples");
    }
    while ((frames = md_psg_take_samples(psg_samples, MD_SOUND_MIX_BLOCK)) > 0) {
        for (i = 0; i < frames; i++) {
            psg_stereo[i * 2] = psg_samples[i];
            psg_stereo[i * 2 + 1] = psg_samples[i];
        }
        if (md_sound_mixer_push_psg(active_mixer, psg_stereo, frames) != frames)
            nk_fatal("sound mixer could not accept PSG samples");
    }
    while ((frames = md_sound_mixer_take(active_mixer, mixed, MD_SOUND_MIX_BLOCK)) > 0)
        queue_output(mixed, frames);
}

void md_sound_advance_to(uint64_t master_clock)
{
    uint64_t cursor;

    if (master_clock < sound_clock)
        nk_fatal("sound mixer timestamp moved backward");
    cursor = sound_clock;
    while (master_clock - cursor > MD_SOUND_ADVANCE_CHUNK_CLOCKS) {
        cursor += MD_SOUND_ADVANCE_CHUNK_CLOCKS;
        sound_process_at(cursor);
    }
    sound_process_at(master_clock);
    sound_clock = master_clock;
}

int md_sound_audio_take(int16_t *samples, int max_frames)
{
    uint32_t read;
    uint32_t write;
    unsigned expected = MD_AUDIO_READER_IDLE;
    int frames = 0;

    if (max_frames < 0 || (max_frames && !samples))
        nk_fatal("invalid console audio drain buffer");
    if (!atomic_compare_exchange_strong_explicit(&audio_reader_state, &expected,
            MD_AUDIO_READER_ACTIVE, memory_order_acq_rel, memory_order_acquire))
        return 0;
    read = atomic_load_explicit(&audio_read, memory_order_relaxed);
    write = atomic_load_explicit(&audio_write, memory_order_acquire);
#ifdef NMD_TESTING
    if (md_sound_audio_take_test_hook)
        md_sound_audio_take_test_hook();
#endif
    while (read != write && frames < max_frames) {
        uint32_t index = read % MD_SOUND_QUEUE_CAPACITY;
        samples[frames * 2] = audio_ring[index * 2u];
        samples[frames * 2 + 1] = audio_ring[index * 2u + 1u];
        read++;
        frames++;
    }
    atomic_store_explicit(&audio_read, read, memory_order_release);
    expected = MD_AUDIO_READER_ACTIVE;
    if (!atomic_compare_exchange_strong_explicit(&audio_reader_state, &expected,
            MD_AUDIO_READER_IDLE, memory_order_acq_rel, memory_order_acquire)) {
        uint32_t reset_read = atomic_load_explicit(&audio_write, memory_order_acquire);
        atomic_store_explicit(&audio_read, reset_read, memory_order_release);
        if (frames)
            memset(samples, 0, (size_t)frames * 2u * sizeof *samples);
        atomic_store_explicit(&audio_reader_state, MD_AUDIO_READER_IDLE,
                              memory_order_release);
        return 0;
    }
    return frames;
}

int md_sound_audio_queued(void)
{
    if (atomic_load_explicit(&audio_reader_state, memory_order_acquire) ==
        MD_AUDIO_RESET_PENDING)
        return 0;
    uint32_t write = atomic_load_explicit(&audio_write, memory_order_acquire);
    uint32_t read = atomic_load_explicit(&audio_read, memory_order_acquire);
    return (int)(write - read);
}
