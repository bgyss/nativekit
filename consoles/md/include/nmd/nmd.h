/*
 * nmd.h: entry points for a game built on nativemd.
 */
#ifndef NMD_NMD_H
#define NMD_NMD_H

#include "nmd/cart.h"
#include "nmd/machine.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmd_main(const nmd_cart *cart, int argc, char **argv);
void nmd_power(const nmd_cart *cart);
void nmd_run_frame(void);

#ifdef __cplusplus
}
#endif

#endif
