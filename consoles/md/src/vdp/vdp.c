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

#define MD_VDP_FIFO_WORDS 4u
#define MD_VDP_NEVER UINT64_MAX

typedef struct md_vdp_fifo_entry {
    uint8_t code;
    uint8_t width;
    uint8_t lane;
    uint16_t addr;
    uint16_t value;
} md_vdp_fifo_entry;

static uint8_t registers[24];
static uint8_t vram[0x10000];
static uint16_t cram[64];
static uint16_t vsram[40];
static md_vdp_fifo_entry fifo[MD_VDP_FIFO_WORDS];
static unsigned fifo_head;
static unsigned fifo_count;
static uint64_t next_fifo_clock;
static unsigned lines_per_frame;
static unsigned active_lines;
static unsigned line;
static uint64_t next_line_clock;
static uint64_t frame_count;
static uint64_t vblank_count;
static uint64_t hint_count;
static uint8_t hint_counter;
static uint16_t address;
static uint16_t first_control_word;
static uint16_t read_buffer;
static uint8_t code;
static int control_pending;
static int read_buffer_valid;
static int vint_flag;
static int vdp_pal;

md_vdp_trace_fn md_vdp_trace_hook;

static unsigned slots_per_line(void)
{
    return registers[12] & 0x01 ? 20u : 16u;
}

static uint64_t fifo_slot_after(uint64_t clock)
{
    uint64_t line_start = clock - clock % MD_MASTER_CLOCKS_PER_LINE;
    uint64_t hpos = clock % MD_MASTER_CLOCKS_PER_LINE;
    unsigned slots = slots_per_line();
    unsigned slot = (unsigned)(hpos * slots / MD_MASTER_CLOCKS_PER_LINE) + 1u;
    if (slot >= slots)
        return line_start + MD_MASTER_CLOCKS_PER_LINE;
    return line_start + ((uint64_t)slot * MD_MASTER_CLOCKS_PER_LINE + slots - 1u) / slots;
}

static uint16_t vram_read_word(uint16_t addr)
{
    uint16_t base = (uint16_t)(addr & 0xFFFEu);
    return (uint16_t)((uint16_t)vram[base] << 8 | vram[(uint16_t)(base + 1u)]);
}

static void vdp_memory_write(uint8_t write_code, uint8_t width, uint8_t lane,
                             uint16_t addr, uint16_t value)
{
    if (write_code == 1) {
        uint16_t base = (uint16_t)(addr & 0xFFFEu);
        if (width == 1) {
            unsigned byte_lane = lane ^ (addr & 1u);
            vram[(uint16_t)(base + byte_lane)] = (uint8_t)value;
        } else if (addr & 1u) {
            vram[base] = (uint8_t)value;
            vram[(uint16_t)(base + 1u)] = (uint8_t)(value >> 8);
        } else {
            vram[base] = (uint8_t)(value >> 8);
            vram[(uint16_t)(base + 1u)] = (uint8_t)value;
        }
    } else if (write_code == 3) {
        unsigned index = (addr >> 1) & 0x3Fu;
        if (width == 1) {
            if (lane)
                cram[index] = (uint16_t)((cram[index] & 0xFF00u) | (uint8_t)value);
            else
                cram[index] = (uint16_t)((cram[index] & 0x00FFu) | ((uint16_t)(uint8_t)value << 8));
        } else {
            cram[index] = value;
        }
    } else if (write_code == 5) {
        unsigned index = (addr >> 1) % 40u;
        if (width == 1) {
            if (lane)
                vsram[index] = (uint16_t)((vsram[index] & 0xFF00u) | (uint8_t)value);
            else
                vsram[index] = (uint16_t)((vsram[index] & 0x00FFu) | ((uint16_t)(uint8_t)value << 8));
        } else {
            vsram[index] = value;
        }
    } else {
        nk_fatal("VDP write with unsupported access code %u", write_code);
    }
}

static uint16_t vdp_memory_read(uint8_t read_code, uint16_t addr)
{
    if (read_code == 0)
        return vram_read_word(addr);
    if (read_code == 8)
        return cram[(addr >> 1) & 0x3Fu];
    if (read_code == 4)
        return vsram[(addr >> 1) % 40u];
    nk_fatal("VDP read with unsupported access code %u", read_code);
}

