/*
 * md_console.c: the nativekit console adapter. Remaining Stage 2 work fills
 * in VDP ports, rendering, and variable frame dimensions.
 */
#include <stdint.h>

#include "nk/nk.h"
#include "nk/fatal.h"
#include "nmd/nmd.h"
#include "md_internal.h"
#include "md_console.h"

static const nmd_cart *active_cart;
static uint32_t blank_picture[256 * 224];

static void power(const void *cart)
{
    nmd_power((const nmd_cart *)cart);
}

static void app_name(const void *cart, const char **dir, const char **title)
{
    const nmd_cart *k = (const nmd_cart *)cart;
    *title = k && k->title ? k->title : "nativemd";
    *dir = *title;
}

static uint64_t frame_ns(void)
{
    uint64_t mclk = active_cart && active_cart->region == NMD_PAL ? 53203395ull : 53693175ull;
    uint64_t lines = active_cart && active_cart->region == NMD_PAL ? 312ull : 262ull;
    return 3420ull * lines * 1000000000ull / mclk;
}

static void window_size(int *w, int *h)
{
    *w = 256;
    *h = 224;
}

static double aspect(void)
{
    return 4.0 / 3.0;
}

static void present(const int dst[4])
{
    (void)dst;
}

static const uint32_t *picture(int *w, int *h)
{
    *w = 256;
    *h = 224;
    return blank_picture;
}

static const uint32_t *shot(int *w, int *h, int *xs, int *ys)
{
    *xs = 1;
    *ys = 1;
    return picture(w, h);
}

static int audio_take(int16_t *lr, int max)
{
    (void)lr;
    (void)max;
    return 0;
}

static int audio_queued(void)
{
    return 0;
}

static void input(int headless, const uint32_t *buttons)
{
    (void)headless;
    (void)buttons;
}

const nk_console nmd_console = {
    .name = "nativemd",
    .env = "NMD",
    .play_ext = ".nmd",
    .power = power,
    .run_frame = nmd_run_frame,
    .frame_ns = frame_ns,
    .app_name = app_name,
    .window_size = window_size,
    .aspect = aspect,
    .present = present,
    .shot = shot,
    .picture = picture,
    .audio_hz = 44100.0,
    .audio_take = audio_take,
    .audio_queued = audio_queued,
    .players = 2,
    .input = input,
};

int nmd_main(const nmd_cart *cart, int argc, char **argv)
{
    return nk_main(&nmd_console, cart, argc, argv);
}

void nmd_power(const nmd_cart *cart)
{
    active_cart = cart;
    md_bus_reset(cart);
    md_timing_reset(cart);
}

void nmd_run_frame(void)
{
    if (!active_cart)
        nk_fatal("nmd_run_frame before nmd_power");
    md_advance_to(md_vdp_next_vblank_clock());
    md_irq_dispatch_pending();
}
