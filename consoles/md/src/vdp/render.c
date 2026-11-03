/* Software Mode 5 plane, window, scroll, and palette renderer. */
#include <stddef.h>
#include <stdint.h>

#include "render.h"
#include "md_internal.h"
#include "vdp.h"

#define MD_RENDER_MAX_WIDTH 320
#define MD_RENDER_MAX_HEIGHT 480
#define MD_RENDER_PRIORITY 0x8000u
#define MD_RENDER_SHADOW 0x4000u
#define MD_RENDER_HIGHLIGHT 0x2000u
#define MD_RENDER_COLOR 0x003Fu
#define MD_RENDER_NO_LINE 0xFFFFFFFFu

static uint32_t picture[MD_RENDER_MAX_WIDTH * MD_RENDER_MAX_HEIGHT];
static unsigned rendered_line = MD_RENDER_NO_LINE;
static unsigned rendered_x;

static unsigned tile_height(void)
{
    return (md_vdp_register(12) & 0x06u) == 0x06u ? 16u : 8u;
}

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
    unsigned height = tile_height();
    uint16_t addr = (uint16_t)(((tile & 0x07FFu) * height * 4u) +
                               y * 4u + (x >> 1));
    uint8_t packed = md_vdp_vram_byte(addr);
    return (uint8_t)((x & 1u) ? packed & 0x0Fu : packed >> 4);
}

