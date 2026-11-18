/*
 * session.c: the recorder and the frame log (session.h).
 *
 * The recorder writes the buttons each frame ran with as the script's
 * `wait N` and `hold B[+B...] N` lines: a line when the buttons change,
 * flushed then, and the last at exit. The buttons a frame ran with are the
 * console's controllers as they stand just before the frame runs
 * (nk_console.held), whatever set them: the keyboard, a controller.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0777)
#endif

#include "session.h"
#include "fatal.h"
#include "host.h"

#define VK_F1 0x70
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F9 0x78
#define VK_F10 0x79

static const nk_console *con;
static FILE *log_f, *input_f, *marks_f;
static long ran;                            /* frames run */
static uint32_t run_b[NK_PLAYERS];          /* the buttons of the run of frames not yet written */
static long run_n;
static int recording, shown = 1, holding, step_due;
static long range_from = -1;
static uint64_t start_ns, mark_until_ns;
static char text[96];

/* 64-bit FNV-1a */
#define FNV_START 0xCBF29CE484222325ull
static uint64_t fnv(uint64_t h, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001B3ull;
    }
    return h;
}

static FILE *open_in(const char *dir, const char *name)
{
    char path[1024];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(f = fopen(path, "w")))
        nk_fatal("--record: cannot write %s", path);
    return f;
}

static void log_head(FILE *f)
{
    int i;
    fprintf(f, "# nk frame log 1\n# console %s\n# audio_hz %.3f\n# regions", con->name, con->audio_hz);
    for (i = 0; i < con->region_count; i++)
        fprintf(f, " %s", con->regions[i].ext);
    fprintf(f, "\n");
    fflush(f);
}

/* the run of frames with the same buttons, as a script line */
static void write_run(void)
{
    char line[1024] = "";
    int p, i;
    if (!run_n)
        return;
    for (p = 0; p < con->players; p++)
        for (i = 0; i < con->button_count; i++)
            if (run_b[p] & con->buttons[i].bit) {
                size_t k = strlen(line);
                if (p)
                    snprintf(line + k, sizeof line - k, "%s%d.%s", k ? "+" : "", p + 1, con->buttons[i].name);
                else
                    snprintf(line + k, sizeof line - k, "%s%s", k ? "+" : "", con->buttons[i].name);
            }
    if (line[0])
        fprintf(input_f, "hold %s %ld\n", line, run_n);
    else
        fprintf(input_f, "wait %ld\n", run_n);
    fflush(input_f);
    run_n = 0;
}

static void close_all(void)
{
    if (input_f) {
        write_run();
        fclose(input_f);
        input_f = NULL;
    }
    if (marks_f) {
        if (range_from >= 0)
            fprintf(marks_f, "range %ld %ld %.3f\n", range_from, ran,
                    (double)(host_ticks_ns() - start_ns) / 1e9);
        fclose(marks_f);
        marks_f = NULL;
    }
    if (log_f) {
        fclose(log_f);
        log_f = NULL;
    }
}

void nk_session_open(const nk_console *c, const char *dir, const char *log, int headless)
{
    con = c;
    if (dir && headless)
        nk_fatal("--record is for a windowed run (a headless run replays one: --script DIR/input.nks)");
    if (dir) {
        if (!c->held)
            nk_fatal("--record: %s does not report its controllers (nk_console.held)", c->name);
        if (make_dir(dir) && errno != EEXIST)
            nk_fatal("--record: cannot make %s", dir);
        input_f = open_in(dir, "input.nks");
        marks_f = open_in(dir, "marks.log");
        log_f = open_in(dir, "frames.log");
        fprintf(input_f, "# %s, recorded by --record: --headless --script this file plays it again\n", c->name);
        recording = 1;
        start_ns = host_ticks_ns();
    }
    if (log && !log_f && !(log_f = fopen(log, "w")))
        nk_fatal("--frame-log: cannot write %s", log);
    if (log_f)
        log_head(log_f);
    if (log_f || input_f)
        atexit(close_all);
}

