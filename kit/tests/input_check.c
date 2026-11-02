/* input_check: the kit's devices. Keys to positional bits and back to
   their names; a controller's stick past half on the d-pad's bits; a new
   press on a controller makes it its player's source, reported once; the
   mouse's motion summed over a frame then cleared; two touches at most,
   one ended taken out. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nk/fatal.h"
#include "nk/host.h"
#include "nk/input.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(70);
}
int host_pad_read(int index, host_pad *p) { (void)index; memset(p, 0, sizeof *p); return 0; }

static int fails, reports, last_player, last_kind;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "input_check: %s\n", #c); fails++; } } while (0)
static void hook(int player, const nk_input_source *s) { reports++; last_player = player; last_kind = s->kind; }

int main(void)
{
    host_pad now[NK_PLAYERS];
    int on[NK_PLAYERS] = { 0 };
    float tx[2], ty[2];
    memset(now, 0, sizeof now);
    nk_input_key('Z', 1);
    nk_input_key(0x26, 1);
    CHECK(nk_input_keyboard_bits() == (1u << 0 | 1u << 12));
    CHECK(!strcmp(nk_input_key_name(1u << 0), "Z"));
    CHECK(!strcmp(nk_input_key_name(1u << 6), "Right Shift"));
    nk_input_report(hook);
    CHECK(reports == 1 && last_player == 0 && last_kind == NK_INPUT_KEYBOARD);
    nk_input_focus_lost();
    CHECK(nk_input_keyboard_bits() == 0 && !nk_input_key_held('Z'));

    now[1].x = 0.75f;
    now[1].buttons = 1u << 1;
    now[1].style = HOST_PAD_PLAYSTATION;
    on[1] = 1;
    CHECK(nk_pad_bits(&now[1]) == (1u << 1 | 1u << 13));
    nk_input_pads(now, on);
    nk_input_report(hook);
    CHECK(reports == 2 && last_player == 1 && last_kind == NK_INPUT_GAMEPAD);
    nk_input_pads(now, on);                 /* held, not pressed again */
    nk_input_report(hook);
    CHECK(reports == 2);
    CHECK(nk_input_pad(1) && nk_input_pad(1)->style == HOST_PAD_PLAYSTATION && !nk_input_pad(2));

    nk_input_mouse(10, 20, 3, 4, 1);
    nk_input_mouse(11, 21, 2, -1, 1);
    CHECK(nk_input_mouse_now()->dx == 5 && nk_input_mouse_now()->dy == 3 && nk_input_mouse_now()->seen);
    nk_input_mouse_frame_done();
    CHECK(nk_input_mouse_now()->dx == 0 && !nk_input_mouse_now()->seen && nk_input_mouse_now()->x == 11);

    nk_input_touch(7, 0, 1, 1);
    nk_input_touch(8, 0, 2, 2);
    nk_input_touch(9, 0, 3, 3);              /* a third: not kept */
    CHECK(nk_input_touches(tx, ty) == 2);
    nk_input_touch(7, 2, 0, 0);
    CHECK(nk_input_touches(tx, ty) == 1 && tx[0] == 2);
    if (fails)
        return 1;
    printf("input_check: ok\n");
    return 0;
}
