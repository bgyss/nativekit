/* Shared candidate-only NMDH v1 callback state and event validation. */
#include "decomp_abi.h"

#include <stddef.h>

static nmd_decomp_callback receiver;
static void *receiver_user;
static uint64_t sequence;
static int failed;
static int stop_pending;
static int handed_off;
static uint32_t stop_action;
static uint32_t stop_cpu;
static uint32_t stop_pc;
static uint32_t stop_next_pc;
static uint32_t stop_criterion;
static uint64_t stop_clock;
static uint64_t stop_value;
static uint64_t stop_instruction_count;
static int stop_metadata_set;

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
    stop_pending = 0;
    handed_off = 0;
    stop_action = NMD_DECOMP_CONTINUE;
    stop_cpu = 0;
    stop_pc = 0;
    stop_next_pc = 0;
    stop_criterion = 0;
    stop_clock = 0;
    stop_value = 0;
    stop_instruction_count = 0;
    stop_metadata_set = 0;
}

int nmd_decomp_error(void)
{
    return failed;
}

int nmd_decomp_stop_pending(void)
{
    return stop_pending;
}

uint32_t nmd_decomp_stop_action(void)
{
    return stop_action;
}

uint32_t nmd_decomp_stop_cpu(void)
{
    return stop_cpu;
}

uint32_t nmd_decomp_stop_pc(void)
{
    return stop_pc;
}

uint32_t nmd_decomp_stop_next_pc(void)
{
    return stop_next_pc;
}

uint64_t nmd_decomp_stop_clock(void)
{
    return stop_clock;
}

int nmd_decomp_should_stop_cpu(uint32_t cpu, uint64_t master_clock)
{
    if (failed || handed_off)
        return 1;
    if (!stop_pending)
        return 0;
    return cpu == stop_cpu || master_clock >= stop_clock;
}

int nmd_decomp_handed_off(void)
{
    return handed_off;
}

void nmd_decomp_set_stop_metadata(uint32_t criterion, uint64_t value,
                                  uint64_t instruction_count)
{
    stop_criterion = criterion;
    stop_value = value;
    stop_instruction_count = instruction_count;
    stop_metadata_set = 1;
}

void nmd_decomp_fill_stop(struct nmd_decomp_stop *stop)
{
    if (!stop)
        return;
    stop->criterion = stop_criterion;
    stop->reason = stop_action;
    stop->value = stop_value;
    stop->instruction_count = stop_instruction_count;
    stop->stop_clock = stop_clock;
}

void nmd_decomp_mark_handed_off(void)
{
    if (stop_pending)
        handed_off = 1;
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

    stop_metadata_set = 0;
    action = receiver(receiver_user, &copy);
    if (action < NMD_DECOMP_CONTINUE || action > NMD_DECOMP_REPLACE) {
        failed = 1;
        return NMD_DECOMP_STOP;
    }
    if (copy.kind != NMD_DECOMP_INSTRUCTION && action != NMD_DECOMP_CONTINUE) {
        failed = 1;
        return NMD_DECOMP_STOP;
    }
    if (action != NMD_DECOMP_CONTINUE) {
        if (stop_pending) {
            failed = 1;
            return NMD_DECOMP_STOP;
        }
        stop_pending = 1;
        stop_action = action;
        stop_cpu = copy.cpu;
        stop_pc = copy.pc;
        stop_next_pc = copy.next_pc;
        stop_clock = copy.master_clock;
        if (!stop_metadata_set) {
            stop_criterion = 0;
            stop_value = 0;
            stop_instruction_count = 0;
        }
    }
    return action;
}
