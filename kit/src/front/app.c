/*
 * app.c: the console as an application (nk_main): the window, the controls,
 * the sound, the save and the pace, over the console's machine (nk.h).
 *
 * Time is the console's: a frame each nk_console.frame_ns against the wall
 * clock, the sound drained at the console's rate (nk_console.audio_hz) and
 * resampled to the device's (windowed sinc), the rate trimmed by up to
 * 0.5% to keep 50 ms buffered.
 *
 * The controls: the keyboard, game controllers, the mouse and touches
 * (input.h), onto the console's controllers (nk_console.input).
 *
 * The save is the console's battery RAM (save.h), kept in the host's save
 * folder and written when it changes. A headless run (a test) neither
 * reads nor writes it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "nk.h"
#include "env.h"
#include "fatal.h"
#include "save.h"
#include "script.h"
#include "input.h"
#include "host.h"
#include "resample.h"
#include "rr.h"
#include "rr_size.h"

static const nk_console *c;
const nk_console *nk_console_now(void) { return c; }

void nk_window_size(float *w, float *h)
{
    host_window_points(w, h);
    if (*w <= 0 || *h <= 0) {
        int tw = 0, th = 0;
        c->window_size(&tw, &th);
        rr_set_headless_size(tw, th);
        rr_headless_size(&tw, &th);
        *w = (float)tw;
        *h = (float)th;
    }
}

/* ---- the game's overlays ---- */
typedef struct {
    uint32_t *argb, *shown;      /* the image; it at its alpha */
    int w, h;
    float cx, cy, scale, angle, alpha, shown_alpha;
    int visible;
} Overlay;
static Overlay overlays[NK_OVERLAYS];
static int noverlays;

int nk_overlay_image(const uint32_t *argb, int w, int h)
{
    Overlay *o;
    if (noverlays == NK_OVERLAYS || w <= 0 || h <= 0)
        return -1;
    o = &overlays[noverlays];
    o->argb = (uint32_t *)malloc((size_t)w * (size_t)h * 4);
    o->shown = (uint32_t *)malloc((size_t)w * (size_t)h * 4);
    if (!o->argb || !o->shown)
        nk_fatal("no memory for an overlay");
    memcpy(o->argb, argb, (size_t)w * (size_t)h * 4);
    o->w = w;
    o->h = h;
    o->scale = 1;
    o->alpha = 1;
    o->shown_alpha = -1;
    o->visible = 0;
    return noverlays++;
}

void nk_overlay_place(int id, float cx, float cy, float scale, float angle_rad, float alpha, int visible)
{
    Overlay *o;
    if (id < 0 || id >= noverlays)
        return;
    o = &overlays[id];
    o->cx = cx;
    o->cy = cy;
    o->scale = scale;
    o->angle = angle_rad;
    o->alpha = alpha < 0 ? 0 : alpha > 1 ? 1 : alpha;
    o->visible = visible != 0;
}

/* the overlays over the target (tw x th pixels for pw x ph points) */
static void draw_overlays(int tw, int th)
{
    float pw, ph, sx, sy;
    int i;
    if (!noverlays)
        return;
    nk_window_size(&pw, &ph);
    sx = pw > 0 ? (float)tw / pw : 1;
    sy = ph > 0 ? (float)th / ph : 1;
    for (i = 0; i < noverlays; i++) {
        Overlay *o = &overlays[i];
        float hw, hh, co, sn, q[6];
        if (!o->visible || o->alpha <= 0)
            continue;
        if (o->shown_alpha != o->alpha) {
            size_t k, n = (size_t)o->w * (size_t)o->h;
            for (k = 0; k < n; k++) {
                uint32_t a = (uint32_t)((float)(o->argb[k] >> 24) * o->alpha + 0.5f);
                o->shown[k] = (o->argb[k] & 0xFFFFFFu) | a << 24;
            }
            o->shown_alpha = o->alpha;
        }
        hw = (float)o->w * o->scale / 2;
        hh = (float)o->h * o->scale / 2;
        co = cosf(o->angle);
        sn = sinf(o->angle);
        /* the corners (top left, top right, bottom left) turned about the
           centre, in points, then in target pixels */
        q[0] = (o->cx + (-hw * co - -hh * sn)) * sx;
        q[1] = (o->cy + (-hw * sn + -hh * co)) * sy;
        q[2] = (o->cx + (hw * co - -hh * sn)) * sx;
        q[3] = (o->cy + (hw * sn + -hh * co)) * sy;
        q[4] = (o->cx + (-hw * co - hh * sn)) * sx;
        q[5] = (o->cy + (-hw * sn + hh * co)) * sy;
        rr_overlay(o->shown, o->w, o->h, q);
    }
}

