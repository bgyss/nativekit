/*
 * cart.h: the base Mega Drive cartridge and its native routine table.
 */
#ifndef NMD_CART_H
#define NMD_CART_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    NMD_NTSC = 0,
    NMD_PAL = 1
} nmd_region;

typedef struct nmd_routine {
    uint32_t addr;
    void (*abi)(void);
    const char *name;
} nmd_routine;

typedef struct nmd_cart {
    const char *title;
    nmd_region region;
    const uint8_t *rom;
    uint32_t rom_size;
    const nmd_routine *routines;
    size_t routine_count;
} nmd_cart;

#endif
