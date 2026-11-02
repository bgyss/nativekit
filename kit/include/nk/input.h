/*
 * input.h: the players' native devices (input.c): the keyboard, the game
 * controllers, the mouse and touches, and which of them drives each player
 * now. A console maps them onto its own controllers.
 *
 * The keyboard and a controller give positional bits, host_pad's (host.h):
 *
 *   bit     controller        keyboard
 *   0       bottom face       Z
 *   1       right face        X
 *   2       left face         A
 *   3       top face          S
 *   4, 5    shoulders         Q, W
 *   6       view              Right Shift, Tab
 *   7       menu              Return
 *   12-15   d-pad up, right,  arrows
 *           down, left (the
 *           left stick past
 *           half sets these
 *           too)
 */
#ifndef NK_INPUT_H
#define NK_INPUT_H

#include "host.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "nk.h"

/* what drives a player: its kind, a controller's family and the device's
   name */
enum { NK_INPUT_NONE, NK_INPUT_KEYBOARD, NK_INPUT_GAMEPAD, NK_INPUT_MOUSE, NK_INPUT_TOUCH, NK_INPUT_SCRIPT };
enum { NK_PAD_GENERIC, NK_PAD_XBOX, NK_PAD_PLAYSTATION, NK_PAD_NINTENDO };
typedef struct { int kind; int style; char name[64]; } nk_input_source;

/* positional bits, host_pad's (host.h): 0 bottom 1 right 2 left 3 top 4 LB 5 RB 6 view 7 menu,
   12-15 d-pad up, right, down, left (the left stick past half sets these too) */
unsigned nk_pad_bits(const host_pad *p);

/* ---- the keyboard ---- */
void nk_input_key(int vk, int down);
int  nk_input_key_held(int vk);
unsigned nk_input_keyboard_bits(void);       /* the keys held, as positional bits */
const char *nk_input_key_name(unsigned bit_mask); /* the first key for a positional bit ("Z"), "" none */
void nk_input_focus_lost(void);

/* ---- the mouse and touches ---- */
void nk_input_mouse(float x, float y, float dx, float dy, int buttons);
void nk_input_touch(int id, int phase, float x, float y);
typedef struct { float x, y, dx, dy; int buttons, seen; } nk_mouse;
const nk_mouse *nk_input_mouse_now(void);
void nk_input_mouse_frame_done(void);         /* dx, dy and seen cleared */
int  nk_input_touches(float x[2], float y[2]);

/* ---- the game controllers and the players ---- */
/* the controllers read for this frame (now/on given: a check's own) */
void nk_input_pads(const host_pad *now, const int *on);
const host_pad *nk_input_pad(int k);         /* controller k, NULL when not connected */
/* player `player` now driven by a device of `kind` (a controller: pad) */
void nk_input_use(int player, int kind, const host_pad *pad);
void nk_input_script(void);                  /* a headless run: the script drives every player */
/* the players whose device changed since the last, each to hook */
void nk_input_report(void (*hook)(int player, const nk_input_source *src));
const nk_input_source *nk_input_source_of(int player);
/* (x, y) in the window's points to the console's column and row: the
   picture drawn at at[] (left, top, right, bottom) on a target `scale`
   pixels a point, `width` x `height` with the console's column 0 at
   column `left` and its first row at row `top`; 0 off the picture */
int  nk_input_point(const int *at, float scale, int width, int height, int left, int top, float x, float y,
                    int *col, int *row);

#ifdef __cplusplus
}
#endif

#endif
