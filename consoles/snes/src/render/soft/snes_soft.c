/*
 * snes_soft.c: the SNES's picture on the software backend: no compositor
 * (the caller draws the picture), Mode 7 and the canvas by rr_mode7.h's
 * reference drawing onto the target.
 */
#include "snes_rr.h"
#include "rr_mode7.h"
#include "nk/rr_soft_ext.h"

int rr_compose(const uint32_t *desc, size_t words, int w, int h, const int d[4])
{
    (void)desc;
    (void)words;
    (void)w;
    (void)h;
    (void)d;
    return 0;   /* no compositor: the caller draws the picture */
}

int rr_compose_woven(const uint32_t *desc, size_t words, int ww, int wh, uint32_t base0, uint32_t base1,
                     const int d[4])
{
    (void)desc;
    (void)words;
    (void)ww;
    (void)wh;
    (void)base0;
    (void)base1;
    (void)d;
    return 0;   /* no compositor */
}

int rr_compose_mode7(const uint32_t *desc, size_t words, const struct RrMode7 *m, const int d[4],
                     const struct RrM7Samples *s)
{
    (void)desc;
    (void)words;
    (void)m;
    (void)d;
    (void)s;
    return 0;   /* no compositor: the caller makes the records and picture */
}

void rr_mode7(const struct RrMode7 *m, const int d[4], const struct RrM7Samples *s)
{
    int w, h;
    uint32_t *px = rr_soft_target(&w, &h);
    rr_ref_mode7(px, w, h, m, d, s);
}
