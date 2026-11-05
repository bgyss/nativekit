/* Shared candidate hook logic; capabilities only cover instrumented paths. */
#include "trace_abi.h"
#include <stddef.h>
#include <string.h>

static nmd_trace_callback receiver;
static void *receiver_user;
static nmd_trace_audio_callback audio_receiver;
static void *audio_receiver_user;
static uint64_t epoch, last[6];
static int failed;

uint32_t nmd_trace_version(void) { return NMD_TRACE_ABI_VERSION; }
uint64_t nmd_trace_capabilities(void)
{
    uint64_t capabilities = NMD_TRACE_CAP_M68K_CPU;
#ifdef NMD_TRACE_HAS_VDP_PORT
    capabilities |= NMD_TRACE_CAP_VDP_PORT;
#endif
#ifdef NMD_TRACE_HAS_PSG_WRITE
    capabilities |= NMD_TRACE_CAP_PSG_WRITE;
#endif
#ifdef NMD_TRACE_HAS_YM_WRITE
    capabilities |= NMD_TRACE_CAP_YM_WRITE;
#endif
#ifdef NMD_TRACE_HAS_YM_PORT
    capabilities |= NMD_TRACE_CAP_YM_PORT;
#endif
#ifdef NMD_TRACE_HAS_Z80_CPU
    capabilities |= NMD_TRACE_CAP_Z80_CPU;
#endif
#ifdef NMD_TRACE_HAS_VDP_DMA_68K
    capabilities |= NMD_TRACE_CAP_VDP_DMA_68K;
#endif
#ifdef NMD_TRACE_HAS_CHIP_AUDIO
    capabilities |= NMD_TRACE_CAP_CHIP_AUDIO;
#endif
#ifdef NMD_TRACE_HAS_VDP_DMA_FILL
    capabilities |= NMD_TRACE_CAP_VDP_DMA_FILL;
#endif
#ifdef NMD_TRACE_HAS_VDP_DMA_COPY
    capabilities |= NMD_TRACE_CAP_VDP_DMA_COPY;
#endif
#ifdef NMD_TRACE_HAS_VDP_HINT_LINE
    capabilities |= NMD_TRACE_CAP_VDP_HINT_LINE;
#endif
#ifdef NMD_TRACE_HAS_CHIP_AUDIO_SOURCE
    capabilities |= NMD_TRACE_CAP_CHIP_AUDIO_SOURCE;
#endif
    return capabilities;
}

int nmd_trace_register(uint32_t version, nmd_trace_callback callback, void *user)
{
    if (version != NMD_TRACE_ABI_VERSION)
        return 0;
    receiver = callback;
    receiver_user = user;
    return 1;
}

int nmd_trace_audio_register(uint32_t version, nmd_trace_audio_callback callback, void *user)
{
    if (version != NMD_TRACE_ABI_VERSION)
        return 0;
    audio_receiver = callback;
    audio_receiver_user = user;
    return 1;
}

int nmd_trace_audio_active(void)
{
    return audio_receiver != NULL;
}

void nmd_trace_audio_emit(uint8_t device, int16_t left, int16_t right)
{
    if (audio_receiver && (device == NMD_TRACE_YM2612 || device == NMD_TRACE_PSG))
        audio_receiver(audio_receiver_user, device, left, right);
}

void nmd_trace_audio_source_emit(uint8_t device, int16_t left, int16_t right)
{
    uint8_t source_device;

    if (device == NMD_TRACE_YM2612)
        source_device = NMD_TRACE_YM2612_SOURCE;
    else if (device == NMD_TRACE_PSG)
        source_device = NMD_TRACE_PSG_SOURCE;
    else
        return;
    if (audio_receiver)
        audio_receiver(audio_receiver_user, source_device, left, right);
}

void nmd_trace_reset(void)
{
    epoch = 0;
    failed = 0;
    memset(last, 0, sizeof last);
}

void nmd_trace_adjust(uint64_t deduction)
{
    if (UINT64_MAX - epoch < deduction)
        failed = 1;
    else
        epoch += deduction;
}

void nmd_trace_emit(uint32_t local_clock, uint8_t device, uint8_t operation,
                    uint8_t width, uint32_t address, uint32_t value)
{
    uint64_t clock;
    if (failed || device < 1 || device > 5 || operation < 1 || operation > NMD_TRACE_IRQ ||
        (width != 1 && width != 2 && width != 4) || UINT64_MAX - epoch < local_clock) {
        failed = 1;
        return;
    }
    clock = epoch + local_clock;
    if (clock < last[device]) {
        failed = 1;
        return;
    }
    last[device] = clock;
    if (receiver)
        receiver(receiver_user, clock, device, operation, width, address, value);
}

int nmd_trace_error(void) { return failed; }

void nmd_trace_get_checkpoint_state(struct nmd_trace_checkpoint_state *state)
{
    if (!state)
        return;
    state->version = 1;
    state->failed = (uint32_t)failed;
    state->epoch = epoch;
    memcpy(state->last, last, sizeof last);
}

int nmd_trace_set_checkpoint_state(const struct nmd_trace_checkpoint_state *state)
{
    if (!state || state->version != 1 || state->failed > 1) {
        failed = 1;
        return 0;
    }
    failed = (int)state->failed;
    epoch = state->epoch;
    memcpy(last, state->last, sizeof last);
    return 1;
}