static void fifo_drain_one(uint64_t clock)
{
    md_vdp_fifo_entry entry;
    if (!fifo_count)
        return;
    entry = fifo[fifo_head];
    fifo_head = (fifo_head + 1u) % MD_VDP_FIFO_WORDS;
    fifo_count--;
    vdp_memory_write(entry.code, entry.width, entry.lane, entry.addr, entry.value);
    next_fifo_clock = fifo_count ? fifo_slot_after(clock) : MD_VDP_NEVER;
}

static void fifo_drain_all(void)
{
    while (fifo_count)
        fifo_drain_one(md_clock);
}

static uint16_t vdp_status(void)
{
    uint16_t value = 0;
    uint64_t hpos = md_clock % MD_MASTER_CLOCKS_PER_LINE;
    unsigned active_hblank = (registers[12] & 0x01) ? 3200u : 2560u;
    if (!fifo_count)
        value |= 0x0200;
    if (fifo_count == MD_VDP_FIFO_WORDS)
        value |= 0x0100;
    if (vint_flag)
        value |= 0x0080;
    if (line >= active_lines)
        value |= 0x0008;
    if (hpos >= active_hblank)
        value |= 0x0004;
    if (vdp_pal)
        value |= 0x0001;
    return value;
}

static void control_write(uint16_t value)
{
    if ((value & 0xC000u) == 0x8000u) {
        control_pending = 0;
        md_vdp_set_register((value >> 8) & 0x1Fu, (uint8_t)value);
        return;
    }
    if (!control_pending) {
        first_control_word = value;
        control_pending = 1;
        return;
    }

    address = (uint16_t)((first_control_word & 0x3FFFu) | ((value & 0x0003u) << 14));
    code = (uint8_t)(((first_control_word >> 14) & 0x03u) | ((value >> 2) & 0x3Cu));
    control_pending = 0;
    read_buffer_valid = 0;
    if ((code & 0x1Fu) == 0) {
        read_buffer = vdp_memory_read(0, address);
        address = (uint16_t)(address + registers[15]);
        read_buffer_valid = 1;
    }
}

void md_vdp_reset(const nmd_cart *cart)
{
    memset(registers, 0, sizeof registers);
    memset(vram, 0, sizeof vram);
    memset(cram, 0, sizeof cram);
    memset(vsram, 0, sizeof vsram);
    memset(fifo, 0, sizeof fifo);
    lines_per_frame = cart && cart->region == NMD_PAL ? 312u : 262u;
    vdp_pal = cart && cart->region == NMD_PAL;
    active_lines = 224;
    line = 0;
    next_line_clock = MD_MASTER_CLOCKS_PER_LINE;
    frame_count = 0;
    vblank_count = 0;
    hint_count = 0;
    hint_counter = 0;
    fifo_head = 0;
    fifo_count = 0;
    next_fifo_clock = MD_VDP_NEVER;
    address = 0;
    first_control_word = 0;
    read_buffer = 0;
    code = 0;
    control_pending = 0;
    read_buffer_valid = 0;
    vint_flag = 0;
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
    if (fifo_count && line < active_lines && next_fifo_clock < next_line_clock)
        return next_fifo_clock;
    return next_line_clock;
}

