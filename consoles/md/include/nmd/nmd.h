/*
 * nmd.h: entry points for a game built on nativemd.
 */
#ifndef NMD_NMD_H
#define NMD_NMD_H

#include "nmd/version.h"
#include "nmd/cart.h"
#include "nmd/hw.h"
#include "nmd/machine.h"
#include "nmd/z80.h"

enum {
    NMD_KIND_ROM,
    NMD_KIND_WORK_RAM,
    NMD_KIND_Z80_RAM,
    NMD_KIND_IO,
    NMD_KIND_VDP,
    NMD_KIND_SOUND,
    NMD_KIND_OPEN_BUS,
    NMD_KINDS
};

enum {
    NMD_Z80_KIND_RAM,
    NMD_Z80_KIND_YM2612,
    NMD_Z80_KIND_BANK_REGISTER,
    NMD_Z80_KIND_PSG,
    NMD_Z80_KIND_BANK_WINDOW,
    NMD_Z80_KIND_OPEN_BUS,
    NMD_Z80_KINDS
};

/* Additional work around each bus access, in tenths of the CPU's cycle. */
typedef struct nmd_cost_profile {
    int version;
    uint32_t m68k_tenths[2][NMD_KINDS];
    uint32_t z80_tenths[2][NMD_Z80_KINDS];
} nmd_cost_profile;

#define NMD_COST_PROFILE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

int nmd_main(const nmd_cart *cart, int argc, char **argv);
void nmd_power(const nmd_cart *cart);
void nmd_run_frame(void);
void nmd_set_cpu_costs(int on);
int nmd_access_kind(uint32_t addr);
int nmd_z80_access_kind(uint16_t addr);
/* Observation, since 1.1: the 65,536 bytes of work RAM and the 8,192 bytes
 * of Z80 RAM as they stand. Reading them takes no bus time and makes no
 * access the game could see; a game's own logic uses the bus calls. */
const uint8_t *nmd_work_ram(void);
const uint8_t *nmd_z80_ram(void);

#ifdef __cplusplus
}
#endif

#endif
