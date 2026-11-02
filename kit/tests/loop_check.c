/* loop_check: the kit's loop through frames on the null host. Windowed: the
   battery saved at frame 120 and at quit (nk_quit), as a new file renamed
   over the old; --headless: never saved. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nk/fatal.h"
#include "nk/host.h"
#include "nk/nk.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(70);
}

static uint8_t ram[16];
static uint32_t frame;
static int headless_run, fails;
static char path[1024];

static int read_frame(uint32_t *f)
{
    FILE *fp = fopen(path, "rb");
    size_t n;
    if (!fp)
        return 0;
    n = fread(f, 1, sizeof *f, fp);
    fclose(fp);
    return n == sizeof *f;
}

static void power(const void *cart) { (void)cart; }
static void run_frame(void) { frame++; memcpy(ram, &frame, sizeof frame); }
static uint64_t frame_ns(void) { return 1000000ull; }   /* 1 ms: the test runs fast */
static void app_name(const void *cart, const char **dir, const char **title) { (void)cart; *dir = "loop_check"; *title = "loop_check"; }
static void window_size(int *w, int *h) { *w = 8; *h = 8; }
static double aspect(void) { return 1.0; }
static void present(const int d[4]) { (void)d; }
static uint32_t pic[64];
static const uint32_t *shot(int *w, int *h, int *xs, int *ys) { *w = *h = 8; *xs = *ys = 1; return pic; }
static const uint32_t *picture(int *w, int *h) { *w = *h = 8; return pic; }
static int audio_take(int16_t *lr, int max) { (void)lr; (void)max; return 0; }
static int audio_queued(void) { return 0; }
static void input(int headless, const uint32_t *b) { (void)headless; (void)b; }
static int battery(nk_battery *b)
{
    b->ram = ram;
    b->bytes = sizeof ram;
    b->from = 0;
    b->size = 0;
    b->name = "loop";
    return 1;
}
static void at_frame(void)
{
    uint32_t f = 0;
    if (frame == 121 && !headless_run && !(read_frame(&f) && f == 120)) {
        fprintf(stderr, "loop_check: at frame 121 the save holds %u, not 120\n", f);
        fails++;
    }
    if (frame == 130)
        nk_quit();
}
static void at_exit(void)
{
    uint32_t f = 0;
    int have = read_frame(&f);
    if (headless_run ? have : !(have && f == 130)) {
        fprintf(stderr, "loop_check: at the end the save %s\n", have ? "is there" : "is missing");
        fails++;
    }
    printf(fails ? "loop_check: FAIL\n" : "loop_check: ok\n");
    if (fails)
        _exit(1);
}

static const nk_console loop = {
    .name = "loop_check", .env = "LOOP", .play_ext = ".loop",
    .power = power, .run_frame = run_frame, .frame_ns = frame_ns, .app_name = app_name,
    .window_size = window_size, .aspect = aspect, .present = present, .shot = shot, .picture = picture,
    .audio_hz = 48000.0, .audio_take = audio_take, .audio_queued = audio_queued,
    .players = 1, .input = input, .frame = at_frame, .battery = battery, .at_exit = at_exit,
};

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--headless"))
            headless_run = 1;
    /* the null host's save folder: PORT_SAVEDIR, emptied of a last run's file */
    snprintf(path, sizeof path, "%s/loop.srm", getenv("PORT_SAVEDIR") ? getenv("PORT_SAVEDIR") : ".");
    remove(path);
    return nk_main(&loop, NULL, argc, argv);
}
