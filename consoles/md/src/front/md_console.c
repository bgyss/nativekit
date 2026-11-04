/*
 * md_console.c: the nativekit console adapter. Remaining Stage 2 work fills
 * in VDP ports, rendering, and variable frame dimensions.
 */
#include <stdint.h>

#include "nk/nk.h"
#include "nk/fatal.h"
#include "nmd/nmd.h"
#include "md_internal.h"
#include "../sound/sound.h"
#include "md_console.h"
#include "../vdp/render.h"
#include "md_present.h"

static const nmd_cart *active_cart;
static const nk_button buttons[] = {
    { "UP", NMD_BUTTON_UP },
    { "DOWN", NMD_BUTTON_DOWN },
    { "LEFT", NMD_BUTTON_LEFT },
    { "RIGHT", NMD_BUTTON_RIGHT },
    { "B", NMD_BUTTON_B },
    { "C", NMD_BUTTON_C },
    { "A", NMD_BUTTON_A },
    { "START", NMD_BUTTON_START },
    { "X", NMD_BUTTON_X },
    { "Y", NMD_BUTTON_Y },
    { "Z", NMD_BUTTON_Z },
    { "MODE", NMD_BUTTON_MODE },
};

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
    md_vdp_dimensions(w, h);
}

static double aspect(void)
{
    int width, height;
    double logical_height;
    md_vdp_dimensions(&width, &height);
    logical_height = (double)height;
    if ((md_vdp_register(12) & 0x06u) == 0x06u)
        logical_height /= 2.0;
    return 4.0 / 3.0 * ((double)width / 256.0) * (224.0 / logical_height);
}

static void present(const int dst[4])
{
    md_present(dst);
}

static const uint32_t *picture(int *w, int *h)
{
    return md_vdp_picture(w, h);
}

static const uint32_t *shot(int *w, int *h, int *xs, int *ys)
{
    *xs = 1;
    *ys = 1;
    return picture(w, h);
}

static int audio_take(int16_t *lr, int max)
{
    return md_sound_audio_take(lr, max);
}

static int audio_queued(void)
{
    return md_sound_audio_queued();
}

static void input(int headless, const uint32_t *buttons)
{
    (void)headless;
    md_io_input(buttons);
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
    .audio_hz = MD_SOUND_OUTPUT_RATE,
    .audio_take = audio_take,
    .audio_queued = audio_queued,
    .players = 2,
    .buttons = buttons,
    .button_count = (int)(sizeof buttons / sizeof buttons[0]),
    .input = input,
};

int nmd_main(const nmd_cart *cart, int argc, char **argv)
{
    return nk_main(&nmd_console, cart, argc, argv);
}

void nmd_power(const nmd_cart *cart)
{
    active_cart = cart;
    md_io_input(NULL);
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
