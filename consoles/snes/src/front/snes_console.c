/*
 * snes_console.c: the SNES as nativekit's console: its script steps and
 * buttons, its memories for dumps, its shots.
 */
#include <stdlib.h>
#include <string.h>

#include "nshvc/nshvc.h"
#include "nshvc/cart.h"
#include "snes_internal.h"
#include "ppu.h"
#include "snes_console.h"

/* ---- the script ---- */
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

const nk_console snes_console = {
    .name = "nativeshvc", .env = "NSHVC", .play_ext = ".sfx",
    .shot = shot,
    .buttons = buttons, .button_count = sizeof buttons / sizeof buttons[0],
    .steps = steps, .step_count = 4, .script_step = script_step,
    .regions = regions, .region_count = sizeof regions / sizeof regions[0],
};
