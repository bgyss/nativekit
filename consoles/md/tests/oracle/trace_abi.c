/* Shared candidate hook logic; capabilities remain zero until core paths are proven. */
#include "trace_abi.h"
#include <stddef.h>
#include <string.h>

static nmd_trace_callback receiver;
static void *receiver_user;
static uint64_t epoch, last[6];
static int failed;

uint32_t nmd_trace_version(void) { return NMD_TRACE_ABI_VERSION; }
uint64_t nmd_trace_capabilities(void) { return 0; }

int nmd_trace_register(uint32_t version, nmd_trace_callback callback, void *user)
{
    if (version != NMD_TRACE_ABI_VERSION)
        return 0;
    receiver = callback;
    receiver_user = user;
    return 1;
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
    if (failed || device < 1 || device > 5 || operation < 1 || operation > 4 ||
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
