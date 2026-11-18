/* live.h: the lockstep runner's side of the live hybrid library (test-only). */
#ifndef NMD_CHECK_LIVE_H
#define NMD_CHECK_LIVE_H

#include <stdint.h>

enum {
    NMD_LIVE_OK = 0,
    NMD_LIVE_ORDER = 1,         /* hardware access differs from the original's next one */
    NMD_LIVE_PAST_RECORD = 2,   /* hardware access after the original's last */
    NMD_LIVE_ODD = 3,
    NMD_LIVE_UNSUPPORTED = 4,
    NMD_LIVE_EXCEPTION = 5      /* the original took an exception here; the C did not */
};

/* One hardware access of the original's call: what it was, when it began,
 * and the machine's RAM refresh counter as it stood then (68000 only). */
typedef struct nmd_live_slot {
    uint64_t clock;
    uint64_t refresh_sync;
    uint32_t address;
    uint32_t refresh_counter;
    uint32_t prefetch;      /* the 68000's prefetch register: open-bus bits */
    uint8_t width;
    uint8_t write;
} nmd_live_slot;

typedef struct nmd_live_result {
    int status;
    uint32_t address, width;
    int write;
    uint32_t slot;          /* the record position at the difference */
    uint32_t slots_used;
    uint32_t accesses;      /* bus calls the C made */
    uint32_t hardware;      /* of them made at a recorded clock */
} nmd_live_result;

/* the reference core's nmd_decomp_m68k_live_access and nmd_decomp_z80_live_access */
typedef uint32_t (*nmd_live_m68k_bus)(uint32_t address, uint32_t width, uint32_t write,
                                      uint32_t value, uint64_t master_clock,
                                      uint32_t refresh_counter, uint64_t refresh_sync,
                                      uint32_t prefetch);
typedef uint32_t (*nmd_live_z80_bus)(uint32_t address, uint32_t write, uint32_t value,
                                     uint64_t master_clock);

typedef void (*nmd_live_sync)(uint64_t master_clock, uint32_t refresh_counter,
                              uint64_t refresh_sync);

/* One machine sync of the original inside the call: its clock and the RAM
 * refresh counter as it stood when the sync began. */
typedef struct nmd_live_sync_point {
    uint64_t clock;
    uint64_t refresh_sync;
    uint32_t refresh_counter;
} nmd_live_sync_point;

void nmd_live_bind(nmd_live_m68k_bus m68k, nmd_live_z80_bus z80, nmd_live_sync sync,
                   uint32_t image_size);

enum { NMD_LIVE_PAUSE_INTERRUPT = 0, NMD_LIVE_PAUSE_EXCEPTION = 1 };

/* Where the original took an interrupt or the frame ended (kind INTERRUPT),
 * or took an exception (EXCEPTION: TRAP at `pc`): after the instruction
 * ending at `clock`, before the one at `pc`, with these registers, once the
 * first `syncs_before` of the call's syncs were made. */
typedef struct nmd_live_pause {
    uint64_t clock;
    uint32_t pc;
    uint32_t d[8], a[8];
    uint32_t ccr;
    uint32_t kind;
    uint32_t syncs_before;
    uint32_t index[3];      /* 68000: index[0] = original advanced since prior pause. Z80: IX, IY, SP (d: A F B C D E H L, a: the alternates) */
} nmd_live_pause;

/* Run a call's C on its own stack. At each interrupt pause the C stops: where
 * a draft reaches the interrupted instruction with the original's registers
 * once it has made every hardware access the original made before (exact), or else before its first hardware access after the clock, or at
 * its end. At an exception pause it stops where it takes the exception
 * (nmd_live_exception); reaching past it without is NMD_LIVE_EXCEPTION.
 * nmd_live_continue goes on once the machine has taken it. The syncs are
 * made at their clocks, in time order with the pauses (68000 only). Returns 0 finished, 1 paused, 2 a difference (see the
 * result), -1 no room for another call. */
int nmd_live_start(int cpu, const nmd_live_slot *slots, uint32_t slot_count,
                   const nmd_live_sync_point *syncs, uint32_t sync_count,
                   const nmd_live_pause *pauses, uint32_t pause_count, void (*routine)(void),
                   int *handle);
int nmd_live_continue(int handle);
const nmd_live_result *nmd_live_result_of(int handle);
uint32_t nmd_live_syncs_made(int handle);
uint32_t nmd_live_pauses_made(int handle);
int nmd_live_at_end(int handle);        /* paused after the C's own return */
void nmd_live_release(int handle);

#endif
