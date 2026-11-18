/* Headless target size shared by renderer backends. */
#include "fatal.h"
#include "rr.h"
#include "rr_size.h"

static int s_w, s_h;

void rr_set_headless_size(int w, int h)
{
    s_w = w;
    s_h = h;
}

void rr_headless_default(int *w, int *h)
{
    if (s_w < 1 || s_h < 1)
        nk_fatal("no headless size: rr_set_headless_size before rr_open, or RR_SIZE=WxH");
    *w = s_w;
    *h = s_h;
}
