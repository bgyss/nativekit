/* The VDP data path reads back the VRAM word addressed by its control port. */
#include <assert.h>
#include <stdint.h>

#include <nmd/hw.h>
#include <nmd/nmd.h>

#include "md_internal.h"

static uint8_t rom[0x400];
static unsigned irq_count;
static uint64_t last_vdp_clock;
static uint8_t last_vdp_operation;
static uint8_t last_vdp_width;
static uint32_t last_vdp_port;
static uint32_t last_vdp_value;
static uint64_t last_data_write_clock;
static uint64_t largest_data_write_gap;
static unsigned data_write_count;

static void irq6(void)
{
    irq_count++;
}

static const nmd_routine routines[] = {
    { 0x100, irq6, "irq6" },
};

static void trace_vdp(uint64_t clock, uint8_t operation, uint8_t width,
                      uint32_t port, uint32_t value)
{
    last_vdp_clock = clock;
    last_vdp_operation = operation;
    last_vdp_width = width;
    last_vdp_port = port;
    last_vdp_value = value;
}

static void trace_bus(uint64_t clock, uint8_t operation, uint8_t width,
                      uint32_t addr, uint32_t value)
{
    (void)operation;
    (void)value;
    if (addr != NMD_VDP_DATA || width != 2)
        return;
    if (data_write_count && clock - last_data_write_clock > largest_data_write_gap)
        largest_data_write_gap = clock - last_data_write_clock;
    last_data_write_clock = clock;
    data_write_count++;
}

static void control(uint32_t command)
{
    wr32(NMD_VDP_CONTROL, command);
}

static int64_t clock_delta(uint64_t event_clock, uint64_t cpu_clock)
{
    if (event_clock >= cpu_clock)
        return (int64_t)(event_clock - cpu_clock);
    return -(int64_t)(cpu_clock - event_clock);
}

static void test_vram_read_prefetch_and_increment(nmd_cart *cart)
{
    nmd_power(cart);
    nmd_wait_vblank();
    wr16(NMD_VDP_CONTROL, 0x8F02); /* auto increment by one word */
    control(0x40800000);           /* VRAM write at byte address $0080 */
    wr16(NMD_VDP_DATA, 0xA55A);
    wr16(NMD_VDP_DATA, 0xBEEF);
    control(0x00800000);           /* VRAM read at byte address $0080 */
    assert(rd16(NMD_VDP_DATA) == 0xA55A);
    assert(rd16(NMD_VDP_DATA + 2) == 0xBEEF); /* $C00002 mirrors the data port */

    /* A data access cancels the pending first word of another command. */
    control(0x41000000);           /* VRAM write at byte address $0100 */
    wr16(NMD_VDP_DATA, 0x1111);
    wr16(NMD_VDP_CONTROL, 0x4200); /* begin a command for $0200 */
    wr16(NMD_VDP_DATA, 0x2222);    /* still writes the old stream at $0102 */
    control(0x01000000);           /* VRAM read at byte address $0100 */
    assert(rd16(NMD_VDP_DATA) == 0x1111);
    assert(rd16(NMD_VDP_DATA) == 0x2222);
}

static void test_cram_vsram(nmd_cart *cart)
{
    nmd_power(cart);
    nmd_wait_vblank();
    control(0xC0000000);           /* CRAM write at word address zero */
    wr16(NMD_VDP_DATA, 0x0123);
    control(0x00000020);           /* CRAM read at word address zero */
    assert(rd16(NMD_VDP_DATA) == 0x0022); /* Unused CRAM bits read as zero. */

    control(0x40000010);           /* VSRAM write at word address zero */
    wr16(NMD_VDP_DATA, 0x0123);
    control(0x00000010);           /* VSRAM read at word address zero */
    assert(rd16(NMD_VDP_DATA) == 0x0123);
}

static void test_vram_byte_lanes(nmd_cart *cart)
{
    nmd_power(cart);
    nmd_wait_vblank();
    wr16(NMD_VDP_CONTROL, 0x8F01);
    control(0x42000000);           /* VRAM write at byte address $0200 */
    wr8(NMD_VDP_DATA, 0x12);
    wr8(NMD_VDP_DATA + 1, 0x34);
    control(0x02000000);           /* VRAM read at byte address $0200 */
    assert(rd16(NMD_VDP_DATA) == 0x1234);
}