/* ---- the application ---- */
static int headless;

/* --counts FILE (headless): each frame's accesses by the console's kinds
   of access (nk_console.access_kind), for the cost profile's fitter:
   "F frame" and the reads of each kind, then the writes. A stretch of
   more than 64 reads with no write (a poll loop, a wait's reads) is left
   out, as the profile's measure of the original leaves it out. */
static FILE *counts_f;
static uint64_t counts[2][16];
static uint8_t pending_kind[65];
static unsigned pending_n;

/* an access's kind, one of the console's kind_count */
static int count_kind(uint32_t addr)
{
    int k = c->access_kind(addr);
    if (k < 0 || k >= c->kind_count)
        nk_fatal("an access of kind %d, not one of the console's %d", k, c->kind_count);
    return k;
}

static void count_read(uint32_t addr, uint8_t value)
{
    (void)value;
    if (pending_n < 65)
        pending_kind[pending_n] = (uint8_t)count_kind(addr);
    pending_n++;
}

static void count_write(uint32_t addr, uint8_t value)
{
    unsigned k;
    (void)value;
    if (pending_n <= 64)
        for (k = 0; k < pending_n; k++)
            counts[0][pending_kind[k]]++;
    pending_n = 0;
    counts[1][count_kind(addr)]++;
}

static void counts_frame(long n)
{
    int w, k;
    fprintf(counts_f, "F %ld", n);
    for (w = 0; w < 2; w++)
        for (k = 0; k < c->kind_count; k++)
            fprintf(counts_f, " %llu", (unsigned long long)counts[w][k]);
    fprintf(counts_f, "\n");
    memset(counts, 0, sizeof counts);
}
static long max_frames = -1;
static long frame_no;
static FILE *sound_f;

int nk_headless(void) { return headless; }
long nk_frame_no(void) { return frame_no; }

/* ---- sound: the console's rate to the device's ---- */
#define OUT_RATE 48000
static Resampler *s_rs;

static void audio_cb(float *lr, int frames, void *user)
{
    int16_t in[2048 * 2];
    int got, made, q;
    double target = c->audio_hz * 0.05, d;
    (void)user;
    while (resample_queued(s_rs) < 12000 && (got = c->audio_take(in, 2048)) > 0)
        resample_push(s_rs, in, got);
    q = resample_queued(s_rs) + c->audio_queued();
    d = (q - target) / target;
    resample_adjust(s_rs, 0.005 * (d > 1 ? 1 : d < -1 ? -1 : d));
    made = resample_pull(s_rs, lr, frames);
    if (made < frames)
        memset(lr + 2 * made, 0, (size_t)(frames - made) * 2 * sizeof *lr);
}

static void sound_drain(void)
{
    int16_t b[2048 * 2];
    int n;
    while ((n = c->audio_take(b, 2048)) > 0)
        if (!nk_script_replaying())
            fwrite(b, 4, (size_t)n, sound_f);
}

/* <env>_SCALE=S or SXxSY (headless), window pixels a console pixel: a
   shot is the frame presented as a window that size shows it (without
   it, the console's picture); 0 when not set */
void nk_shot_scale(double *x, double *y)
{
    static int parsed;
    static double sx, sy;
    if (!parsed) {
        const char *e = nk_getenv("SCALE");
        parsed = 1;
        if (e && sscanf(e, "%lfx%lf", &sx, &sy) < 2)
            sy = sx;
        if (!(sx > 0 && sy > 0 && sx <= 16 && sy <= 16))
            sx = sy = 0;
    }
    *x = sx;
    *y = sy;
}

