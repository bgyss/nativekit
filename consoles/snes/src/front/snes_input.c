/*
 * snes_input.c: the native controls as the console's controllers, read
 * from the kit's devices (kit/include/nk/input.h). Whatever is
 * plugged into the controller ports (ports.c), the player's own devices
 * drive it:
 *
 *   joypad      the keyboard (player 0) and game controller N (player N)
 *   multitap    its four players, each a game controller (the keyboard
 *               player 0's too)
 *   mouse       the mouse's motion and buttons
 *   Super Scope the pointer, or a touch, where it aims; the left button
 *               (or the touch) the trigger, the right the cursor button,
 *               the middle (or T) the turbo switch, P the pause
 *   Justifier   the pointer or the first touch gun 0, a second touch gun
 *               1; the left button (or the touch) the trigger, the right
 *               (or Return) start
 *
 * The players are the controllers in port order, each port's device one
 * player (a multitap four): port 0's first. For each, the game can ask
 * which native device is driving it now (the last one used: a keyboard, a
 * game controller and its family, a mouse, a touch screen, a script) and
 * the name of the control that makes each of its buttons, and hear when
 * that changes (nshvc_hooks.input), to show the player the right names.
 *
 *   SNES joypad  keyboard          controller (by position)
 *   D-pad        arrows            d-pad or left stick
 *   B, A         Z, X              bottom, right face buttons
 *   Y, X         A, S              left, top face buttons
 *   L, R         Q, W              shoulders
 *   START        Return            menu
 *   SELECT       Right Shift, Tab  view
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "nshvc/nshvc.h"
#include "nk/host.h"
#include "nk/input.h"
#include "snes_input.h"

#define PLAYERS NK_PLAYERS

_Static_assert(sizeof(nshvc_input_source) == sizeof(nk_input_source), "the same source");
_Static_assert(offsetof(nshvc_input_source, name) == offsetof(nk_input_source, name), "the same source");
_Static_assert((int)NSHVC_INPUT_SCRIPT == (int)NK_INPUT_SCRIPT && (int)NSHVC_PAD_NINTENDO == (int)NK_PAD_NINTENDO,
               "the same kinds");

static int pad_bit_of(uint16_t button);

int nshvc_ext_key_for(unsigned control)
{
    int bit = control <= 0xFFFFu ? pad_bit_of((uint16_t)control) : -1;
    return bit < 0 ? 0 : nk_input_key_for(1u << bit);
}

void nshvc_ext_key_bind(unsigned control, int vk)
{
    int bit = control <= 0xFFFFu ? pad_bit_of((uint16_t)control) : -1;
    if (bit >= 0)
        nk_input_key_bind(1u << bit, vk);
}

const char *nshvc_ext_key_name(int vk) { return nk_key_name(vk); }

/* ---- the keyboard ---- */
/* T and P, the Super Scope's turbo and pause, mark the keyboard as player
   0's too (the kit's nk_input_key the rest) */
int snes_input_key(int vk, int down)
{
    if (down && (vk == 'T' || vk == 'P'))
        nk_input_use(0, NK_INPUT_KEYBOARD, NULL);
    return 0;
}

/* ---- the game controllers ---- */
/* positional bits (kit/include/nk/input.h) as the joypad's */
static uint16_t joypad_of(unsigned b)
{
    uint16_t j = 0;
    if (b & 1u << 0) j |= NSHVC_B;
    if (b & 1u << 1) j |= NSHVC_A;
    if (b & 1u << 2) j |= NSHVC_Y;
    if (b & 1u << 3) j |= NSHVC_X;
    if (b & 1u << 4) j |= NSHVC_L;
    if (b & 1u << 5) j |= NSHVC_R;
    if (b & 1u << 6) j |= NSHVC_SELECT;
    if (b & 1u << 7) j |= NSHVC_START;
    if (b & 1u << 12) j |= NSHVC_UP;
    if (b & 1u << 13) j |= NSHVC_RIGHT;
    if (b & 1u << 14) j |= NSHVC_DOWN;
    if (b & 1u << 15) j |= NSHVC_LEFT;
    return j;
}

