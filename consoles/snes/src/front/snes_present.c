/*
 * snes_present.c: the SNES's picture on the kit's window (nk.h's present,
 * aspect, scaled, rrcheck): the PPU's picture or its frame's description
 * by the renderer's compositor, a frame with Mode 7 or a canvas by its
 * record (rr_mode7.h); a shot's picture; the dot under a point; the PPU's
 * own checks' reports at exit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nshvc/nshvc.h"
#include "snes_internal.h"
#include "snes_rr.h"
#include "nk/rr_size.h"
#include "rr_mode7.h"
#include "snes_console.h"
#include "nk/input.h"
#include "snes_input.h"

/* the canvas's texels a console pixel along each axis for a picture shown
   w x h: the window's pixels a console pixel, rounded up (so a canvas at
   that scale has at least a texel a window pixel) */
static void canvas_scale(int w, int h)
{
    const PpuFrame *f = ppu_frame();
    int kx = (w + f->width - 1) / f->width, ky = (h + f->screen_height - 1) / f->screen_height;
    ppu_set_canvas_scale(kx > ky ? kx : ky);
}

/* The last frame's picture over target rectangle d: drawn by the
   renderer's compositor from the frame's description where it has one
   (render/compose_body.h; NSHVC_COMPOSE=0: never), else the PPU's picture
   drawn here. An interlaced frame's weave is the PPU's. */
static void show_picture(const int d[4])
{
    static int on = -1;
    const PpuFrame *f = ppu_frame();
    const uint32_t *px;
    int pw, ph;
    if (on < 0)
        on = !getenv("NSHVC_COMPOSE") || strcmp(getenv("NSHVC_COMPOSE"), "0");
    if (on && f->versions > 0 && !f->interlace) {
        size_t nw;
        const uint32_t *desc = ppu_compose(f, &nw);
        if (rr_compose(desc, nw, ppu_frame_width(f), f->screen_height, d))
            return;
    } else if (on && f->versions > 0) {
        /* an interlaced frame: woven from its description and, for the
           rows that are the frame before's, that one's */
        static uint32_t *both;
        static size_t cap;
        int ww, wh, from[2];
        size_t na, nb = 0;
        const uint32_t *a;
        if (ppu_woven(&ww, &wh, from) && (!(from[0] | from[1]) || ppu_frame_before()->versions > 0)) {
            a = ppu_compose(f, &na);
            if (na > cap) {
                free(both);
                both = (uint32_t *)malloc(na * 2 * sizeof *both);
                cap = both ? na * 2 : 0;
            }
            if (both) {
                memcpy(both, a, na * sizeof *both);
                if (from[0] | from[1]) {
                    const uint32_t *b = ppu_compose(ppu_frame_before(), &nb);
                    if (na + nb > cap) {
                        uint32_t *g = (uint32_t *)realloc(both, (na + nb) * sizeof *both);
                        if (g) {
                            both = g;
                            cap = na + nb;
                        }
                    }
                    if (na + nb <= cap)
                        memcpy(both + na, b, nb * sizeof *both);
                    else
                        nb = (size_t)-1;
                }
                if (nb != (size_t)-1 &&
                    rr_compose_woven(both, na + nb, ww, wh, from[0] ? (uint32_t)na : 0, from[1] ? (uint32_t)na : 0, d))
                    return;
            }
        }
    }
    px = ppu_picture(&pw, &ph);
    rr_image(px, pw, ph, d);
}

/* ---- a frame with Mode 7 or a canvas presented (render/rr_mode7.h) ---- */
static int32_t *m7_cols, *m7_rows;
static int m7_cols_cap, m7_rows_cap;

/* the last frame's record and where it samples drawn at ow x oh; 0 when it
   is drawn as its picture scaled (no Mode 7 or canvas in it, or an
   interlaced weave) */
static int mode7_prepare(int ow, int oh, RrMode7 *m, RrM7Samples *s)
{
    const PpuFrame *f = ppu_frame();
    int pw, ph;
    const uint32_t *px;
    if (ow <= 0 || oh <= 0 || !ppu_frame_mode7(f))
        return 0;
    px = ppu_picture(&pw, &ph);
    if (ph != f->screen_height || !ppu_mode7(f, px, m))
        return 0;
    if (ow > m7_cols_cap) {
        free(m7_cols);
        m7_cols = (int32_t *)malloc((size_t)ow * 3 * 4);
        m7_cols_cap = m7_cols ? ow : 0;
    }
    if (oh > m7_rows_cap) {
        free(m7_rows);
        m7_rows = (int32_t *)malloc((size_t)oh * 4 * 4);
        m7_rows_cap = m7_rows ? oh : 0;
    }
    if (!m7_cols || !m7_rows)
        nshvc_fatal("no memory for the picture at %dx%d", ow, oh);
    rr_m7_tables(m, ow, oh, s, m7_cols, m7_rows);
    return 1;
}