int nk_session_holding(void)
{
    if (!holding)
        return 0;
    if (step_due) {
        step_due = 0;
        return 0;
    }
    return 1;
}

void nk_session_run_begin(void)
{
    uint32_t b[NK_PLAYERS] = { 0 }, known = 0;
    int p, i;
    if (!input_f)
        return;
    con->held(b);
    for (i = 0; i < con->button_count; i++)
        known |= con->buttons[i].bit;
    for (p = 0; p < NK_PLAYERS; p++)
        b[p] = p < con->players ? b[p] & known : 0;
    if (run_n && memcmp(b, run_b, sizeof b))
        write_run();
    memcpy(run_b, b, sizeof b);
    run_n++;
}

void nk_session_run_end(long long samples)
{
    uint64_t pic = FNV_START, st = FNV_START;
    int i;
    ran++;
    if (!log_f)
        return;
    if (con->picture) {
        int w = 0, h = 0;
        const uint32_t *px = con->picture(&w, &h);
        pic = fnv(pic, &w, sizeof w);
        pic = fnv(pic, &h, sizeof h);
        if (px && w > 0 && h > 0)
            pic = fnv(pic, px, (size_t)w * (size_t)h * sizeof *px);
    }
    for (i = 0; i < con->region_count; i++) {
        size_t n = 0;
        const void *d = con->regions[i].data(&n);
        st = fnv(st, &n, sizeof n);
        if (d && n)
            st = fnv(st, d, n);
    }
    if (samples >= 0)
        fprintf(log_f, "F %ld %016llx %016llx %lld\n", ran, (unsigned long long)pic, (unsigned long long)st,
                samples);
    else
        fprintf(log_f, "F %ld %016llx %016llx\n", ran, (unsigned long long)pic, (unsigned long long)st);
    if (recording && ran % 60 == 0)
        fflush(log_f);
}

int nk_session_key(int vk, int down)
{
    double t;
    if (!recording)
        return 0;
    if (vk != VK_F1 && vk != VK_F5 && vk != VK_F6 && vk != VK_F7 && vk != VK_F9 && vk != VK_F10)
        return 0;
    if (!down)
        return 1;
    t = (double)(host_ticks_ns() - start_ns) / 1e9;
    switch (vk) {
    case VK_F1: shown = !shown; break;
    case VK_F5:
        fprintf(marks_f, "mark %ld %.3f\n", ran, t);
        fflush(marks_f);
        mark_until_ns = host_ticks_ns() + 1000000000ull;
        break;
    case VK_F6: range_from = ran; break;
    case VK_F7:
        if (range_from >= 0) {
            long a = range_from < ran ? range_from : ran, b = range_from < ran ? ran : range_from;
            fprintf(marks_f, "range %ld %ld %.3f\n", a, b, t);
            fflush(marks_f);
            range_from = -1;
            mark_until_ns = host_ticks_ns() + 1000000000ull;
        }
        break;
    case VK_F9:
        holding = !holding;
        step_due = 0;
        if (log_f)
            fflush(log_f);
        break;
    case VK_F10:
        if (holding)
            step_due = 1;
        break;
    }
    return 1;
}

const char *nk_session_text(void)
{
    size_t k;
    if (!recording || !shown)
        return "";
    snprintf(text, sizeof text, "rec %ld", ran);
    k = strlen(text);
    if (holding)
        snprintf(text + k, sizeof text - k, "  pause");
    k = strlen(text);
    if (range_from >= 0)
        snprintf(text + k, sizeof text - k, "  range %ld", range_from);
    k = strlen(text);
    if (host_ticks_ns() < mark_until_ns)
        snprintf(text + k, sizeof text - k, "  marked");
    return text;
}

long nk_session_frames(void) { return ran; }
