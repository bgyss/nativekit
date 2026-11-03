/* Mode 5 plane A converts packed tile pixels through CRAM to ARGB. */
#include <assert.h>
#include <stdint.h>

#include <nmd/hw.h>
#include <nmd/nmd.h>

#include "md_console.h"
#include "md_internal.h"
#include "render.h"

static uint8_t rom[0x400];

static void reg_write(unsigned reg, uint8_t value)
{
    wr16(NMD_VDP_CONTROL, (uint16_t)(0x8000u | (reg << 8) | value));
}

static void control(uint32_t command)
{
    wr32(NMD_VDP_CONTROL, command);
}

static void test_mode5_plane_a_pixel(nmd_cart *cart)
{
    const uint32_t *picture;
    int width, height, y;

    nmd_power(cart);
    nmd_wait_vblank();
    reg_write(0, 0x04); /* Mode 5. */
    reg_write(1, 0x44); /* Display and Mode 5 on. */
    reg_write(2, 0x10); /* Plane A table at $4000. */
    reg_write(15, 2);

    control(0x40000000); /* VRAM pattern generator at $0000. */
    for (y = 0; y < 8; y++) {
        wr16(NMD_VDP_DATA, 0x1111); /* Packed nibbles set every pixel to color 1. */
        wr16(NMD_VDP_DATA, 0x1111);
    }
    control(0x41000000); /* Plane A tile map. */
    wr16(NMD_VDP_DATA, 0x0000); /* Tile 0, palette 0. */
    control(0xC0020000); /* CRAM palette 0, color 1. */
    wr16(NMD_VDP_DATA, 0x000E); /* Full-intensity red. */

    nmd_run_frame();
    picture = md_vdp_picture(&width, &height);
    assert(picture != NULL);
    assert(width == 256 && height == 224);
    assert(picture[0] == 0xFFFF0000u);
}

static void test_picture_dimensions(nmd_cart *cart)
{
    nmd_cart pal = *cart;
    int width, height;

    nmd_power(cart);
    (void)md_vdp_picture(&width, &height);
    assert(width == 256 && height == 224);
    md_vdp_set_register(12, 0x81);
    (void)md_vdp_picture(&width, &height);
    assert(width == 320 && height == 224);

    pal.region = NMD_PAL;
    nmd_power(&pal);
    md_vdp_set_register(1, 0x08);
    (void)md_vdp_picture(&width, &height);
    assert(width == 256 && height == 240);
    md_vdp_set_register(12, 0x81);
    (void)md_vdp_picture(&width, &height);
    assert(width == 320 && height == 240);
}

static void test_sprite_over_plane_pixel(nmd_cart *cart)
{
    const uint32_t *picture;
    int width, height, y;

    nmd_power(cart);
    nmd_wait_vblank();
    reg_write(0, 0x04);
    reg_write(1, 0x44);
    reg_write(2, 0x10);
    reg_write(5, 0x7F); /* H32 sprite table at $FE00. */
    reg_write(15, 2);

    control(0x40000000);
    for (y = 0; y < 8; y++) {
        wr16(NMD_VDP_DATA, 0x1111);
        wr16(NMD_VDP_DATA, 0x1111);
    }
    control(0x40200000);
    for (y = 0; y < 8; y++) {
        wr16(NMD_VDP_DATA, 0x2222);
        wr16(NMD_VDP_DATA, 0x2222);
    }
    control(0x41000000); /* Plane A uses red tile 0. */
    wr16(NMD_VDP_DATA, 0x0000);
    control(0xC0020000);
    wr16(NMD_VDP_DATA, 0x000E);
    wr16(NMD_VDP_DATA, 0x00E0);
    assert(md_vdp_code() == 3);
    control(0x7E000003); /* Sprite 0: top-left, 1x1 cell, tile 1. */
    assert(md_vdp_code() == 1);
    wr16(NMD_VDP_DATA, 0x0080);
    wr16(NMD_VDP_DATA, 0x0000);
    wr16(NMD_VDP_DATA, 0x0001);
    wr16(NMD_VDP_DATA, 0x0080);

    nmd_run_frame();
    picture = md_vdp_picture(&width, &height);
    assert(width == 256 && height == 224);
    assert(picture[0] == 0xFF00FF00u);
}

static void test_interlace_two_field_output(nmd_cart *cart)
{
    const uint32_t *picture;
    int width, height, shot_width, shot_height, xs, ys;

    nmd_power(cart);
    nmd_wait_vblank();
    reg_write(0, 0x04);
    reg_write(1, 0x44);
    reg_write(2, 0x10);
    reg_write(12, 0x06); /* Interlace mode 2: doubled vertical raster. */
    reg_write(15, 2);
    control(0x40000000);
    for (int y = 0; y < 16; y++) {
        wr16(NMD_VDP_DATA, 0x1111);
        wr16(NMD_VDP_DATA, 0x1111);
    }
    control(0x41000000);
    wr16(NMD_VDP_DATA, 0x0000);
    control(0xC0020000);
    wr16(NMD_VDP_DATA, 0x000E);

    nmd_run_frame();
    picture = md_vdp_picture(&width, &height);
    assert(width == 256 && height == 448);
    assert(nmd_console.window_size != NULL);
    nmd_console.window_size(&shot_width, &shot_height);
    assert(shot_width == 256 && shot_height == 448);
    assert(nmd_console.shot(&shot_width, &shot_height, &xs, &ys) == picture);
    assert(shot_width == 256 && shot_height == 448 && xs == 1 && ys == 1);
    assert(picture[0] == 0xFF000000u);
    assert(picture[256] == 0xFFFF0000u);

    nmd_run_frame();
    picture = md_vdp_picture(&width, &height);
    assert(picture[0] == 0xFFFF0000u);
    assert(picture[256] == 0xFFFF0000u);
}

int main(void)
{
    nmd_cart cart = {
        .title = "Render check",
        .region = NMD_NTSC,
        .rom = rom,
        .rom_size = sizeof rom,
    };
    test_mode5_plane_a_pixel(&cart);
    test_picture_dimensions(&cart);
    test_sprite_over_plane_pixel(&cart);
    test_interlace_two_field_output(&cart);
    return 0;
}