static void test_status_and_hblank(nmd_cart *cart)
{
    uint16_t status;
    irq_count = 0;
    nmd_power(cart);
    md_vdp_set_register(1, 0x20);  /* V interrupt enable */
    nmd_wait_vblank();
    status = rd16(NMD_VDP_CONTROL);
    assert((status & 0x0200) != 0); /* FIFO empty */
    assert((status & 0x0088) == 0x0088); /* V interrupt flag and VBlank */
    status = rd16(NMD_VDP_CONTROL);
    assert((status & 0x0080) == 0); /* status read acknowledges V interrupt flag */
    nmd_cpu.sr = 0x2000;
    md_irq_dispatch_pending();
    assert(irq_count == 0);

    nmd_power(cart);
    md_advance_to(3000);
    status = rd16(NMD_VDP_CONTROL);
    assert((status & 0x0004) != 0); /* H32 horizontal blank */
    nmd_power(cart);
    md_vdp_set_register(12, 0x81); /* H40 display extends horizontal active scan */
    md_advance_to(3000);
    status = rd16(NMD_VDP_CONTROL);
    assert((status & 0x0004) == 0);
}

static void test_port_event_timing(nmd_cart *cart)
{
    nmd_power(cart);
    nmd_wait_vblank();
    md_vdp_trace_hook = trace_vdp;

    wr16(NMD_VDP_CONTROL, 0x8F02);
    assert(last_vdp_operation == MD_VDP_TRACE_WRITE && last_vdp_width == 2);
    assert(last_vdp_port == MD_VDP_CONTROL_PORT && last_vdp_value == 0x8F02);
    assert(clock_delta(last_vdp_clock, md_clock) == 8);

    control(0x40000000);
    wr16(NMD_VDP_DATA, 0x55AA);
    assert(last_vdp_operation == MD_VDP_TRACE_WRITE && last_vdp_port == MD_VDP_DATA_PORT);
    assert(clock_delta(last_vdp_clock, md_clock) == -12);

    (void)rd16(NMD_VDP_CONTROL);
    assert(last_vdp_operation == MD_VDP_TRACE_READ && last_vdp_port == MD_VDP_CONTROL_PORT);
    assert(clock_delta(last_vdp_clock, md_clock) == -4);

    control(0x00000000);
    (void)rd16(NMD_VDP_DATA);
    assert(last_vdp_operation == MD_VDP_TRACE_READ && last_vdp_port == MD_VDP_DATA_PORT);
    assert(last_vdp_clock == md_clock);
}

static void test_fifo_stall(nmd_cart *cart)
{
    unsigned i;
    nmd_power(cart);
    md_vdp_trace_hook = NULL;
    md_bus_trace_hook = trace_bus;
    data_write_count = 0;
    largest_data_write_gap = 0;
    last_data_write_clock = 0;
    wr16(NMD_VDP_CONTROL, 0x8F02);
    control(0x40000000); /* VRAM write at address zero */
    for (i = 0; i < 16; i++)
        wr16(NMD_VDP_DATA, 0x1234);
    assert(data_write_count == 16);
    assert(largest_data_write_gap > 28);

    nmd_wait_vblank();
    assert((rd16(NMD_VDP_CONTROL) & 0x0200) != 0);
    md_bus_trace_hook = NULL;
}

int main(void)
{
    nmd_cart cart = {
        .title = "VDP check",
        .region = NMD_NTSC,
        .rom = rom,
        .rom_size = sizeof rom,
        .routines = routines,
        .routine_count = sizeof routines / sizeof routines[0],
    };

    rom[0x78] = 0;
    rom[0x79] = 0;
    rom[0x7A] = 1;
    rom[0x7B] = 0;

    test_vram_read_prefetch_and_increment(&cart);
    test_vram_byte_lanes(&cart);
    test_cram_vsram(&cart);
    test_status_and_hblank(&cart);
    test_port_event_timing(&cart);
    test_fifo_stall(&cart);
    return 0;
}