/* ---- the picture on the window ---- */
/* where the last frame was drawn on the renderer's target (pixels), and
   that target's width */
static int shown_at[4], shown_tw;

void nk_shown_at(int d[4], int *tw)
{
    memcpy(d, shown_at, sizeof shown_at);
    *tw = shown_tw;
}

/* ---- the frame-rate meter (<env>_FPS=1, <env>_FPS_LOG=FILE) ----
   Over the last whole second of the window's run: frames presented and
   console frames run a second, the longest time between two presents,
   and the longest each frame spent running the console, presenting and
   waiting for its time. <env>_FPS=1 draws it at the top left;
   <env>_FPS_LOG=FILE writes a line a presented frame: its number, its
   present's time from the first (ms), the time since the last present,
   and the time it spent running, presenting and waiting. */
static int fps_on = -1;
static FILE *fps_log;
static uint64_t fps_first, fps_last_shown, fps_sec, fps_frame_start;
static long fps_shown, fps_run;                    /* this second's */
static double fps_worst_gap, fps_worst_run, fps_worst_present, fps_worst_wait;
static char fps_text[96] = "";                     /* the last second's, as shown */
static uint64_t fps_ran_ns, fps_present_ns;        /* this frame's */

/* a 3 x 5 font for the meter: a row a 3-bit number, top row first */
static const struct {
    char c;
    uint8_t row[5];
} fps_font[] = {
    { '0', { 7, 5, 5, 5, 7 } }, { '1', { 2, 6, 2, 2, 7 } }, { '2', { 7, 1, 7, 4, 7 } }, { '3', { 7, 1, 3, 1, 7 } },
    { '4', { 5, 5, 7, 1, 1 } }, { '5', { 7, 4, 7, 1, 7 } }, { '6', { 7, 4, 7, 5, 7 } }, { '7', { 7, 1, 1, 1, 1 } },
    { '8', { 7, 5, 7, 5, 7 } }, { '9', { 7, 5, 7, 1, 7 } }, { '.', { 0, 0, 0, 0, 2 } }, { ' ', { 0, 0, 0, 0, 0 } },
    { 'a', { 0, 7, 5, 7, 5 } }, { 'e', { 0, 7, 7, 4, 7 } }, { 'f', { 3, 4, 7, 4, 4 } }, { 'g', { 0, 7, 5, 7, 1 } },
    { 'i', { 2, 0, 2, 2, 2 } }, { 'm', { 0, 7, 7, 5, 5 } }, { 'n', { 0, 6, 5, 5, 5 } }, { 'o', { 0, 7, 5, 5, 7 } },
    { 'p', { 0, 7, 5, 7, 4 } }, { 'r', { 0, 7, 4, 4, 4 } }, { 's', { 0, 7, 6, 3, 7 } }, { 't', { 2, 7, 2, 2, 3 } },
    { 'u', { 0, 5, 5, 5, 7 } }, { 'w', { 0, 5, 5, 7, 7 } }, { 'x', { 0, 5, 2, 2, 5 } }, { '/', { 1, 1, 2, 4, 4 } },
};

static void fps_draw(void)
{
    static uint32_t px[7 * (2 + 4 * 96)];
    size_t n = strlen(fps_text), i, w = 2 + 4 * n;
    int x, y;
    float q[6];
    if (!n)
        return;
    for (i = 0; i < 7 * w; i++)
        px[i] = 0xB0000000u;                       /* a dark panel behind the text */
    for (i = 0; i < n; i++) {
        size_t g;
        for (g = 0; g < sizeof fps_font / sizeof fps_font[0] && fps_font[g].c != fps_text[i]; g++)
            ;
        if (g == sizeof fps_font / sizeof fps_font[0])
            continue;
        for (y = 0; y < 5; y++)
            for (x = 0; x < 3; x++)
                if (fps_font[g].row[y] >> (2 - x) & 1)
                    px[(size_t)(y + 1) * w + 1 + 4 * i + (size_t)x] = 0xFFFFFF40u;
    }
    /* 3 target pixels a font pixel, at the top left */
    q[0] = 8;
    q[1] = 8;
    q[2] = 8 + 3.0f * (float)w;
    q[3] = 8;
    q[4] = 8;
    q[5] = 8 + 3.0f * 7;
    rr_overlay(px, (int)w, 7, q);
}

