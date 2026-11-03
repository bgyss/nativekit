/* Software Mode 5 plane, window, scroll, and palette renderer. */
#include <stddef.h>
#include <stdint.h>

#include "render.h"
#include "vdp.h"

#define MD_RENDER_MAX_WIDTH 320
#define MD_RENDER_MAX_HEIGHT 240

static uint32_t picture[MD_RENDER_MAX_WIDTH * MD_RENDER_MAX_HEIGHT];

static unsigned plane_width_cells(void)
{
    static const unsigned sizes[] = { 32u, 64u, 32u, 128u };
    return sizes[md_vdp_register(16) & 0x03u];
}

static unsigned plane_height_cells(void)
{
    static const unsigned sizes[] = { 32u, 64u, 32u, 128u };
    return sizes[(md_vdp_register(16) >> 4) & 0x03u];
}

static uint16_t read_vram_word(uint16_t addr)
{
    return (uint16_t)((uint16_t)md_vdp_vram_byte(addr) << 8 |
                      md_vdp_vram_byte((uint16_t)(addr + 1u)));
}

static uint8_t tile_pixel(unsigned tile, unsigned x, unsigned y)
{
    uint16_t addr = (uint16_t)(((tile & 0x07FFu) << 5) + y * 4u + (x >> 1));
    uint8_t packed = md_vdp_vram_byte(addr);
    return (uint8_t)((x & 1u) ? packed & 0x0Fu : packed >> 4);
}

static uint16_t pattern_pixel(uint16_t base, unsigned table_width, unsigned table_height,
                              unsigned x, unsigned y)
{
    unsigned tile_x = (x >> 3) % table_width;
    unsigned tile_y = (y >> 3) % table_height;
    uint32_t word_offset = ((uint32_t)tile_y * table_width + tile_x) * 2u;
    uint16_t entry = read_vram_word((uint16_t)(base + word_offset));
    unsigned tile = entry & 0x07FFu;
    unsigned pixel_x = x & 7u;
    unsigned pixel_y = y & 7u;
    uint8_t color;

    if (entry & 0x0800u)
        pixel_x = 7u - pixel_x;
    if (entry & 0x1000u)
        pixel_y = 7u - pixel_y;
    color = tile_pixel(tile, pixel_x, pixel_y);
    if (!color)
        return 0;
    return (uint16_t)(((entry >> 9) & 0x30u) | color);
}

static uint16_t plane_pixel(unsigned plane, unsigned x, unsigned y)
{
    unsigned width_cells = plane_width_cells();
    unsigned height_cells = plane_height_cells();
    unsigned scroll_mode = md_vdp_register(11) & 0x03u;
    unsigned scroll_row = 0;
    unsigned scroll_addr;
    unsigned hscroll;
    unsigned vscroll_index;
    unsigned vscroll;
    uint16_t base;
    unsigned source_x;
    unsigned source_y;

    if (scroll_mode == 2u)
        scroll_row = y >> 3;
    else if (scroll_mode == 3u)
        scroll_row = y;
    scroll_addr = ((unsigned)(md_vdp_register(13) & 0x3Fu) << 10) +
                  scroll_row * 4u + plane * 2u;
    hscroll = read_vram_word((uint16_t)scroll_addr) & 0x03FFu;

    if (md_vdp_register(11) & 0x04u)
        vscroll_index = ((x >> 4) * 2u + plane) % 40u;
    else
        vscroll_index = plane;
    vscroll = md_vdp_vsram_value(vscroll_index) & 0x07FFu;

    if (!plane)
        base = (uint16_t)((md_vdp_register(2) & 0x38u) << 10);
    else
        base = (uint16_t)((md_vdp_register(4) & 0x07u) << 13);

    source_x = (x + width_cells * 8u - (hscroll % (width_cells * 8u))) %
               (width_cells * 8u);
    source_y = (y + vscroll) % (height_cells * 8u);
    return pattern_pixel(base, width_cells, height_cells, source_x, source_y);
}

static int in_window(unsigned x, unsigned y)
{
    unsigned hpos = (md_vdp_register(17) & 0x1Fu) * 16u;
    unsigned vpos = (md_vdp_register(18) & 0x1Fu) * 8u;
    int hwindow = (md_vdp_register(17) & 0x80u) ? x >= hpos : x < hpos;
    int vwindow = (md_vdp_register(18) & 0x80u) ? y >= vpos : y < vpos;
    return hwindow || vwindow;
}

static uint16_t window_pixel(unsigned x, unsigned y)
{
    int width, height;
    unsigned table_width;
    uint16_t base;
    md_vdp_dimensions(&width, &height);
    (void)height;
    table_width = width == 320 ? 64u : 32u;
    base = (uint16_t)((md_vdp_register(3) & 0x3Eu) << 10);
    return pattern_pixel(base, table_width, 32u, x, y);
}

static uint32_t cram_color(unsigned color_index)
{
    uint16_t color = md_vdp_cram_value(color_index & 0x3Fu);
    unsigned red = ((color >> 1) & 7u) * 255u / 7u;
    unsigned green = ((color >> 5) & 7u) * 255u / 7u;
    unsigned blue = ((color >> 9) & 7u) * 255u / 7u;
    return 0xFF000000u | (red << 16) | (green << 8) | blue;
}

static uint32_t palette_pixel(uint16_t pattern)
{
    unsigned index = pattern ? pattern : (md_vdp_register(7) & 0x3Fu);
    return cram_color(index);
}

void md_vdp_render_reset(void)
{
    size_t i;
    for (i = 0; i < (size_t)MD_RENDER_MAX_WIDTH * MD_RENDER_MAX_HEIGHT; i++)
        picture[i] = 0xFF000000u;
}

void md_vdp_render_line(unsigned line)
{
    int width, height;
    unsigned x;
    md_vdp_dimensions(&width, &height);
    if (line >= (unsigned)height || width > MD_RENDER_MAX_WIDTH)
        return;

    for (x = 0; x < (unsigned)width; x++) {
        uint16_t plane_a = 0;
        uint16_t plane_b;
        uint16_t selected;

        if (in_window(x, line))
            plane_a = window_pixel(x, line);
        else
            plane_a = plane_pixel(0, x, line);
        plane_b = plane_pixel(1, x, line);
        selected = plane_a ? plane_a : plane_b;
        picture[(size_t)line * (size_t)width + x] = palette_pixel(selected);
    }
}

const uint32_t *md_vdp_picture(int *width, int *height)
{
    md_vdp_dimensions(width, height);
    return picture;
}