/* the controller bit each joypad button comes from (-1: the stick) */
static int pad_bit_of(uint16_t button)
{
    switch (button) {
    case NSHVC_B: return 0;
    case NSHVC_A: return 1;
    case NSHVC_Y: return 2;
    case NSHVC_X: return 3;
    case NSHVC_L: return 4;
    case NSHVC_R: return 5;
    case NSHVC_SELECT: return 6;
    case NSHVC_START: return 7;
    case NSHVC_UP: return 12;
    case NSHVC_RIGHT: return 13;
    case NSHVC_DOWN: return 14;
    case NSHVC_LEFT: return 15;
    }
    return -1;
}

/* a controller's button names by family, where the controller gives none */
static const char *pad_default_label(int style, int bit)
{
    static const char *const xbox[16] = { "A", "B", "X", "Y", "LB", "RB", "View", "Menu", "LS", "RS", "LT", "RT",
                                          "D-pad Up", "D-pad Right", "D-pad Down", "D-pad Left" };
    static const char *const ps[16] = { "Cross", "Circle", "Square", "Triangle", "L1", "R1", "Create", "Options",
                                        "L3", "R3", "L2", "R2", "D-pad Up", "D-pad Right", "D-pad Down",
                                        "D-pad Left" };
    static const char *const nin[16] = { "B", "A", "Y", "X", "L", "R", "-", "+", "Left Stick", "Right Stick", "ZL",
                                         "ZR", "D-pad Up", "D-pad Right", "D-pad Down", "D-pad Left" };
    static const char *const gen[16] = { "Bottom", "Right", "Left", "Top", "Left Shoulder", "Right Shoulder",
                                         "Back", "Start", "Left Stick", "Right Stick", "Left Trigger",
                                         "Right Trigger", "D-pad Up", "D-pad Right", "D-pad Down", "D-pad Left" };
    const char *const *t = style == NSHVC_PAD_XBOX ? xbox : style == NSHVC_PAD_PLAYSTATION ? ps
                           : style == NSHVC_PAD_NINTENDO ? nin : gen;
    return bit >= 0 && bit < 16 ? t[bit] : "";
}

/* ---- the players and the ports ---- */
/* the port and device player `player` is on (-1 none), its slot on a
   multitap */
static int player_port(int player, int *device, int *slot)
{
    int p, first = 0;
    for (p = 0; p < 2; p++) {
        int d = nshvc_plugged(p), n = d == NSHVC_PORT_MULTITAP ? 4 : 1;
        if (player < first + n) {
            *device = d;
            *slot = player - first;
            return p;
        }
        first += n;
    }
    return -1;
}

static void gun_aim(int gun, float x, float y, int *dot, int *line)
{
    (void)gun;
    if (!console_picture_at(x, y, dot, line)) {
        *dot = -1;
        *line = -1;
    }
}

/* each frame: the players' devices to the console's controllers (now, on:
   the controllers' states given, a check's own; NULL: read from the host) */
