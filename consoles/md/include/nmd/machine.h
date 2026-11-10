/*
 * machine.h: the 68000-visible base Mega Drive machine.
 */
#ifndef NMD_MACHINE_H
#define NMD_MACHINE_H

#include <stdint.h>

#include "nmd/version.h"

typedef struct nmd_cpu_state {
    uint32_t d[8];
    uint32_t a[8];
    uint32_t pc;
    uint16_t sr;
    /* Since 1.1: the stack pointer of the mode the 68000 is not in. A7 is
     * the supervisor's while SR's S bit is set and usp holds the user's;
     * in user mode A7 is the user's and ssp holds the supervisor's. */
    uint32_t usp;
    uint32_t ssp;
} nmd_cpu_state;

extern nmd_cpu_state nmd_cpu;

uint8_t rd8(uint32_t addr);
uint16_t rd16(uint32_t addr);
uint32_t rd32(uint32_t addr);
void wr8(uint32_t addr, uint8_t value);
void wr16(uint32_t addr, uint16_t value);
void wr32(uint32_t addr, uint32_t value);
uint8_t nmd_tas(uint32_t addr);

uint64_t nmd_wait_until(int (*ready)(void *ctx), void *ctx);
void nmd_wait_vblank(void);
void nmd_stop(void);

/* Since 1.1. Load the status register as MOVE to SR, RTE or RTR's CCR do:
 * a change of the S bit exchanges A7 with usp/ssp, and an interrupt the new
 * mask admits is taken before the call returns. */
void nmd_set_sr(uint16_t sr);
/* Native execution only: leave every C frame of the 68000's run and enter
 * the cartridge's routine at addr, as a jump to code that never returns to
 * its caller does (a soft reset, an error screen). Registers are unchanged. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void nmd_restart(uint32_t addr);

#endif