static uint16_t pattern_pixel(uint16_t base, unsigned table_width, unsigned table_height,
                              unsigned x, unsigned y)
{
    unsigned cell_height = tile_height();
    unsigned tile_x = (x >> 3) % table_width;
    unsigned tile_y = (y / cell_height) % table_height;
    uint32_t word_offset = ((uint32_t)tile_y * table_width + tile_x) * 2u;
    uint16_t entry = read_vram_word((uint16_t)(base + word_offset));
    unsigned tile = entry & 0x07FFu;
    unsigned pixel_x = x & 7u;
    unsigned pixel_y = y % cell_height;
    uint8_t color;

    if (entry & 0x0800u)
        pixel_x = 7u - pixel_x;
    if (entry & 0x1000u)
        pixel_y = cell_height - 1u - pixel_y;
    color = tile_pixel(tile, pixel_x, pixel_y);
    if (!color)
        return 0;
    return (uint16_t)(((entry & 0x8000u) ? MD_RENDER_PRIORITY : 0u) |
                      ((entry >> 9) & 0x30u) | color);
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
    source_y = (y + vscroll) % (height_cells * tile_height());
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

static uint32_t cram_color(unsigned color_index, unsigned shade)
{
    static const uint8_t levels[] = {
        0, 27, 49, 71, 87, 103, 119, 130, 146, 157, 174, 190, 206, 228, 255
    };
    uint16_t color = md_vdp_cram_value(color_index & 0x3Fu);
    unsigned red = (color >> 1) & 7u;
    unsigned green = (color >> 5) & 7u;
    unsigned blue = (color >> 9) & 7u;
    unsigned level_offset = shade == 1u ? 0u : shade == 2u ? 7u : 0u;

    if (shade == 0u) {
        red *= 2u;
        green *= 2u;
        blue *= 2u;
    }
    red = levels[red + level_offset];
    green = levels[green + level_offset];
    blue = levels[blue + level_offset];
    return 0xFF000000u | (red << 16) | (green << 8) | blue;
}

static unsigned layer_rank(unsigned layer, int priority)
{
    if (layer == 3u)
        return priority ? 6u : 3u;
    if (layer == 1u)
        return priority ? 5u : 2u;
    if (layer == 2u)
        return priority ? 4u : 1u;
    return 0u;
}

static int sprite_pixel(uint16_t attributes, unsigned tile, unsigned x, unsigned y,
                        uint16_t *pattern)
{
    unsigned color = tile_pixel(tile, x, y);
    unsigned palette = (attributes >> 13) & 0x03u;

    if (!color)
        return 0;
    *pattern = (uint16_t)(((attributes & 0x8000u) ? MD_RENDER_PRIORITY : 0u) |
                          (palette << 4) | color);
    if ((md_vdp_register(12) & 0x08u) && palette == 3u) {
        if (color == 14u)
            *pattern |= MD_RENDER_HIGHLIGHT;
        else if (color == 15u)
            *pattern |= MD_RENDER_SHADOW;
    }
    return 1;
}

static void sprite_line(unsigned raster_y, uint16_t patterns[MD_RENDER_MAX_WIDTH],
                        uint8_t ranks[MD_RENDER_MAX_WIDTH])
{
    int width, height;
    unsigned h40 = md_vdp_register(12) & 0x01u;
    unsigned max_sprites = h40 ? 80u : 64u;
    unsigned max_line_sprites = h40 ? 20u : 16u;
    unsigned sprite_base = (unsigned)(md_vdp_register(5) & (h40 ? 0x7Eu : 0x7Fu)) << 9;
    unsigned index = 0;
    unsigned visited = 0;
    unsigned active = 0;

    md_vdp_dimensions(&width, &height);
    (void)height;
    while (visited < max_sprites) {
        uint16_t yword = read_vram_word((uint16_t)(sprite_base + index * 8u));
        uint16_t size_link = read_vram_word((uint16_t)(sprite_base + index * 8u + 2u));
        uint16_t attributes = read_vram_word((uint16_t)(sprite_base + index * 8u + 4u));
        uint16_t xword = read_vram_word((uint16_t)(sprite_base + index * 8u + 6u));
        unsigned h_cells = ((size_link >> 8) & 0x03u) + 1u;
        unsigned v_cells = ((size_link >> 10) & 0x03u) + 1u;
        unsigned cell_height = tile_height();
        unsigned sprite_width = h_cells * 8u;
        unsigned sprite_height = v_cells * cell_height;
        int y = (int)(yword & 0x03FFu) - 128;
        int x = (int)(xword & 0x01FFu) - 128;
        int row = (int)raster_y - y;
        unsigned next = size_link & 0x007Fu;

        if (row >= 0 && (unsigned)row < sprite_height) {
            active++;
            if (active > max_line_sprites)
                break;
            if ((xword & 0x01FFu) != 0u) {
                unsigned source_y = (attributes & 0x1000u) ?
                                    sprite_height - 1u - (unsigned)row : (unsigned)row;
                unsigned source_cell_y = source_y / cell_height;
                unsigned pixel_y = source_y % cell_height;
                unsigned px;

                for (px = 0; px < sprite_width; px++) {
                    int screen_x = x + (int)px;
                    unsigned source_x = (attributes & 0x0800u) ?
                                        sprite_width - 1u - px : px;
                    unsigned source_cell_x = source_x >> 3;
                    unsigned pixel_x = source_x & 7u;
                    unsigned tile = (attributes & 0x07FFu) +
                                    source_cell_x * v_cells + source_cell_y;
                    uint16_t pattern;
                    unsigned rank;

                    if (screen_x < 0 || screen_x >= width ||
                        !sprite_pixel(attributes, tile, pixel_x, pixel_y, &pattern))
                        continue;
                    rank = layer_rank(3u, (pattern & MD_RENDER_PRIORITY) != 0);
                    if (rank > ranks[screen_x]) {
                        patterns[screen_x] = pattern;
                        ranks[screen_x] = (uint8_t)rank;
                    }
                }
            }
        }
        visited++;
        if (!next)
            break;
        index = next;
    }
}

static void render_range(unsigned line, unsigned begin, unsigned end)
{
    int width, height;
    unsigned field = md_vdp_field();
    unsigned double_height = (md_vdp_register(12) & 0x06u) == 0x06u;
    unsigned raster_y = double_height ? line * 2u + field : line;
    unsigned x;
    uint16_t sprite_patterns[MD_RENDER_MAX_WIDTH] = { 0 };
    uint8_t ranks[MD_RENDER_MAX_WIDTH] = { 0 };
    unsigned colors[MD_RENDER_MAX_WIDTH];
    uint8_t intensities[MD_RENDER_MAX_WIDTH] = { 0 };
    unsigned shadow_mode = (md_vdp_register(12) & 0x08u) != 0;

    md_vdp_dimensions(&width, &height);
    if (line >= (unsigned)(double_height ? height / 2 : height) ||
        width > MD_RENDER_MAX_WIDTH)
        return;

    for (x = begin; x < end; x++) {
        uint16_t plane_a = in_window(x, raster_y) ? window_pixel(x, raster_y) :
                                                   plane_pixel(0, x, raster_y);
        uint16_t plane_b = plane_pixel(1, x, raster_y);
        unsigned rank;

        colors[x] = md_vdp_register(7) & 0x3Fu;
        if (plane_b) {
            intensities[x] = (plane_b & MD_RENDER_PRIORITY) ? 1u : 0u;
            rank = layer_rank(2u, (plane_b & MD_RENDER_PRIORITY) != 0);
            if (rank > ranks[x]) {
                colors[x] = plane_b & MD_RENDER_COLOR;
                ranks[x] = (uint8_t)rank;
            }
        }
        if (plane_a) {
            intensities[x] |= (plane_a & MD_RENDER_PRIORITY) ? 1u : 0u;
            rank = layer_rank(1u, (plane_a & MD_RENDER_PRIORITY) != 0);
            if (rank > ranks[x]) {
                colors[x] = plane_a & MD_RENDER_COLOR;
                ranks[x] = (uint8_t)rank;
            }
        }
    }

    sprite_line(raster_y, sprite_patterns, ranks);
    for (x = begin; x < end; x++) {
        uint16_t sprite = sprite_patterns[x];
        if (sprite) {
            if (sprite & MD_RENDER_SHADOW)
                intensities[x] = 0;
            else if (sprite & MD_RENDER_HIGHLIGHT)
                intensities[x]++;
            else {
                colors[x] = sprite & MD_RENDER_COLOR;
                if (sprite & MD_RENDER_PRIORITY)
                    intensities[x] |= 1u;
            }
        }
        unsigned shade = !shadow_mode ? 0u :
                         intensities[x] == 2u ? 2u :
                         intensities[x] ? 0u : 1u;
        picture[(size_t)(double_height ? raster_y : line) * (size_t)width + x] =
            cram_color(colors[x], shade);
    }
}

void md_vdp_render_reset(void)
{
    size_t i;
    for (i = 0; i < (size_t)MD_RENDER_MAX_WIDTH * MD_RENDER_MAX_HEIGHT; i++)
        picture[i] = 0xFF000000u;
    rendered_line = MD_RENDER_NO_LINE;
    rendered_x = 0;
}

void md_vdp_render_line(unsigned line)
{
    int width, height;
    md_vdp_dimensions(&width, &height);
    if (width > MD_RENDER_MAX_WIDTH)
        return;
    if (rendered_line != line) {
        rendered_line = line;
        rendered_x = 0;
    }
    render_range(line, rendered_x, (unsigned)width);
    rendered_line = MD_RENDER_NO_LINE;
    rendered_x = 0;
}

void md_vdp_render_until(uint64_t clock)
{
    unsigned line = md_vdp_current_line();
    uint64_t hpos = clock % MD_MASTER_CLOCKS_PER_LINE;
    unsigned active_clocks, end;
    int picture_width, picture_height;
    unsigned active_height;

    md_vdp_dimensions(&picture_width, &picture_height);
    active_height = (unsigned)picture_height;
    if ((md_vdp_register(12) & 0x06u) == 0x06u)
        active_height /= 2u;
    if (line >= active_height ||
        picture_width <= 0 || picture_width > MD_RENDER_MAX_WIDTH)
        return;
    if (rendered_line != line) {
        rendered_line = line;
        rendered_x = 0;
    }
    active_clocks = (md_vdp_register(12) & 0x01u) ? 3200u : 2560u;
    end = hpos >= active_clocks ? (unsigned)picture_width :
          (unsigned)(hpos * (unsigned)picture_width / active_clocks);
    if (end > (unsigned)picture_width)
        end = (unsigned)picture_width;
    if (end > rendered_x) {
        render_range(line, rendered_x, end);
        rendered_x = end;
    }
}

const uint32_t *md_vdp_picture(int *width, int *height)
{
    md_vdp_dimensions(width, height);
    return picture;
}
