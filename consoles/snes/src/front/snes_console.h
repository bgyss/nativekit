/*
 * snes_console.h: the SNES as nativekit's console (kit/include/nk/nk.h).
 */
#ifndef NSHVC_SNES_CONSOLE_H
#define NSHVC_SNES_CONSOLE_H

#include <stdint.h>

#include "nk/nk.h"

extern const nk_console snes_console;

/* the picture a shot holds (snes_present.c): nshvc_picture's, or with
   NSHVC_SCALE the frame presented at that scale (*presented 1) */
const uint32_t *front_shot_picture(int *w, int *h, int *presented);

/* snes_present.c: the table's picture */
void snes_present(const int d[4]);
double snes_aspect(void);
void snes_scaled(int w, int h);
int snes_rrcheck(const int d[4], uint32_t *want, int tw, int th);
void snes_rrcheck_why(int X, int Y);
void snes_at_exit(void);

/* console.c: the table's controllers and the game's hooks */
void snes_input(int headless, const uint32_t *b);
int snes_key(int vk, int down);
int snes_pointer(float x, float y, int buttons);
void snes_touch(int id, int phase, float x, float y);
void snes_game_frame(void);

#endif
