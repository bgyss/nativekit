/* VDP DMA transfer scheduler. */
#include <stdint.h>

#include "dma.h"
#include "md_internal.h"
#include "nk/fatal.h"

#define MD_DMA_NEVER UINT64_MAX

static int dma_busy;
static int dma_bus_owned;
static int dma_waiting_seed;
static uint64_t next_dma_clock;
static uint32_t source_address;
static uint32_t remaining;
static uint16_t destination_address;
static uint16_t fill_data;
static uint8_t destination_code;
static uint8_t destination_increment;
static uint8_t dma_mode;

enum {
    MD_DMA_MEMORY = 0,
    MD_DMA_FILL = 1,
    MD_DMA_COPY = 2
};

static uint64_t dma_slot_interval(void)
{
    int h40 = (md_vdp_register(12) & 0x01u) != 0;
    int active = md_vdp_active_display();

    if (dma_mode == MD_DMA_MEMORY) {
        if (destination_code == 1) {
            if (active)
                return h40 ? 380u : 428u;
            return h40 ? 33u : 41u;
        }
        if (active)
            return h40 ? 190u : 214u;
        return h40 ? 16u : 20u;
    }
    if (dma_mode == MD_DMA_FILL) {
        if (active)
            return h40 ? 201u : 228u;
        return h40 ? 16u : 20u;
    }
    if (active)
        return h40 ? 380u : 428u;
    return h40 ? 33u : 40u;
}

static uint64_t next_dma_slot(uint64_t clock)
{
    uint64_t interval = dma_slot_interval();
    return clock + interval - clock % interval;
}

void md_dma_reset(void)
{
    dma_busy = 0;
    dma_bus_owned = 0;
    dma_waiting_seed = 0;
    next_dma_clock = MD_DMA_NEVER;
    source_address = 0;
    remaining = 0;
    destination_address = 0;
    fill_data = 0;
    destination_code = 0;
    destination_increment = 0;
    dma_mode = MD_DMA_MEMORY;
}

int md_dma_busy(void)
{
    return dma_busy;
}

int md_dma_bus_owned(void)
{
    return dma_bus_owned;
}

int md_dma_waiting_seed(void)
{
    return dma_waiting_seed;
}

uint64_t md_dma_next_event(void)
{
    return next_dma_clock;
}

void md_dma_start(uint64_t clock)
{
    uint8_t reg23;
    uint8_t mode_bits;
    uint16_t length;

    if (!(md_vdp_register(1) & 0x10u) || !(md_vdp_code() & 0x20u))
        return;
    if (dma_busy)
        nk_fatal("VDP DMA started while a transfer is active");

    reg23 = md_vdp_register(23);
    mode_bits = reg23 & 0xC0u;
    if (!(mode_bits & 0x80u))
        dma_mode = MD_DMA_MEMORY;
    else if (mode_bits == 0x80u)
        dma_mode = MD_DMA_FILL;
    else
        dma_mode = MD_DMA_COPY;

    destination_code = md_vdp_code() & 0x07u;
    if (dma_mode == MD_DMA_FILL || dma_mode == MD_DMA_COPY) {
        if (destination_code != 1)
            nk_fatal("VDP DMA mode %u only supports VRAM", (unsigned)dma_mode);
    } else if (destination_code != 1 && destination_code != 3 && destination_code != 5) {
        nk_fatal("VDP DMA has unsupported destination code %u", destination_code);
    }

    length = (uint16_t)((uint16_t)md_vdp_register(20) << 8 | md_vdp_register(19));
    remaining = length ? length : 65536u;
    destination_address = md_vdp_address();
    destination_increment = md_vdp_register(15);
    if (dma_mode == MD_DMA_MEMORY) {
        source_address = ((uint32_t)(reg23 & 0x7Fu) << 17) |
                         ((uint32_t)md_vdp_register(22) << 9) |
                         ((uint32_t)md_vdp_register(21) << 1);
        dma_busy = 1;
        dma_bus_owned = 1;
        next_dma_clock = clock;
    } else if (dma_mode == MD_DMA_FILL) {
        dma_waiting_seed = 1;
        next_dma_clock = MD_DMA_NEVER;
    } else {
        source_address = ((uint32_t)md_vdp_register(22) << 8) |
                         md_vdp_register(21);
        dma_busy = 1;
        next_dma_clock = next_dma_slot(clock);
    }
}

void md_dma_seed(uint16_t value, uint64_t clock)
{
    if (!dma_waiting_seed)
        nk_fatal("VDP fill data written without a pending fill DMA");
    dma_waiting_seed = 0;
    fill_data = value;
    md_vdp_dma_write(destination_code, 2, destination_address, value);
    destination_address = (uint16_t)(destination_address + destination_increment);
    md_vdp_set_address(destination_address);
    dma_busy = 1;
    next_dma_clock = clock + 5u * dma_slot_interval();
}

void md_dma_advance(uint64_t to_clock)
{
    while (dma_busy && next_dma_clock <= to_clock) {
        uint16_t value;
        uint8_t width;
        uint32_t trace_address;
        if (dma_mode == MD_DMA_MEMORY) {
            value = nmd_dma_source_read16(source_address);
            width = 2;
            trace_address = source_address;
            md_vdp_dma_write(destination_code, width, destination_address, value);
            source_address = (source_address + 2u) & 0x00FFFFFFu;
        } else if (dma_mode == MD_DMA_FILL) {
            value = (uint8_t)(fill_data >> 8);
            width = 1;
            trace_address = 0x10000000u | destination_address;
            md_vdp_dma_write_byte(destination_address, (uint8_t)value);
        } else {
            value = md_vdp_dma_read_copy_byte((uint16_t)source_address);
            width = 1;
            trace_address = 0x20000000u | destination_address;
            md_vdp_dma_write_byte(destination_address, (uint8_t)value);
            source_address = (source_address + 1u) & 0xFFFFu;
        }
        md_vdp_trace_dma(next_dma_clock, width, trace_address, value);
        destination_address = (uint16_t)(destination_address + destination_increment);
        md_vdp_set_address(destination_address);
        remaining--;
        if (!remaining) {
            dma_busy = 0;
            dma_bus_owned = 0;
            next_dma_clock = MD_DMA_NEVER;
        } else {
            next_dma_clock += dma_slot_interval();
        }
    }
}
