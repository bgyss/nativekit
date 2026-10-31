/*
 * routines.h: the routines with C a console reaches by address (a vector,
 * a computed call or jump), in a table sorted by address. Each entry's
 * first member is its address, a uint32_t; the rest is the console's.
 */
#ifndef NK_ROUTINES_H
#define NK_ROUTINES_H

#include <stddef.h>
#include <stdint.h>

/* the entry of `count` entries `stride` bytes apart at `base` whose
   address is addr; NULL when none is */
const void *nk_routine_find(const void *base, int count, size_t stride, uint32_t addr);

#endif
