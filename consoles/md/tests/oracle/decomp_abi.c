/* Shared candidate-only NMDH v1 callback state and event validation. */
#include "decomp_abi.h"

#include <stddef.h>

static nmd_decomp_callback receiver;
static void *receiver_user;
static uint64_t sequence;
static int failed;

uint32_t nmd_decomp_abi_version(void)
{
    return NMD_DECOMP_ABI_VERSION;
}

uint64_t nmd_decomp_capabilities(void)
{
    uint64_t capabilities = 0;

#ifdef NMD_DECOMP_HAS_M68K_INSTRUCTION
    capabilities |= NMD_DECOMP_CAP_M68K_INSTRUCTION;
#endif
#ifdef NMD_DECOMP_HAS_M68K_STATE
    capabilities |= NMD_DECOMP_CAP_M68K_STATE;
#endif
#ifdef NMD_DECOMP_HAS_Z80_INSTRUCTION
    capabilities |= NMD_DECOMP_CAP_Z80_INSTRUCTION;
#endif
#ifdef NMD_DECOMP_HAS_Z80_STATE
    capabilities |= NMD_DECOMP_CAP_Z80_STATE;
#endif
#ifdef NMD_DECOMP_HAS_FLOW_EDGE
    capabilities |= NMD_DECOMP_CAP_FLOW_EDGE;
#endif
#ifdef NMD_DECOMP_HAS_RESET_EXCEPTION_INTERRUPT
    capabilities |= NMD_DECOMP_CAP_RESET_EXCEPTION_INTERRUPT;
#endif
#ifdef NMD_DECOMP_HAS_FETCH_PROVENANCE
    capabilities |= NMD_DECOMP_CAP_FETCH_PROVENANCE;
#endif
#ifdef NMD_DECOMP_HAS_BUS_INTERVALS
    capabilities |= NMD_DECOMP_CAP_BUS_INTERVALS;
#endif
#ifdef NMD_DECOMP_HAS_MASTER_CLOCK
    capabilities |= NMD_DECOMP_CAP_MASTER_CLOCK;
#endif
#ifdef NMD_DECOMP_HAS_SYNC_CHECKPOINT
    capabilities |= NMD_DECOMP_CAP_SYNC_CHECKPOINT;
#endif
#ifdef NMD_DECOMP_HAS_CONTROLLED_STOP
    capabilities |= NMD_DECOMP_CAP_CONTROLLED_STOP;
#endif
#ifdef NMD_DECOMP_HAS_REPLACE_HANDOFF
    capabilities |= NMD_DECOMP_CAP_REPLACE_HANDOFF;
#endif
    return capabilities;
}

int nmd_decomp_register(uint32_t version, nmd_decomp_callback callback, void *user)
{
    if (version != NMD_DECOMP_ABI_VERSION || !callback || receiver)
        return 0;

    receiver = callback;
    receiver_user = user;
    return 1;
}

void nmd_decomp_reset(void)
{
    receiver = NULL;
    receiver_user = NULL;
    sequence = 0;
    failed = 0;
}

int nmd_decomp_error(void)
{
    return failed;
}

static int valid_event(const struct nmd_decomp_event *event)
{
    return event && event->struct_size == sizeof(*event) &&
           event->kind >= NMD_DECOMP_INSTRUCTION &&
           event->kind <= NMD_DECOMP_REPLACE_EVENT &&
           (event->cpu == NMD_DECOMP_M68K || event->cpu == NMD_DECOMP_Z80);
}

enum nmd_decomp_action nmd_decomp_publish(struct nmd_decomp_event *event)
{
    struct nmd_decomp_event copy;
    enum nmd_decomp_action action;

    if (failed || !valid_event(event) || sequence == UINT64_MAX) {
        failed = 1;
        return NMD_DECOMP_STOP;
    }

    copy = *event;
    copy.sequence = ++sequence;
    *event = copy;

    if (!receiver)
        return NMD_DECOMP_CONTINUE;

    action = receiver(receiver_user, &copy);
    if (action < NMD_DECOMP_CONTINUE || action > NMD_DECOMP_REPLACE) {
        failed = 1;
        return NMD_DECOMP_STOP;
    }
    if (copy.kind != NMD_DECOMP_INSTRUCTION && action != NMD_DECOMP_CONTINUE) {
        failed = 1;
        return NMD_DECOMP_STOP;
    }
    return action;
}