void md_vdp_advance(uint64_t to_clock)
{
    for (;;) {
        uint64_t next = md_vdp_next_event();
        if (next > to_clock)
            break;
        if (fifo_count && line < active_lines && next_fifo_clock == next) {
            fifo_drain_one(next);
            continue;
        }

        line++;
        next_line_clock += MD_MASTER_CLOCKS_PER_LINE;
        if (line >= lines_per_frame) {
            line = 0;
            frame_count++;
            hint_counter = 0;
        }

        if (line == active_lines) {
            vblank_count++;
            vint_flag = 1;
            if (registers[1] & 0x20)
                md_irq_request(6);
            fifo_drain_all();
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

static void trace_vdp_port(uint64_t clock, uint8_t operation, uint8_t width,
                           uint32_t port, uint32_t value)
{
    if (md_vdp_trace_hook)
        md_vdp_trace_hook(clock, operation, width, port, value);
}

static void schedule_vram_read(void)
{
    read_buffer = vdp_memory_read(0, address);
    address = (uint16_t)(address + registers[15]);
    read_buffer_valid = 1;
}

static uint16_t data_read(void)
{
    uint8_t read_code = code & 0x1Fu;
    uint16_t value;
    if (read_code == 0) {
        if (!read_buffer_valid)
            schedule_vram_read();
        value = read_buffer;
        schedule_vram_read();
    } else {
        value = vdp_memory_read(read_code, address);
        address = (uint16_t)(address + registers[15]);
    }
    return value;
}

static void enqueue_data_write(uint8_t width, uint8_t lane, uint16_t value,
                               uint64_t port_clock)
{
    md_vdp_fifo_entry *entry;
    unsigned tail;

    if (fifo_count == MD_VDP_FIFO_WORDS)
        nk_fatal("VDP FIFO enqueue attempted while full");
    if (!fifo_count)
        next_fifo_clock = fifo_slot_after(port_clock);
    tail = (fifo_head + fifo_count) % MD_VDP_FIFO_WORDS;
    entry = &fifo[tail];
    entry->code = code & 0x1Fu;
    entry->width = width;
    entry->lane = lane;
    entry->addr = address;
    entry->value = value;
    fifo_count++;
    address = (uint16_t)(address + registers[15]);
}

static void data_write(uint8_t width, uint8_t lane, uint16_t value, uint64_t clock)
{
    uint8_t write_code = code & 0x1Fu;
    uint64_t port_clock;

    if (write_code == 0) {
        /* A data write while VRAM is configured for reads is ignored. */
        md_advance_to(clock + 16u);
        control_pending = 0;
        trace_vdp_port(md_clock, MD_VDP_TRACE_WRITE, width, MD_VDP_DATA_PORT, value);
        md_advance_to(md_clock + 12u);
        return;
    }
    if (write_code != 1 && write_code != 3 && write_code != 5)
        nk_fatal("VDP data write with unsupported access code %u", write_code);

    md_advance_to(clock + 16u);
    control_pending = 0;
    if (line >= active_lines) {
        vdp_memory_write(write_code, width, lane, address, value);
        address = (uint16_t)(address + registers[15]);
        port_clock = md_clock;
    } else {
        while (fifo_count == MD_VDP_FIFO_WORDS) {
            if (next_fifo_clock == MD_VDP_NEVER || next_fifo_clock <= md_clock)
                next_fifo_clock = fifo_slot_after(md_clock);
            md_advance_to(next_fifo_clock);
        }
        port_clock = md_clock;
        enqueue_data_write(width, lane, value, port_clock);
    }
    trace_vdp_port(port_clock, MD_VDP_TRACE_WRITE, width, MD_VDP_DATA_PORT, value);
    md_advance_to(port_clock + 12u);
}

static uint16_t read_status(uint64_t clock)
{
    uint16_t value;
    md_advance_to(clock + 24u);
    control_pending = 0;
    value = vdp_status();
    vint_flag = 0;
    md_irq_clear(6);
    trace_vdp_port(md_clock, MD_VDP_TRACE_READ, 2, MD_VDP_CONTROL_PORT, value);
    return value;
}

uint16_t md_vdp_read16(uint32_t port, uint64_t clock)
{
    uint16_t value;
    if (port == MD_VDP_DATA_PORT) {
        md_advance_to(clock + 28u);
        control_pending = 0;
        value = data_read();
        trace_vdp_port(md_clock, MD_VDP_TRACE_READ, 2, MD_VDP_DATA_PORT, value);
        return value;
    }
    if (port == MD_VDP_CONTROL_PORT)
        return read_status(clock);
    if (port == MD_VDP_HV_PORT) {
        value = md_vdp_hv_counter();
        md_advance_to(clock + 28u);
        trace_vdp_port(md_clock, MD_VDP_TRACE_READ, 2, MD_VDP_HV_PORT, value);
        return value;
    }
    nk_fatal("prohibited VDP word read at port $%X", (unsigned)port);
}

void md_vdp_write16(uint32_t port, uint16_t value, uint64_t clock)
{
    if (port == MD_VDP_CONTROL_PORT) {
        control_write(value);
        trace_vdp_port(clock + 36u, MD_VDP_TRACE_WRITE, 2, MD_VDP_CONTROL_PORT, value);
        return;
    }
    if (port == MD_VDP_DATA_PORT) {
        data_write(2, 0, value, clock);
        return;
    }
    nk_fatal("prohibited VDP word write at port $%X", (unsigned)port);
}

void md_vdp_write8(uint32_t port, uint8_t value, uint64_t clock)
{
    if (port > 3u)
        nk_fatal("prohibited VDP byte write at port $%X", (unsigned)port);
    data_write(1, 0, value, clock);
}