/* a presented frame's times, ns: the meter's second and the log's line */
static void fps_frame(uint64_t shown, uint64_t ran, uint64_t presented, uint64_t waited)
{
    double gap = fps_last_shown ? (double)(shown - fps_last_shown) / 1e6 : 0;
    if (!fps_first)
        fps_first = fps_sec = shown;
    fps_last_shown = shown;
    fps_shown++;
    fps_run++;
    if (gap > fps_worst_gap) fps_worst_gap = gap;
    if ((double)ran / 1e6 > fps_worst_run) fps_worst_run = (double)ran / 1e6;
    if ((double)presented / 1e6 > fps_worst_present) fps_worst_present = (double)presented / 1e6;
    if ((double)waited / 1e6 > fps_worst_wait) fps_worst_wait = (double)waited / 1e6;
    if (fps_log)
        fprintf(fps_log, "%ld %.3f %.3f %.3f %.3f %.3f\n", frame_no, (double)(shown - fps_first) / 1e6, gap,
                (double)ran / 1e6, (double)presented / 1e6, (double)waited / 1e6);
    if (shown - fps_sec >= 1000000000ull) {
        double secs = (double)(shown - fps_sec) / 1e9;
        snprintf(fps_text, sizeof fps_text, "%.1f fps  %.1f run  gap %.1f  ran %.1f  present %.1f  wait %.1f ms",
                 fps_shown / secs, fps_run / secs, fps_worst_gap, fps_worst_run, fps_worst_present, fps_worst_wait);
        fps_sec = shown;
        fps_shown = fps_run = 0;
        fps_worst_gap = fps_worst_run = fps_worst_present = fps_worst_wait = 0;
    }
}

/* the frame as large as the window holds, centred, at the console's
   aspect (nk_console.aspect), drawn by the console (nk_console.present) */
static void show(void)
{
    int tw, th, w, h, d[4];
    double aspect;
    rr_target(&tw, &th);
    aspect = c->aspect();
    w = (int)(th * aspect + 0.5);
    h = th;
    if (w > tw) {
        w = tw;
        h = (int)(tw / aspect + 0.5);
    }
    d[0] = (tw - w) / 2;
    d[1] = (th - h) / 2;
    d[2] = d[0] + w;
    d[3] = d[1] + h;
    memcpy(shown_at, d, sizeof shown_at);
    shown_tw = tw;
    if (c->scaled)
        c->scaled(w, h);
    rr_begin(0xFF000000u);
    c->present(d);
    draw_overlays(tw, th);
    if (fps_on > 0)
        fps_draw();
    rr_present();
}

/* <env>_RRCHECK=1 (headless, any renderer): each finished frame shown by
   the renderer one to one and scaled to the target with sharp bilinear,
   read back, against the console's picture drawn the same way on the CPU
   (rr_size.h rr_ref_image), or a frame the console draws itself
   (nk_console.rrcheck) against the reference it makes; at exit, the frames
   and pixels that differ by more than 1 in a channel (a GPU's filtering
   rounds) */
static long rr_frames = -1, rr_bad, rr_pixels, rr_first = -1, rr_own_frames;

static void rr_report(void)
{
    fprintf(stderr, "rrcheck: %s, %ld frames (%ld %s), %ld differ (first at frame %ld), %ld pixels\n", rr_name(),
            rr_frames, rr_own_frames, c->rrcheck_own ? c->rrcheck_own : "drawn by the console", rr_bad, rr_first,
            rr_pixels);
}

