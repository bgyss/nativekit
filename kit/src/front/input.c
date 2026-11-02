/*
 * input.c: the players' native devices (input.h). The keyboard (player
 * 0's), the game controllers (controller N player N's), the mouse and up
 * to two touches, read by the console each frame onto its own
 * controllers; and for each player the native device driving it now (the
 * last one used: a keyboard, a game controller and its family, a mouse, a
 * touch screen, a script), reported when that changes so a game can show
 * the player the right names.
 *
 *   positional bit   keyboard          controller
 *   d-pad            arrows            d-pad or left stick
 *   bottom, right    Z, X              bottom, right face buttons
 *   left, top        A, S              left, top face buttons
 *   LB, RB           Q, W              shoulders
 *   menu             Return            menu
 *   view             Right Shift, Tab  view
 */
#include <stdio.h>
#include <string.h>

#include "host.h"
#include "input.h"

/* ---- what drives each player ---- */
static nk_input_source source[NK_PLAYERS];
static int source_changed[NK_PLAYERS];
static host_pad pads[NK_PLAYERS];
static int pad_on[NK_PLAYERS];

void nk_input_use(int player, int kind, const host_pad *pad)
{
    nk_input_source s;
    if (player < 0 || player >= NK_PLAYERS)
        return;
    memset(&s, 0, sizeof s);
    s.kind = kind;
    if (pad) {
        s.style = pad->style == HOST_PAD_XBOX ? NK_PAD_XBOX
                  : pad->style == HOST_PAD_PLAYSTATION ? NK_PAD_PLAYSTATION
                  : pad->style == HOST_PAD_NINTENDO ? NK_PAD_NINTENDO : NK_PAD_GENERIC;
        snprintf(s.name, sizeof s.name, "%s", pad->name);
    } else {
        snprintf(s.name, sizeof s.name, "%s",
                 kind == NK_INPUT_KEYBOARD ? "Keyboard" : kind == NK_INPUT_MOUSE ? "Mouse"
                 : kind == NK_INPUT_TOUCH ? "Touch screen" : kind == NK_INPUT_SCRIPT ? "Script" : "");
    }
    if (memcmp(&s, &source[player], sizeof s)) {
        source[player] = s;
        source_changed[player] = 1;
    }
}

const nk_input_source *nk_input_source_of(int player)
{
    return player >= 0 && player < NK_PLAYERS ? &source[player] : NULL;
}

/* ---- the keyboard ---- */
static uint32_t keys;              /* the positional bits held */
static unsigned char held[256];    /* the virtual keys held */

static const nk_key *k_map;
static int k_n;

void nk_input_keymap(const nk_key *map, int n) { k_map = map; k_n = n; }

void nk_input_key(int vk, int down)
{
    int i;
    uint32_t m = 0;
    for (i = 0; i < k_n; i++)
        if (k_map[i].vk == vk)
            m |= k_map[i].bit;
    if (vk >= 0 && vk < 256)
        held[vk] = (unsigned char)(down != 0);
    if (down)
        keys |= m;
    else
        keys &= ~m;
    if (down && m)
        nk_input_use(0, NK_INPUT_KEYBOARD, NULL);
}

int nk_input_key_held(int vk)
{
    return vk >= 0 && vk < 256 && held[vk];
}

uint32_t nk_input_keyboard_bits(void)
{
    return keys;
}

const char *nk_input_key_name(uint32_t bit_mask)
{
    int i;
    for (i = 0; i < k_n; i++)
        if (k_map[i].bit == bit_mask)
            return k_map[i].name;
    return "";
}

void nk_input_focus_lost(void)
{
    keys = 0;
    memset(held, 0, sizeof held);
}

/* ---- the mouse and touches ---- */
static nk_mouse mouse;
static struct { int id; float x, y; } touch[2];
static int touches;

void nk_input_mouse(float x, float y, float dx, float dy, int buttons)
{
    mouse.x = x;
    mouse.y = y;
    mouse.dx += dx;
    mouse.dy += dy;
    if (buttons & ~mouse.buttons || dx || dy)
        mouse.seen = 1;
    mouse.buttons = buttons;
}

