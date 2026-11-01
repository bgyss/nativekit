/*
 * snes_input.h: the native controls as the console's controllers
 * (snes_input.c), read from the kit's devices (kit/include/nk/input.h).
 */
#ifndef NSHVC_SNES_INPUT_H
#define NSHVC_SNES_INPUT_H

#include <stdint.h>

#include "nshvc/nshvc.h"
#include "host.h"

/* a key the console reads itself (T, P: the Super Scope's), before the
   kit's nk_input_key; 0: the key goes on to the kit */
int snes_input_key(int vk, int down);
/* each frame: the players' devices to the controllers (game_buttons: what
   the game holds for each player, nshvc_game_buttons); now and on[k] (the
   game controllers' states, controller k connected) given by a check, or
   NULL to read the host's */
void snes_input_frame(const host_pad *now, const int *on, const uint16_t *game_buttons);
/* the players whose device changed since the last, each to hook */
void snes_input_report(void (*hook)(int player, const nshvc_input_source *src));

/* snes_present.c: the console's dot and line shown at (x, y) in the window's
   points; 0 off the picture */
int console_picture_at(float x, float y, int *dot, int *line);

#endif
