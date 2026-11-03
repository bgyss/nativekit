/* The Z80-visible state and routine model for a Mega Drive sound driver. */
#ifndef NMD_Z80_H
#define NMD_Z80_H

#include <stdint.h>

typedef struct nmd_z80_state {
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t i, r, iff1, iff2, im;
    uint16_t ix, iy, sp, pc;
} nmd_z80_state;

typedef struct nmd_z80_routine {
    uint16_t addr;
    void (*abi)(void);
    const char *name;
} nmd_z80_routine;

extern nmd_z80_state nmd_z80;

/* Byte-wide sound-CPU memory and I/O bus operations. */
uint8_t zrd(uint16_t addr);
void zwr(uint16_t addr, uint8_t value);
uint8_t zin(uint8_t port);
void zout(uint8_t port, uint8_t value);

/* Restart the sound CPU at a C routine from the cartridge's table. The
 * routine must not return; helper functions use the host C stack. */
void nmd_z80_restart(uint16_t addr);

/* Wait for a hardware condition, charging one Z80 T-state per poll. */
uint64_t nmd_z80_wait_until(int (*ready)(void *ctx), void *ctx);

#endif
