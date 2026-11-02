/*
 * rr_soft_ext.h: the software backend's target, for a console's own
 * drawing onto it.
 */
#ifndef NK_RR_SOFT_EXT_H
#define NK_RR_SOFT_EXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the target's pixels (0xAARRGGBB, top row first) and its size */
uint32_t *rr_soft_target(int *w, int *h);

#ifdef __cplusplus
}
#endif
#endif
