/* The public loop extensions through a real null host and software renderer:
 * paused machine, continuing hooks, backdrop tint, and updated overlay pixels. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nk/nk.h"
#include "nk/rr.h"

static int machine, displays, fails, overlay, headless;
static uint32_t pic[64];

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(70);
}

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "extensions_check: %s at display %d\n", what, displays);
        fails++;
    }
}

static void power(const void *cart)
{
    const uint32_t red = 0xFFFF0000u;
    int i;
    (void)cart;
    for (i = 0; i < 64; i++)
        pic[i] = 0xFF112233u;
    overlay = nk_overlay_image(&red, 1, 1);
    nk_overlay_place(overlay, 4, 4, 2, 0, 1, 1);
}
static void run_frame(void) { machine++; }
static uint64_t frame_ns(void) { return 1000000; }
static void app_name(const void *cart, const char **dir, const char **title)
{
    (void)cart;
    *dir = *title = "extensions_check";
}
static void window_size(int *w, int *h) { *w = *h = 8; }
static double aspect(void) { return 1; }
static void present(const int d[4]) { rr_image(pic, 8, 8, d); }
static const uint32_t *picture(int *w, int *h) { *w = *h = 8; return pic; }
static int audio_take(int16_t *lr, int n) { (void)lr; (void)n; return 0; }
static int audio_queued(void) { return 0; }
static void input(int is_headless, const uint32_t *b) { (void)is_headless; (void)b; }

static void frame(void)
{
    uint32_t shown[64];
    displays++;
    check(machine == (displays > 1 ? displays - 1 : 1), "pause holds machine for one display");
    if (!headless) {
        check(rr_read(shown), "last picture can be read");
        check(shown[0] == (displays == 2 ? 0xFF081119u : pic[0]), "only pause backdrop is tinted");
        check(shown[3 * 8 + 3] == (displays == 1 ? 0xFFFF0000u : 0xFF00FF00u),
              "overlay update replaces cached pixels during pause");
    }
    if (displays == 1) {
        const uint32_t green = 0xFF00FF00u;
        nk_pause(1, 0, 0x80000000u);
        check(nk_paused(), "pause state is observable");
        nk_overlay_update(overlay, &green);
    } else if (displays == 2) {
        nk_pause(0, 0, 0);
        check(!nk_paused(), "resume state is observable");
    } else if (displays == 4) {
        printf("extensions_check: %d failures\n", fails);
        exit(fails ? 1 : 0);
    }
}

static const nk_console console = {
    .name = "extensions_check", .env = "EXTS", .play_ext = ".exts",
    .power = power, .run_frame = run_frame, .frame_ns = frame_ns, .app_name = app_name,
    .window_size = window_size, .aspect = aspect, .present = present, .picture = picture,
    .audio_hz = 48000, .audio_take = audio_take, .audio_queued = audio_queued,
    .players = 1, .input = input, .frame = frame,
};

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--headless"))
            headless = 1;
    return nk_main(&console, NULL, argc, argv);
}