const nk_mouse *nk_input_mouse_now(void)
{
    return &mouse;
}

void nk_input_mouse_frame_done(void)
{
    mouse.dx = mouse.dy = 0;
    mouse.seen = 0;
}

void nk_input_touch(int id, int phase, float x, float y)
{
    int i;
    for (i = 0; i < touches; i++)
        if (touch[i].id == id)
            break;
    if (phase == 2) {
        if (i < touches) {
            touch[i] = touch[touches - 1];
            touches--;
        }
        return;
    }
    if (i == touches) {
        if (touches == 2)
            return;
        touches++;
    }
    touch[i].id = id;
    touch[i].x = x;
    touch[i].y = y;
}

int nk_input_touches(float x[2], float y[2])
{
    int i;
    for (i = 0; i < touches; i++) {
        x[i] = touch[i].x;
        y[i] = touch[i].y;
    }
    return touches;
}

/* ---- the game controllers ---- */
unsigned nk_pad_bits(const host_pad *p)
{
    unsigned b = p->buttons & 0xFFu;
    if ((p->buttons & 1u << 12) || p->y < -0.5f) b |= 1u << 12;
    if ((p->buttons & 1u << 13) || p->x > 0.5f) b |= 1u << 13;
    if ((p->buttons & 1u << 14) || p->y > 0.5f) b |= 1u << 14;
    if ((p->buttons & 1u << 15) || p->x < -0.5f) b |= 1u << 15;
    return b;
}

void nk_input_pads(const host_pad *now, const int *on)
{
    host_pad read[NK_PLAYERS];
    int got[NK_PLAYERS], k;
    if (!now) {
        for (k = 0; k < NK_PLAYERS; k++)
            got[k] = host_pad_read(k, &read[k]);
        now = read;
        on = got;
    }
    for (k = 0; k < NK_PLAYERS; k++) {
        unsigned was = pad_on[k] ? nk_pad_bits(&pads[k]) : 0, was_b = pad_on[k] ? pads[k].buttons : 0;
        pad_on[k] = on[k];
        if (on[k])
            pads[k] = now[k];
        if (pad_on[k] && ((nk_pad_bits(&pads[k]) & ~was) || (pads[k].buttons & ~was_b)))
            nk_input_use(k, NK_INPUT_GAMEPAD, &pads[k]);
    }
}

const host_pad *nk_input_pad(int k)
{
    return k >= 0 && k < NK_PLAYERS && pad_on[k] ? &pads[k] : NULL;
}

/* ---- the players ---- */
/* a headless run's: the script drives the controllers */
void nk_input_script(void)
{
    int k;
    for (k = 0; k < NK_PLAYERS; k++)
        nk_input_use(k, NK_INPUT_SCRIPT, NULL);
}

/* the players whose device changed, to the hook */
void nk_input_report(void (*hook)(int player, const nk_input_source *src))
{
    int k;
    for (k = 0; k < NK_PLAYERS; k++)
        if (source_changed[k]) {
            source_changed[k] = 0;
            if (hook)
                hook(k, &source[k]);
        }
}

/* (x, y) in the window's points to the console's column and row: the
   picture drawn at at[] (left, top, right, bottom) on a target `scale`
   pixels a point, `width` x `height` with the console's column 0 at
   column `left` and its first row at row `top`; 0 off the picture */
int nk_input_point(const int *at, float scale, int width, int height, int left, int top, float x, float y, int *col,
                   int *row)
{
    double u, v;
    if (at[2] <= at[0] || at[3] <= at[1])
        return 0;
    u = ((double)x * scale - at[0]) / (at[2] - at[0]);
    v = ((double)y * scale - at[1]) / (at[3] - at[1]);
    if (u < 0 || u >= 1 || v < 0 || v >= 1)
        return 0;
    *col = (int)(u * width) - left;
    *row = (int)(v * height) - top;
    return 1;
}
