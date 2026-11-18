/* session_console: a console for the recorder's checks (session_check.py):
   its machine a xorshift sequence the players' buttons stir each frame, its
   picture and memories made from it. Windowed on the null host it reads the
   keyboard (PORT_EVENTS gives keys); headless, the script's buttons.

   SESSION_NONDET=1 plants an error: the host's clock stirs the machine, so a
   replay cannot match the play. SESSION_QUIT=N ends a windowed run at the
   Nth pass of the loop (default 400). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nk/fatal.h"
#include "nk/host.h"
#include "nk/input.h"
#include "nk/nk.h"
#include "nk/session.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "session_console: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(70);
}

static uint32_t state = 1, frames, players[2];
static uint32_t pic[16 * 8];
static int nondet;
static long quit_at = 400;

static void power(const void *cart)
{
    (void)cart;
    nondet = getenv("SESSION_NONDET") != NULL;
    if (getenv("SESSION_QUIT"))
        quit_at = atol(getenv("SESSION_QUIT"));
}

static void run_frame(void)
{
    int i;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    uint32_t first = nk_session_poll(0, players[0]);
    uint32_t second = nk_session_poll(1, players[1]);
    state += first * 7u + second * 131u;
    if (nondet)
        state += (uint32_t)(host_ticks_ns() >> 10);
    frames++;
    for (i = 0; i < 16 * 8; i++)
        pic[i] = 0xFF000000u | ((state >> (i & 31)) * 2654435761u & 0xFFFFFFu);
}

static uint64_t frame_ns(void) { return 1000000ull; }
static void app_name(const void *cart, const char **dir, const char **title)
{
    (void)cart;
    *dir = "session_console";
    *title = "session_console";
}
static void window_size(int *w, int *h) { *w = 16; *h = 8; }
static double aspect(void) { return 2.0; }
static void present(const int d[4]) { (void)d; }
static const uint32_t *shot(int *w, int *h, int *xs, int *ys)
{
    *w = 16;
    *h = 8;
    *xs = *ys = 1;
    return pic;
}
static const uint32_t *picture(int *w, int *h)
{
    *w = 16;
    *h = 8;
    return pic;
}

/* 4 samples a frame after each input, their level from the state */
static int tone_left;
static int audio_take(int16_t *lr, int max)
{
    int n = tone_left < max ? tone_left : max, i;
    for (i = 0; i < 2 * n; i++)
        lr[i] = (int16_t)(state & 0x7FFF);
    tone_left -= n;
    return n;
}
static int audio_queued(void) { return tone_left; }

/* A B START as the keyboard's A B Return (positional bits 0, 1, 7) */
static const nk_key keys[] = { { 'A', 1u << 0, "A" }, { 'B', 1u << 1, "B" }, { 0x0D, 1u << 7, "Return" } };
static const nk_button buttons[] = { { "A", 1 }, { "B", 2 }, { "START", 8 } };

static void input(int headless, const uint32_t *b)
{
    if (headless) {
        players[0] = b[0];
        players[1] = b[1];
    } else {
        uint32_t k = nk_input_keyboard_bits();
        players[0] = (k & 1u ? 1u : 0) | (k & 2u ? 2u : 0) | (k & 0x80u ? 8u : 0);
        players[1] = 0;
    }
    tone_left = 4;
}

static void held(uint32_t *b)
{
    b[0] = players[0];
    b[1] = players[1];
}

static void at_frame(void)
{
    if (!nk_headless() && nk_frame_no() >= quit_at)
        nk_quit();
}

static const void *r_state(size_t *n) { *n = sizeof state; return &state; }
static const void *r_frames(size_t *n) { *n = sizeof frames; return &frames; }
static const void *r_players(size_t *n) { *n = sizeof players; return players; }
static const nk_region regions[] = {
    { ".state", r_state, 0 }, { ".frames", r_frames, 0 }, { ".players", r_players, 0 },
};

static const nk_console session = {
    .name = "session_console", .env = "SESSION", .play_ext = ".play",
    .power = power, .run_frame = run_frame, .frame_ns = frame_ns, .app_name = app_name,
    .window_size = window_size, .aspect = aspect, .present = present, .shot = shot, .picture = picture,
    .audio_hz = 4 * 1000.0, .audio_take = audio_take, .audio_queued = audio_queued,
    .players = 2, .keys = keys, .key_count = 3, .buttons = buttons, .button_count = 3,
    .input = input, .held = held, .frame = at_frame,
    .regions = regions, .region_count = 3,
};

int main(int argc, char **argv)
{
    return nk_main(&session, NULL, argc, argv);
}
