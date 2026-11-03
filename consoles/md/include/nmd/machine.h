/*
 * machine.h: the 68000-visible base Mega Drive machine.
 */
#ifndef NMD_MACHINE_H
#define NMD_MACHINE_H

#include <stdint.h>

typedef struct nmd_cpu_state {
    uint32_t d[8];
    uint32_t a[8];
    uint32_t pc;
    uint16_t sr;
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

#endif
