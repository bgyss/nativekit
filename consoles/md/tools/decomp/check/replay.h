/* replay.h: the per-call checker's side of the hybrid library (test-only). */
#ifndef NMD_CHECK_REPLAY_H
#define NMD_CHECK_REPLAY_H

#include <stdint.h>

enum { NMD_CHECK_M68K = 1, NMD_CHECK_Z80 = 2 };
enum { NMD_CHECK_WORK = 0, NMD_CHECK_ZRAM = 1 };

/* Why a replay stopped. Every value but OK is a difference from the record. */
enum {
    NMD_CHECK_OK = 0,
    NMD_CHECK_ACCESS = 1,       /* hardware access differs in address, width or direction */
    NMD_CHECK_VALUE = 2,        /* hardware write of another value */
    NMD_CHECK_PAST_RECORD = 3,  /* hardware access after the original's last */
    NMD_CHECK_UNKNOWN_READ = 4, /* RAM read the original did not make before writing */
    NMD_CHECK_ROM_WRITE = 5,
    NMD_CHECK_UNMAPPED = 6,
    NMD_CHECK_ODD = 7,          /* word access at an odd address */
    NMD_CHECK_UNSUPPORTED = 8,  /* a wait or restart a per-call check cannot judge */
    NMD_CHECK_NO_ROUTINE = 9,   /* a call to an address with no C and no recorded call */
    NMD_CHECK_RUNAWAY = 10      /* far more bus accesses than the original made */
};

typedef struct nmd_check_access {
    uint32_t address;
    uint32_t value;
    uint8_t width;
    uint8_t write;
} nmd_check_access;

/* A routine entry the C made: one of the tables' routines, or the original
 * standing in (fallback). The first is the routine under check itself. */
typedef struct nmd_check_call {
    uint32_t address;
    uint8_t cpu;
    uint8_t fallback;
} nmd_check_call;

typedef struct nmd_check_result {
    int status;
    uint32_t address, width, value;
    int write;
    uint32_t hardware_index;    /* the record position at the difference */
    uint32_t hardware_consumed;
    uint32_t accesses;          /* bus calls the C made */
    uint32_t fallbacks;         /* nested calls the record stood in for */
} nmd_check_result;

/* The checker supplies this to stand in for a nested call that has no C:
 * it applies the recorded call's effects and returns nonzero, or returns 0
 * when the record holds no such call at this point. */
typedef int (*nmd_check_fallback)(int cpu, uint32_t address);

void nmd_check_reset(const uint8_t *image, uint32_t size);
void nmd_check_entry_byte(int space, uint32_t offset, uint8_t value);
int nmd_check_exit_byte(int space, uint32_t offset);   /* -1: the C did not write it */
void nmd_check_hardware(int cpu, const nmd_check_access *list, uint32_t count);
void nmd_check_set_fallback(nmd_check_fallback handler);
void nmd_check_access_limit(uint32_t limit);
uint32_t nmd_check_hardware_next(void);
void nmd_check_hardware_skip(uint32_t count);
void nmd_check_ram_store(int space, uint32_t offset, uint8_t value);
const nmd_check_result *nmd_check_run(void (*routine)(void));
uint32_t nmd_check_calls(const nmd_check_call **list);
const uint32_t *nmd_check_written(int space, uint32_t *count);   /* RAM offsets the C wrote */

/* For the game's C: call the routine at a guest address that has no C in
 * this build. In the hybrid the record stands in and the call is counted. */
void nmd_check_call_fallback(int cpu, uint32_t address);

#endif
