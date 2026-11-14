/* live.h: the lockstep runner's side of the live hybrid library (test-only). */
#ifndef NMD_CHECK_LIVE_H
#define NMD_CHECK_LIVE_H

#include <stdint.h>

enum {
    NMD_LIVE_OK = 0,
    NMD_LIVE_ORDER = 1,         /* hardware access differs from the original's next one */
    NMD_LIVE_PAST_RECORD = 2,   /* hardware access after the original's last */
    NMD_LIVE_ODD = 3,
    NMD_LIVE_UNSUPPORTED = 4
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

void nmd_live_bind(nmd_live_m68k_bus m68k, nmd_live_z80_bus z80, uint32_t image_size);
/* The machine syncs the original made between instructions inside the call:
 * each is made before the first hardware access that begins at or after its
 * clock (68000 only); the runner makes those left after the C returns. */
void nmd_live_syncs(nmd_live_sync sync, const nmd_live_sync_point *points, uint32_t count);
uint32_t nmd_live_syncs_made(void);
void nmd_live_begin(int cpu, const nmd_live_slot *list, uint32_t count);
const nmd_live_result *nmd_live_run(int cpu, void (*routine)(void));

#endif