/* The last frame, presenting Mode 7 or a canvas, drawn at ow x oh over d
   by the renderer's compositor (its picture and records the GPU's); 0
   when it is not drawn so (no compositor, NSHVC_COMPOSE=0, an interlaced
   weave, or no Mode 7 or canvas presented) */
static int show_mode7(int ow, int oh, const int d[4])
{
    static RrMode7 m;
    static RrM7Samples smp;
    const PpuFrame *f = ppu_frame();
    const char *e = getenv("NSHVC_COMPOSE");
    size_t nw;
    const uint32_t *desc;
    if ((e && !strcmp(e, "0")) || ow <= 0 || oh <= 0 || f->versions == 0 || f->interlace || !ppu_frame_mode7(f))
        return 0;
    desc = ppu_compose_m7(f, &nw, &m);
    if (ow > m7_cols_cap) {
        free(m7_cols);
        m7_cols = (int32_t *)malloc((size_t)ow * 3 * 4);
        m7_cols_cap = m7_cols ? ow : 0;
    }
    if (oh > m7_rows_cap) {
        free(m7_rows);
        m7_rows = (int32_t *)malloc((size_t)oh * 4 * 4);
        m7_rows_cap = m7_rows ? oh : 0;
    }
    if (!m7_cols || !m7_rows)
        nshvc_fatal("no memory for the picture at %dx%d", ow, oh);
    rr_m7_tables(&m, ow, oh, &smp, m7_cols, m7_rows);
    return rr_compose_mode7(desc, nw, &m, d, &smp);
}

/* a shot's picture (script.h): NSHVC_SCALE=S or SXxSY (headless), window
   pixels a console pixel: the frame presented as a window that size shows
   it (without it, the console's picture) */
const uint32_t *front_shot_picture(int *w, int *h, int *presented)
{
    double sx, sy;
    static uint32_t *buf;
    static size_t cap;
    int pw, ph, d[4];
    const uint32_t *px = nshvc_picture(&pw, &ph);
    RrMode7 m;
    RrM7Samples smp;
    size_t i, len;
    nk_shot_scale(&sx, &sy);
    *w = sx ? (int)(pw * sx + 0.5) : pw;
    *h = sy ? (int)(ph * sy + 0.5) : ph;
    if (*w < 1)
        *w = 1;
    if (*h < 1)
        *h = 1;
    *presented = 0;
    if (!sx)
        return px;
    *presented = 1;
    len = (size_t)*w * (size_t)*h;
    if (len > cap) {
        free(buf);
        buf = (uint32_t *)malloc(len * 4);
        cap = buf ? len : 0;
        if (!buf)
            nshvc_fatal("no memory for the picture at %dx%d", *w, *h);
    }
    d[0] = d[1] = 0;
    d[2] = *w;
    d[3] = *h;
    if (mode7_prepare(*w, *h, &m, &smp)) {
        rr_ref_mode7(buf, *w, *h, &m, d, &smp);
    } else {
        for (i = 0; i < len; i++)
            buf[i] = 0xFF000000u;
        rr_ref_image(buf, *w, *h, px, pw, ph, d);
    }
    return buf;
}

/* ---- the picture on the window ---- */
int console_picture_at(float x, float y, int *dot, int *line)
{
    const PpuFrame *f = ppu_frame();
    float ww, wh;
    int shown_at[4], shown_tw;
    nk_shown_at(shown_at, &shown_tw);
    nk_window_size(&ww, &wh);
    if (!shown_tw || ww <= 0)
        return 0;
    return nk_input_point(shown_at, (float)shown_tw / ww, f->width, f->screen_height, f->left, f->top, x, y, dot, line);
}

/* the picture onto d: Mode 7 or a canvas by the GPU's compositor, else by
   its record on the CPU's tables, else the picture (snes_console.present) */