static void rr_check(void)
{
    static uint32_t *got, *want;
    int tw, th, pw, ph, i, d[4], own;
    const uint32_t *px = c->picture(&pw, &ph);
    long bad = 0;
    if (rr_frames < 0) {
        if (!rr_open()) {
            fprintf(stderr, "rrcheck: the renderer cannot open\n");
            exit(2);
        }
        atexit(rr_report);
        rr_frames = 0;
    }
    rr_target(&tw, &th);
    if (!got) {
        got = (uint32_t *)malloc((size_t)tw * (size_t)th * 4);
        want = (uint32_t *)malloc((size_t)tw * (size_t)th * 4);
    }
    /* one to one at the top left, the rest of the target the picture
       scaled up (whole and part pixels) */
    d[0] = 0;
    d[1] = 0;
    d[2] = tw;
    d[3] = th;
    if (c->scaled)
        c->scaled(tw, th);
    rr_begin(0xFF000000u);
    for (i = 0; i < tw * th; i++)
        want[i] = 0xFF000000u;
    own = c->rrcheck && c->rrcheck(d, want, tw, th);
    if (own) {
        rr_own_frames++;
    } else {
        c->present(d);
        rr_ref_image(want, tw, th, px, pw, ph, d);
    }
    rr_present();
    rr_read(got);
    for (i = 0; i < tw * th; i++) {
        int sh, worst = 0;
        for (sh = 0; sh < 24; sh += 8) {
            int a = (int)((got[i] >> sh) & 0xFF) - (int)((want[i] >> sh) & 0xFF);
            if (a < 0)
                a = -a;
            if (a > worst)
                worst = a;
        }
        if (worst > 1) {
            /* <env>_RRCHECK_WHY=1: the first frame that differs, its first
               pixels (with the console's record of each, rrcheck_why) */
            if (rr_first < 0 && bad < 8 && nk_getenv("RRCHECK_WHY")) {
                int X = i % tw, Y = i / tw;
                fprintf(stderr, "rrcheck: frame %ld at (%d, %d): got %08x want %08x", frame_no, X, Y, got[i], want[i]);
                if (own && c->rrcheck_why)
                    c->rrcheck_why(X, Y);
                fprintf(stderr, "\n");
            }
            bad++;
        }
    }
    rr_frames++;
    if (bad) {
        rr_bad++;
        rr_pixels += bad;
        if (rr_first < 0)
            rr_first = frame_no;
    }
}

/* ---- each frame ---- */
static uint64_t next_due;

static void quit(void)
{
    nk_save_write();
    host_shutdown();
    exit(0);
}

/* After a frame: in a headless run the script's shots, dumps and buttons;
   else the window, input, save and the pace. */
static void frame(void)
{
    uint32_t b[NK_PLAYERS] = { 0 };
    frame_no++;
    if (headless) {
        static int rc = -1;
        int more;
        if (counts_f)
            counts_frame(frame_no);
        if (rc < 0)
            rc = nk_getenv("RRCHECK") != NULL;
        if (rc)
            rr_check();
        if (sound_f)
            sound_drain();
        more = nk_script_frame(b);
        c->input(1, b);
        if (c->frame)
            c->frame();
        if ((max_frames >= 0 && frame_no >= max_frames) || (!more && nk_script_steps() && max_frames < 0))
            exit(0);
        return;
    }
    if (fps_on < 0) {
        const char *lf = nk_getenv("FPS_LOG");
        fps_on = nk_getenv("FPS") != NULL;
        if (lf && !(fps_log = fopen(lf, "w")))
            nk_fatal("%s_FPS_LOG: cannot write %s", c->env, lf);
        if (fps_log)
            setvbuf(fps_log, NULL, _IOLBF, 0);
    }
    {
        uint64_t before = host_ticks_ns();
        fps_ran_ns = fps_frame_start ? before - fps_frame_start : 0;
        show();
        fps_present_ns = host_ticks_ns() - before;
    }
    {
        host_event e;
        while (host_poll_event(&e, 0)) {
            if (e.type == HOST_EV_KEY) {
                /* the console's (and its game's) first; 0: on to the kit */
                if (!(c->key && c->key(e.vk, e.down)))
                    nk_input_key(e.vk, e.down);
            } else if (e.type == HOST_EV_MOUSE) {
                nk_input_mouse(e.x, e.y, e.dx, e.dy, e.buttons);
            } else if (e.type == HOST_EV_TOUCH) {
                nk_input_touch(e.id, e.phase, e.x, e.y);
                if (c->touch)
                    c->touch(e.id, e.phase, e.x, e.y);
            } else if (e.type == HOST_EV_CLOSE)
                quit();
            else if (e.type == HOST_EV_FOCUS && !e.down)
                nk_input_focus_lost();
        }
    }
    c->input(0, NULL);
    if (frame_no % 120 == 0)
        nk_save_write();
    if (c->frame)
        c->frame();
    /* the pace: each frame due a frame's time after the last */
    {
        uint64_t frame_ns = c->frame_ns();
        uint64_t now = host_ticks_ns();
        if (!next_due || now > next_due + 100000000ull)
            next_due = now;
        next_due += frame_ns;
        while ((now = host_ticks_ns()) < next_due) {
            uint64_t left = next_due - now;
            if (left > 2000000ull)
                host_sleep_ms((uint32_t)(left / 1000000ull) - 1);
        }
        if (fps_on > 0 || fps_log) {
            uint64_t shown = fps_frame_start + fps_ran_ns + fps_present_ns;
            fps_frame(fps_frame_start ? shown : now, fps_ran_ns, fps_present_ns,
                      fps_frame_start ? now - shown : 0);
        }
        fps_frame_start = now;
    }
}