void snes_input_frame(const host_pad *now, const int *on, const uint16_t *game_buttons)
{
    int player, p, k, touches;
    uint16_t joy[PLAYERS] = { 0 };
    const nk_mouse *m;
    float touch_x[2], touch_y[2];
    nk_input_pads(now, on);
    for (k = 0; k < PLAYERS; k++)
        joy[k] = nk_input_pad(k) ? joypad_of(nk_pad_bits(nk_input_pad(k))) : 0;
    joy[0] |= joypad_of(nk_input_keyboard_bits());
    m = nk_input_mouse_now();
    touches = nk_input_touches(touch_x, touch_y);
    for (player = 0; player < PLAYERS; player++) {
        int device, slot;
        p = player_port(player, &device, &slot);
        if (p < 0)
            break;
        switch (device) {
        case NSHVC_PORT_JOYPAD:
        case NSHVC_PORT_MULTITAP:
            nshvc_set_joypad(player, (uint16_t)(joy[player] | game_buttons[player]));
            break;
        case NSHVC_PORT_MOUSE:
            nshvc_set_mouse(p, (int)m->dx, (int)m->dy, m->buttons & 3);
            if (m->seen)
                nk_input_use(player, NK_INPUT_MOUSE, NULL);
            break;
        case NSHVC_PORT_SUPERSCOPE:
        case NSHVC_PORT_JUSTIFIER:
        case NSHVC_PORT_JUSTIFIERS: {
            int g, x, y;
            for (g = 0; g < (device == NSHVC_PORT_JUSTIFIERS ? 2 : 1); g++) {
                unsigned b = 0;
                if (g < touches) {
                    gun_aim(g, touch_x[g], touch_y[g], &x, &y);
                    b = NSHVC_GUN_TRIGGER;
                    nk_input_use(player, NK_INPUT_TOUCH, NULL);
                } else if (g == 0) {
                    gun_aim(g, m->x, m->y, &x, &y);
                    if (m->buttons & 1) b |= NSHVC_GUN_TRIGGER;
                    if (device == NSHVC_PORT_SUPERSCOPE) {
                        if (m->buttons & 2) b |= NSHVC_GUN_CURSOR;
                        if ((m->buttons & 4) || nk_input_key_held('T')) b |= NSHVC_GUN_TURBO;
                        if (nk_input_key_held('P')) b |= NSHVC_GUN_PAUSE;
                    } else if ((m->buttons & 2) || nk_input_key_held(0x0D)) {
                        b |= NSHVC_GUN_START;
                    }
                    if (m->seen)
                        nk_input_use(player, NK_INPUT_MOUSE, NULL);
                } else {
                    x = y = -1;
                }
                nshvc_set_gun(g, x, y, b);
            }
            break;
        }
        }
    }
    nk_input_mouse_frame_done();
}

/* the players whose device changed, to the game's hook */
static void (*report_hook)(int player, const nshvc_input_source *src);

static void report(int player, const nk_input_source *s)
{
    if (report_hook)
        report_hook(player, (const nshvc_input_source *)s);
}

void snes_input_report(void (*hook)(int player, const nshvc_input_source *src))
{
    report_hook = hook;
    nk_input_report(report);
}

/* ---- nshvc.h ---- */
int nshvc_player_source(int player, nshvc_input_source *src)
{
    int device, slot;
    if (player < 0 || player >= PLAYERS || player_port(player, &device, &slot) < 0) {
        memset(src, 0, sizeof *src);
        return 0;
    }
    memcpy(src, nk_input_source_of(player), sizeof *src);
    return 1;
}

const char *nshvc_player_label(int player, unsigned control)
{
    int device, slot;
    const nshvc_input_source *s;
    if (player < 0 || player >= PLAYERS || player_port(player, &device, &slot) < 0)
        return "";
    s = (const nshvc_input_source *)nk_input_source_of(player);
    if (device == NSHVC_PORT_MOUSE)
        return control == 1 ? "Left button" : control == 2 ? "Right button" : "";
    if (device >= NSHVC_PORT_SUPERSCOPE) {
        if (s->kind == NSHVC_INPUT_TOUCH)
            return control == NSHVC_GUN_TRIGGER ? "Tap" : "";
        switch (control) {
        case NSHVC_GUN_TRIGGER: return "Left button";
        case NSHVC_GUN_CURSOR: return "Right button";
        case NSHVC_GUN_TURBO: return "Middle button, T";
        case NSHVC_GUN_PAUSE: return "P";
        case NSHVC_GUN_START: return "Right button, Return";
        }
        return "";
    }
    if (s->kind == NSHVC_INPUT_GAMEPAD && nk_input_pad(player)) {
        int bit = pad_bit_of((uint16_t)control);
        if (bit >= 0 && nk_input_pad(player)->label[bit][0])
            return nk_input_pad(player)->label[bit];
        return pad_default_label(s->style, bit);
    }
    if (player == 0 && control <= 0xFFFFu) {
        int bit = pad_bit_of((uint16_t)control);
        return bit >= 0 ? nk_input_key_name(1u << bit) : "";
    }
    return "";
}
