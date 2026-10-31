/*
 * snes_console.h: the SNES as nativekit's console (kit/include/nk/nk.h).
 */
#ifndef NSHVC_SNES_CONSOLE_H
#define NSHVC_SNES_CONSOLE_H

#include "nk.h"

extern const nk_console snes_console;

/* the picture a shot holds (console.c): nshvc_picture's, or with
   NSHVC_SCALE the frame presented at that scale (*presented 1) */
const uint32_t *front_shot_picture(int *w, int *h, int *presented);

#endif