int nk_main(const nk_console *con, const void *cart, int argc, char **argv)
{
    const char *script = NULL, *out_dir = ".", *dir, *title;
    int i, ww, wh;
    c = con;
    nk_input_keymap(c->keys, c->key_count);
    if (c->players < 1 || c->players > NK_PLAYERS)
        nk_fatal("the console has %d players, not 1 to %d", c->players, NK_PLAYERS);
    nk_env_prefix(c->env);
    nk_script_console(c);
    if (c->at_exit)
        atexit(c->at_exit);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless"))
            headless = 1;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc)
            script = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out_dir = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            max_frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--counts") && i + 1 < argc) {
            if (!(counts_f = fopen(argv[++i], "w"))) {
                fprintf(stderr, "%s: cannot write %s\n", c->name, argv[i]);
                return 2;
            }
            setvbuf(counts_f, NULL, _IOFBF, 1 << 16);
        } else if (!strcmp(argv[i], "--sound") && i + 1 < argc) {
            /* a new copy of the program going on from a replay adds to it */
            if (!(sound_f = fopen(argv[++i], nk_getenv("SCRIPT_PLAN") ? "ab" : "wb"))) {
                fprintf(stderr, "%s: cannot write %s\n", c->name, argv[i]);
                return 2;
            }
        }
    }
    c->app_name(cart, &dir, &title);
    host_set_app_name(dir, title);
    host_init(argc, argv);
    nk_script_args(argc, argv);
    if (script)
        nk_script_load(script, out_dir);
    c->power(cart);
    c->window_size(&ww, &wh);
    rr_set_headless_size(ww, wh);
    if (headless && c->headless_scale) {
        /* shots as a window <env>_SCALE pixels a console pixel shows them */
        double sx, sy;
        nk_shot_scale(&sx, &sy);
        if (sx > 0)
            c->headless_scale(sx, sy);
    }
    if (counts_f) {
        if (c->kind_count > 16)
            nk_fatal("a console with more than 16 kinds of access");
        if (!c->counts_hooks || !c->access_kind)
            nk_fatal("a console with no access counts to take");
        c->counts_hooks(count_read, count_write);
    }
    if (!headless && c->battery)
        nk_save_load(c->battery, host_save_dir());
    if (headless && script) {
        /* A step's first frame has its buttons before that frame runs: set
           at the end of the frame before. The first frame has none before
           it: it is taken now (its buttons reach no controller read), with
           the shots and dumps before it. */
        uint32_t b[NK_PLAYERS];
        nk_script_takes();
        nk_script_frame(b);
    }
    if (!headless) {
        host_thread_interactive();
        c->window_size(&ww, &wh);
        host_window_open(ww, wh, title);
        if (!rr_open()) {
            host_message_box("The renderer could not start on this computer.", title);
            return 1;
        }
        s_rs = resample_new(c->audio_hz, OUT_RATE);
        host_audio_open(OUT_RATE, audio_cb, NULL);
    }
    for (;;) {
        c->run_frame();
        frame();
    }
}
