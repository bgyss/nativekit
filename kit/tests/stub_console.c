/* stub_console: a console for the kit's checks: its machine a frame
   counter and a xorshift sequence the buttons stir. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nk/env.h"
#include "nk/fatal.h"
#include "nk/nk.h"
#include "nk/script.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "stub: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(70);
}

static uint32_t state = 1, frames;
static uint32_t players[NK_PLAYERS];
static uint32_t pic[8];

static void stir(uint32_t b)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    state += b;
    frames++;
}

static const void *r_state(size_t *n) { *n = sizeof state; return &state; }
static const void *r_frames(size_t *n) { *n = sizeof frames; return &frames; }
static const void *r_players(size_t *n) { *n = sizeof players; return players; }
static const void *r_none(size_t *n) { *n = 0; return NULL; }

static const uint32_t *shot(int *w, int *h, int *xs, int *ys)
{
    int i;
    for (i = 0; i < 8; i++)
        pic[i] = 0xFF000000u | (state >> i) * 2654435761u;
    *w = 4;
    *h = 2;
    *xs = *ys = 1;
    return pic;
}

static int step(int n, char w[][1024])
{
    if (!strcmp(w[0], "poke") && n == 2) {
        state += (uint32_t)atoi(w[1]);
        return 1;
    }
    return 0;
}

static const nk_region regions[] = {
    { ".state", r_state, 0 }, { ".frames", r_frames, 0 }, { ".none", r_none, 1 }, { ".empty", r_none, 0 },
    { ".players", r_players, 0 },
};
static const nk_button buttons[] = { { "A", 1 }, { "B", 2 }, { "START", 8 } };
static const char *const steps[] = { "poke" };

nk_console stub_console = {
    .name = "stub", .env = "STUB", .play_ext = ".play",
    .shot = shot,
    .buttons = buttons, .button_count = 3,
    .steps = steps, .step_count = 1, .script_step = step,
    .regions = regions, .region_count = 5,
};

int main(int argc, char **argv)
{
    const char *script = NULL, *out = ".";
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--script") && i + 1 < argc)
            script = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out = argv[++i];
    }
    nk_env_prefix(stub_console.env);
    stub_console.players = getenv("STUB_PLAYERS") ? atoi(getenv("STUB_PLAYERS")) : 2;
    nk_script_console(&stub_console);
    nk_script_args(argc, argv);
    if (!script)
        return 2;
    nk_script_load(script, out);
    {
        uint32_t b[NK_PLAYERS];
        nk_script_takes();
        nk_script_frame(b);
        for (;;) {
            memcpy(players, b, sizeof players);
            stir(b[0] | b[1] << 16);
            if (!nk_script_frame(b))
                return 0;
        }
    }
}
