/*
 * snes_console.c: the SNES as nativekit's console: the machine, its
 * picture (snes_present.c), its sound, its controllers (console.c), its
 * script steps and buttons, its memories for dumps and its battery save,
 * its shots.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "nshvc/nshvc.h"
#include "nshvc/cart.h"
#include "snes_internal.h"
#include "ppu.h"
#include "snes_console.h"

/* ---- the script ---- */
static const nk_key keys[] = {
    { 0x26, 1u << 12, "Up" }, { 0x28, 1u << 14, "Down" }, { 0x25, 1u << 15, "Left" },
    { 0x27, 1u << 13, "Right" }, { 'Z', 1u << 0, "Z" }, { 'X', 1u << 1, "X" }, { 'A', 1u << 2, "A" },
    { 'S', 1u << 3, "S" }, { 'Q', 1u << 4, "Q" }, { 'W', 1u << 5, "W" }, { 0x0D, 1u << 7, "Return" },
    { 0x10, 1u << 6, "Right Shift" }, { 0x09, 1u << 6, "Tab" },
};
static const char *const kind_names[] = { "wram", "rom", "cart ram", "io" };
_Static_assert(sizeof kind_names / sizeof kind_names[0] == NSHVC_KINDS, "access kind names");
static const nk_button buttons[] = {
    { "B", NSHVC_B }, { "Y", NSHVC_Y }, { "SELECT", NSHVC_SELECT }, { "START", NSHVC_START },
    { "UP", NSHVC_UP }, { "DOWN", NSHVC_DOWN }, { "LEFT", NSHVC_LEFT }, { "RIGHT", NSHVC_RIGHT },
    { "A", NSHVC_A }, { "X", NSHVC_X }, { "L", NSHVC_L }, { "R", NSHVC_R },
};
static const char *const steps[] = { "plug", "pad", "mouse", "gun" };

/* plug PORT DEVICE (joypad, none, mouse, multitap, superscope, justifier,
   justifiers); pad N BITS (joypad N's buttons from now, hex; 0 and 1 are
   hold's); mouse PORT DX DY BUTTONS (motion for its next report); gun G X
   Y BUTTONS (from now) */
static int script_step(int n, char w[][1024])
{
    static const char *const dev[] = { "joypad", "none", "mouse", "multitap", "superscope", "justifier",
                                       "justifiers" };
    int i;
    if (!strcmp(w[0], "plug") && n == 3) {
        for (i = 0; i < 7; i++)
            if (!strcmp(w[2], dev[i])) {
                nshvc_plug(atoi(w[1]), i);
                return 1;
            }
    } else if (!strcmp(w[0], "pad") && n == 3) {
        nshvc_set_joypad(atoi(w[1]), (uint16_t)strtoul(w[2], NULL, 16));
        return 1;
    } else if (!strcmp(w[0], "mouse") && n == 5) {
        nshvc_set_mouse(atoi(w[1]), atoi(w[2]), atoi(w[3]), atoi(w[4]));
        return 1;
    } else if (!strcmp(w[0], "gun") && n == 5) {
        nshvc_set_gun(atoi(w[1]), atoi(w[2]), atoi(w[3]), (unsigned)atoi(w[4]));
        return 1;
    }
    return 0;
}

/* A shot is the picture made the way the reference's are (tests/oracle):
   an interlaced picture whose two fields' rows are the same is its lines
   once, a hires one of the console's width whose pixel pairs are all the
   same 256 wide. A presented one (NSHVC_SCALE) is as presented. */
static const uint32_t *shot(int *w, int *h, int *xs, int *ys)
{
    int x, y, presented;
    const uint32_t *px = front_shot_picture(w, h, &presented);
    const PpuFrame *fr = ppu_frame();
    *xs = *ys = 1;
    if (!presented && fr->interlace && *h == 2 * fr->screen_height) {
        *ys = 2;
        for (y = 0; y + 1 < *h && *ys == 2; y += 2)
            if (memcmp(px + (size_t)y * (size_t)*w, px + (size_t)(y + 1) * (size_t)*w, (size_t)*w * sizeof *px))
                *ys = 1;
    }
    if (!presented && fr->width == PPU_WIDTH && *w == PPU_HIRES_WIDTH) {
        *xs = 2;
        for (y = 0; y < *h && *xs == 2; y++)
            for (x = 0; x < *w; x += 2)
                if (px[y * *w + x] != px[y * *w + x + 1]) {
                    *xs = 1;
                    break;
                }
    }
    return px;
}