void snes_present(const int d[4])
{
    RrMode7 m;
    RrM7Samples smp;
    int w = d[2] - d[0], h = d[3] - d[1];
    if (show_mode7(w, h, d))
        ;
    else if (mode7_prepare(w, h, &m, &smp))
        rr_mode7(&m, d, &smp);
    else
        show_picture(d);
}

/* the window's picture shape: the console's 256 x 224 at a television's
   4:3, a larger screen at the same shape of pixel */
double snes_aspect(void)
{
    const PpuFrame *f = ppu_frame();
    return 4.0 / 3.0 * ((double)f->width / PPU_WIDTH) * (224.0 / (double)f->screen_height);
}

void snes_scaled(int w, int h) { canvas_scale(w, h); }

/* NSHVC_RRCHECK on a frame with Mode 7 or a canvas: the compositor's
   records where the renderer has one, against rr_ref_mode7 */
static RrMode7 rc_m;
static RrM7Samples rc_smp;
static int rc_recorded;

int snes_rrcheck(const int d[4], uint32_t *want, int tw, int th)
{
    int pw, ph, gpu;
    ppu_picture(&pw, &ph);
    rc_recorded = 0;
    if (!(ppu_frame_mode7(ppu_frame()) && ph == ppu_frame()->screen_height))
        return 0;
    rc_recorded = 1;
    /* the compositor's records where the renderer has one (drawn first:
       the reference's are made over the same tables) */
    gpu = show_mode7(tw, th, d);
    if (!mode7_prepare(tw, th, &rc_m, &rc_smp))
        nshvc_fatal("rrcheck: no record for the frame");
    if (!gpu)
        rr_mode7(&rc_m, d, &rc_smp);
    rr_ref_mode7(want, tw, th, &rc_m, d, &rc_smp);
    return 1;
}

/* NSHVC_RRCHECK_WHY: the record of the picture pixel under (X, Y) */
void snes_rrcheck_why(int X, int Y)
{
    int tx, ty, u, v;
    float fx, fy;
    if (!rc_recorded)
        return;
    tx = rc_smp.cols[3 * X], ty = rc_smp.rows[4 * Y], u = rc_smp.cols[3 * X + 2], v = rc_smp.rows[4 * Y + 3];
    memcpy(&fx, &rc_smp.cols[3 * X + 1], 4);
    memcpy(&fy, &rc_smp.rows[4 * Y + 1], 4);
    fprintf(stderr, "; picture (%d, %d) fx %.3f fy %.3f u %d v %d; k %d, %zu texels", tx, ty, fx, fy, u, v,
            rc_m.cv_k, rc_m.cv_len);
    for (int dy = 0; dy < 2; dy++)
        for (int dx = 0; dx < 2; dx++) {
            int ii = tx + dx < 0 ? 0 : tx + dx >= rc_m.pw ? rc_m.pw - 1 : tx + dx;
            int rr = ty + dy < 0 ? 0 : ty + dy >= rc_m.ph ? rc_m.ph - 1 : ty + dy;
            const RrM7Px *q = &rc_m.px[(size_t)rr * (size_t)rc_m.pw + (size_t)ii];
            fprintf(stderr, "\n  (%d, %d): %08x %08x %08x %08x", ii, rr, q->w0, q->w1, q->w2, q->w3);
            if (!(q->w2 & (RR_M7_NOT | RR_M7_BLACK))) {
                const RrM7Seg *g = &rc_m.seg[q->w2 >> 16];
                fprintf(stderr, " seg %u: %d %d %d %d %08x %08x %d %08x", q->w2 >> 16, g->s[0], g->s[1], g->s[2],
                        g->s[3], (unsigned)g->s[4], (unsigned)g->s[5], g->s[6], (unsigned)g->s[7]);
            }
        }
}

/* NSHVC_PPUCHECK=1: the frames drawn from their records against the
   picture drawn a line at a time (ppu.c), at exit */
void snes_at_exit(void)
{
    if (getenv("NSHVC_PPUCHECK"))
        fprintf(stderr, "ppucheck: %lu frames, %lu differ; %lu needed more copies than kept\n", ppu_check_frames,
                ppu_check_bad, ppu_version_overflow);
    if (getenv("NSHVC_COMPOSECHECK"))
        fprintf(stderr, "composecheck: %lu frames, %lu differ\n", ppu_compose_frames, ppu_compose_bad);
}
