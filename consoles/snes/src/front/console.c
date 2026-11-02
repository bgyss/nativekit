/*
 * console.c: the console's public calls (nshvc.h), the game's hooks and
 * buttons, and nshvc_main: the application is the kit's (nk_main,
 * kit/src/front/app.c) with the SNES's table (snes_console.c), whose
 * controllers and game hooks are here and picture in snes_present.c.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nshvc/nshvc.h"
#include "snes_internal.h"
#include "host.h"
#include "snes_console.h"
#include "nk/input.h"
#include "snes_input.h"
#if defined(NSHVC_RENDER_VULKAN)
#include "rr_vulkan_ext.h"
#endif
#include "snes_rr.h"

/* ---- the public calls ---- */
const uint32_t *nshvc_picture(int *width, int *height) { return ppu_picture(width, height); }
uint32_t nshvc_frames(void) { return (uint32_t)ppu_frames(); }
int nshvc_audio_take(int16_t *lr, int max) { return snes_audio_take(lr, max); }
int nshvc_audio_queued(void) { return snes_audio_queued(); }
uint8_t *nshvc_wram(void) { return snes_wram_rw(); }
/* the cartridge's RAM as it stands (a chip that owns it brought up to now) */
uint8_t *nshvc_sram(void) { return snes_sram(); }
const uint8_t *nshvc_aram(void) { return snes_aram(); }
const uint16_t *nshvc_vram(void) { return ppu_vram(); }
const uint16_t *nshvc_cgram(void) { return ppu_cgram(); }
const uint8_t *nshvc_oam(void) { return ppu_oam(); }
uint8_t nshvc_peek(uint32_t addr) { return snes_peek(addr); }
void nshvc_ext_screen(int width, int height) { ppu_ext_screen(width, height); }
void nshvc_ext_screen_origin(int x, int y) { ppu_ext_screen_origin(x, y); }
void nshvc_ext_sprite_x(int sprite, int on) { ppu_ext_sprite_x(sprite, on); }
void nshvc_ext_sprite_y(int sprite, int on) { ppu_ext_sprite_y(sprite, on); }
void nshvc_ext_sprite_page(int sprite, int x_page, int y_page) { ppu_ext_sprite_page(sprite, x_page, y_page); }
void nshvc_ext_sprite_limits(int on) { ppu_ext_sprite_limits(on); }
void nshvc_ext_sprite_wrap(int on) { ppu_ext_sprite_wrap(on); }
void nshvc_ext_vram(int kb) { ppu_ext_vram(kb); }
void nshvc_ext_vram_page(int page) { ppu_ext_vram_page(page); }
void nshvc_ext_layer_page(int layer, int page) { ppu_ext_layer_page(layer, page); }
void nshvc_ext_layer_clip(int layer, int on) { ppu_ext_layer_clip(layer, on); }
void nshvc_ext_layer_outside(int layer, const uint16_t *map, int size) { ppu_ext_layer_outside(layer, map, size); }
void nshvc_ext_vram_copy(int to_page, int from_page) { ppu_ext_vram_copy(to_page, from_page); }
void nshvc_ext_vram_direct(int page) { ppu_ext_vram_direct(page); }
int nshvc_ext_canvas_scale(void) { return ppu_canvas_scale(); }
void nshvc_ext_canvas_chars(int layer, unsigned word_addr, int count, const uint8_t *texels, int k)
{
    ppu_ext_canvas_chars(layer, word_addr, count, texels, k);
}
void nshvc_ext_canvas_screen(int layer, const uint8_t *texels, int w, int h, int k, int priority, int palette)
{
    ppu_ext_canvas_screen(layer, texels, w, h, k, priority, palette);
}

void nshvc_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    snes_vfatal(fmt, ap);
}

/* ---- the game's hooks ---- */
static nshvc_hooks hooks;
void nshvc_set_hooks(const nshvc_hooks *h)
{
    if (h)
        hooks = *h;
    else
        memset(&hooks, 0, sizeof hooks);
}
const char *nshvc_save_dir(void) { return host_save_dir(); }
void nshvc_safe_area(float *l, float *t, float *r, float *b) { host_safe_area(l, t, r, b); }

void nshvc_window_size(float *w, float *h) { nk_window_size(w, h); }

/* ---- the game's buttons and overlays ---- */
static uint16_t game_buttons[8];
void nshvc_game_buttons(int player, uint16_t buttons)
{
    if (player >= 0 && player < 8)
        game_buttons[player] = buttons;
}

_Static_assert(NSHVC_OVERLAYS == NK_OVERLAYS, "the same overlays");
int nshvc_overlay_image(const uint32_t *argb, int w, int h) { return nk_overlay_image(argb, w, h); }
void nshvc_overlay_place(int id, float cx, float cy, float scale, float angle_rad, float alpha, int visible)
{
    nk_overlay_place(id, cx, cy, scale, angle_rad, alpha, visible);
}

/* ---- the table's controllers (snes_console.c) ---- */
/* the players' controllers from the frame's input: headless, the
   script's buttons with the game's; else the kit's devices with the
   game's (snes_input.c); then the players whose device changed to the
   game's input hook */
void snes_input(int headless, const uint32_t b[2])
{
    if (headless) {
        nshvc_set_joypad(0, (uint16_t)(b[0] | game_buttons[0]));
        nshvc_set_joypad(1, (uint16_t)(b[1] | game_buttons[1]));
        nk_input_script();
    } else {
        snes_input_frame(NULL, NULL, game_buttons);
    }
    snes_input_report(hooks.input);
}

/* a key: the game's hook first (1: it took it), then the console's own
   (snes_input_key, 0 for every key: on to the kit's nk_input_key) */
int snes_key(int vk, int down)
{
    if (hooks.key && hooks.key(vk, down))
        return 1;
    return snes_input_key(vk, down);
}

void snes_touch(int id, int phase, float x, float y)
{
    if (hooks.touch) {
        nshvc_touch t;
        t.id = id;
        t.phase = phase;
        t.x = x;
        t.y = y;
        hooks.touch(&t, 1);
    }
}

void snes_game_frame(void)
{
    if (hooks.frame)
        hooks.frame();
}

int nshvc_main(const nshvc_cart *cart, int argc, char **argv)
{
    /* the GPU backend's own SNES work, made when it opens (the software
       backend has none) */
#if defined(NSHVC_RENDER_METAL)
    rr_set_open_hook(snes_metal_open);
#elif defined(NSHVC_RENDER_GL)
    rr_set_open_hook(snes_gl_open);
#elif defined(NSHVC_RENDER_VULKAN)
    rr_vk_set_app_name("nativeshvc");
    rr_set_open_hook(snes_vulkan_open);
#endif
    return nk_main(&snes_console, cart, argc, argv);
}