/* ---- the memories a dump writes, in its order ---- */
static const void *r_wram(size_t *n) { *n = 0x20000; return snes_wram(); }
static const void *r_sram(size_t *n) { *n = nshvc_sram() ? snes_sram_size() : 0; return nshvc_sram(); }
static const void *r_chipram(size_t *n)
{
    uint32_t g = 0;
    *n = 0;
    if (!snes_cart.chip || !snes_cart.chip->ram)
        return NULL;
    {
        uint8_t *p = snes_cart.chip->ram(&g);
        *n = g;
        return p;
    }
}
static const void *r_vram(size_t *n) { *n = 0x10000 * PPU_VRAM_PAGES; return nshvc_vram(); }
static const void *r_cgram(size_t *n) { *n = 0x200; return nshvc_cgram(); }
static const void *r_oam(size_t *n) { *n = 544; return nshvc_oam(); }

static const nk_region regions[] = {
    { ".wram", r_wram, 0 }, { ".sram", r_sram, 0 }, { ".chipram", r_chipram, 1 },
    { ".vram", r_vram, 0 }, { ".cgram", r_cgram, 0 }, { ".oam", r_oam, 0 },
};

/* ---- the machine ---- */
static void power(const void *cart) { nshvc_power((const nshvc_cart *)cart); }
static uint64_t frame_ns(void) { return snes_pal() ? 19997194ull : 16639267ull; }
/* the save folder: the save name, else the title (NULL when both are);
   the window's title: the title, else "nativeshvc", which is snes_cart.title
   as nshvc_power leaves it */
static void app_name(const void *cart, const char **dir, const char **title)
{
    const nshvc_cart *k = (const nshvc_cart *)cart;
    *dir = k->save_name ? k->save_name : k->title;
    *title = k->title ? k->title : "nativeshvc";
}
static void window_size(int *w, int *h)
{
    *w = PPU_WIDTH;
    *h = 224;
}
/* shots as a window NSHVC_SCALE pixels a console pixel shows them: the
   canvas's scale that window's (nshvc_ext_canvas_scale) */
static void headless_scale(double sx, double sy) { ppu_set_canvas_scale((int)ceil(sx > sy ? sx : sy)); }

/* ---- the battery save: the cartridge's RAM, as SAVE_NAME.srm ---- */
static int battery(nk_battery *b)
{
    uint32_t n = snes_sram_size();
    if (!snes_cart.battery || !n || !snes_sram())
        return 0;
    b->ram = snes_sram();
    b->bytes = n;
    b->from = snes_cart.save_size ? snes_cart.save_from : 0;
    b->size = snes_cart.save_size;
    b->name = snes_cart.save_name ? snes_cart.save_name : snes_cart.title;
    return 1;
}

static void counts_hooks(void (*rd)(uint32_t, uint8_t), void (*wr)(uint32_t, uint8_t))
{
    nshvc_set_read_hook(rd);
    nshvc_set_write_hook(wr);
}

const nk_console snes_console = {
    .name = "nativeshvc", .env = "NSHVC", .play_ext = ".sfx",
    .power = power, .run_frame = nshvc_run_frame, .frame_ns = frame_ns, .app_name = app_name,
    .window_size = window_size, .aspect = snes_aspect, .scaled = snes_scaled, .present = snes_present,
    .shot = shot, .rrcheck = snes_rrcheck, .rrcheck_why = snes_rrcheck_why,
    .rrcheck_own = "with Mode 7 or a canvas presented",
    .picture = nshvc_picture, .headless_scale = headless_scale,
    .audio_hz = NSHVC_DSP_RATE, .audio_take = nshvc_audio_take, .audio_queued = nshvc_audio_queued,
    .players = 2, .keys = keys, .key_count = sizeof keys / sizeof keys[0],
    .buttons = buttons, .button_count = sizeof buttons / sizeof buttons[0],
    .steps = steps, .step_count = sizeof steps / sizeof steps[0], .script_step = script_step,
    .key = snes_key, .touch = snes_touch, .input = snes_input, .frame = snes_game_frame,
    .regions = regions, .region_count = sizeof regions / sizeof regions[0],
    .battery = battery, .counts_hooks = counts_hooks, .access_kind = nshvc_access_kind,
    .pointer = snes_pointer,
    .kind_count = NSHVC_KINDS, .kind_names = kind_names, .at_exit = snes_at_exit,
};
