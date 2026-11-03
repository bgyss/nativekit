/*
 * vdp.c: the VDP's line scheduler and beam counters.
 *
 * Port storage and pixel generation are filled in by the following Stage 2
 * tasks. Line boundaries live here so bus waits and device events share one
 * master-clock timeline from the start.
 */
#include <string.h>

#include "md_internal.h"
#include "nk/fatal.h"

static uint8_t registers[24];
static unsigned lines_per_frame;
static unsigned active_lines;
static unsigned line;
static uint64_t next_line_clock;
static uint64_t frame_count;
static uint64_t vblank_count;
static uint64_t hint_count;
static uint8_t hint_counter;

void md_vdp_reset(const nmd_cart *cart)
{
    memset(registers, 0, sizeof registers);
    lines_per_frame = cart && cart->region == NMD_PAL ? 312u : 262u;
    active_lines = 224;
    line = 0;
    next_line_clock = MD_MASTER_CLOCKS_PER_LINE;
    frame_count = 0;
    vblank_count = 0;
    hint_count = 0;
    hint_counter = 0;
}

void md_vdp_set_register(unsigned reg, uint8_t value)
{
    if (reg >= sizeof registers)
        nk_fatal("VDP register number %u is outside 0-23", reg);

    registers[reg] = value;
    if (reg == 1)
        active_lines = (lines_per_frame == 312 && (value & 0x08)) ? 240u : 224u;
}

uint64_t md_vdp_next_event(void)
{
    return next_line_clock;
}

void md_vdp_advance(uint64_t to_clock)
{
    while (next_line_clock <= to_clock) {
        line++;
        next_line_clock += MD_MASTER_CLOCKS_PER_LINE;
        if (line >= lines_per_frame) {
            line = 0;
            frame_count++;
            hint_counter = 0;
        }

        if (line == active_lines) {
            vblank_count++;
            if (registers[1] & 0x20)
                md_irq_request(6);
        }

        if (line < active_lines && (registers[0] & 0x10)) {
            if (!hint_counter) {
                hint_count++;
                md_irq_request(4);
                hint_counter = registers[10];
            } else {
                hint_counter--;
            }
        }
    }
}

uint64_t md_vdp_frame_clocks(void)
{
    return (uint64_t)lines_per_frame * MD_MASTER_CLOCKS_PER_LINE;
}

uint64_t md_vdp_next_vblank_clock(void)
{
    uint64_t frame_clocks = md_vdp_frame_clocks();
    uint64_t frame_start = md_clock - md_clock % frame_clocks;
    uint64_t next = frame_start + (uint64_t)active_lines * MD_MASTER_CLOCKS_PER_LINE;
    if (next <= md_clock)
        next += frame_clocks;
    return next;
}

uint64_t md_vdp_frame_count(void)
{
    return frame_count;
}

uint64_t md_vdp_vblank_count(void)
{
    return vblank_count;
}

uint64_t md_vdp_hint_count(void)
{
    return hint_count;
}

unsigned md_vdp_current_line(void)
{
    return line;
}

uint16_t md_vdp_hv_counter(void)
{
    uint64_t hpos = md_clock % MD_MASTER_CLOCKS_PER_LINE;
    unsigned h_limit = registers[12] & 0x01 ? 0xA0u : 0x80u;
    unsigned h = (unsigned)(hpos * h_limit / MD_MASTER_CLOCKS_PER_LINE);
    if (h >= h_limit)
        h = h_limit - 1;
    return (uint16_t)((h << 8) | (line & 0xFFu));
}
