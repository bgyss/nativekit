/* Clock, VDP line events and 68000 interrupt dispatch. */
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nmd/nmd.h>

#include "md_internal.h"

#define VECTOR_LEVEL2 0x68u
#define VECTOR_LEVEL4 0x70u
#define VECTOR_LEVEL6 0x78u
#define MAX_HANDLERS 512u

static uint8_t rom[0x200];
static unsigned order[MAX_HANDLERS];
static uint64_t handler_clock[MAX_HANDLERS];
static uint16_t handler_sr[MAX_HANDLERS];
static uint32_t handler_pc[MAX_HANDLERS];
static uint32_t handler_sp[MAX_HANDLERS];
static unsigned handler_line[MAX_HANDLERS];
static uint64_t handler_service_clock[MAX_HANDLERS];
static unsigned handler_count;

static void put_vector(uint32_t address, uint32_t target)
{
    rom[address + 0] = (uint8_t)(target >> 24);
    rom[address + 1] = (uint8_t)(target >> 16);
    rom[address + 2] = (uint8_t)(target >> 8);
    rom[address + 3] = (uint8_t)target;
}

static void record_level(unsigned level)
{
    assert(handler_count < MAX_HANDLERS);
    order[handler_count] = level;
    handler_clock[handler_count] = md_clock;
    handler_sr[handler_count] = nmd_cpu.sr;
    handler_pc[handler_count] = nmd_cpu.pc;
    handler_sp[handler_count] = nmd_cpu.a[7];
    handler_line[handler_count] = md_vdp_current_line();
    handler_service_clock[handler_count] = md_irq_last_service_clock;
    handler_count++;
}

static void irq2(void) { record_level(2); }
static void irq4(void) { record_level(4); }
static void irq6(void) { record_level(6); }

static const nmd_routine routines[] = {
    { 0x100, irq2, "irq2" },
    { 0x120, irq4, "irq4" },
    { 0x140, irq6, "irq6" },
};

static nmd_cart cart = {
    .title = "interrupt check",
    .region = NMD_NTSC,
    .rom = rom,
    .rom_size = sizeof rom,
    .routines = routines,
    .routine_count = sizeof routines / sizeof routines[0],
};

static void power_region(nmd_region region)
{
    memset(rom, 0, sizeof rom);
    put_vector(VECTOR_LEVEL2, 0x100);
    put_vector(VECTOR_LEVEL4, 0x120);
    put_vector(VECTOR_LEVEL6, 0x140);
    cart.region = region;
    handler_count = 0;
    memset(order, 0, sizeof order);
    memset(handler_clock, 0, sizeof handler_clock);
    memset(handler_sr, 0, sizeof handler_sr);
    memset(handler_pc, 0, sizeof handler_pc);
    memset(handler_sp, 0, sizeof handler_sp);
    nmd_power(&cart);
    assert(nmd_cpu.sr == 0x2700);
    nmd_cpu.a[7] = 0x00FF0000;
    nmd_cpu.sr = 0x2000;
}

static int handler_ran(void *ctx)
{
    unsigned want = *(unsigned *)ctx;
    return handler_count >= want;
}

static int missing_handler_mode(void)
{
    nmd_cart missing = cart;
    missing.routines = NULL;
    missing.routine_count = 0;
    nmd_power(&missing);
    nmd_cpu.a[7] = 0x00FF0000;
    nmd_cpu.sr = 0x2000;
    md_irq_request_at(6, 100);
    md_advance_to(100);
    md_irq_dispatch_pending();
    return 2;
}

static int hint_trace_mode(void)
{
    unsigned line, i;
    power_region(NMD_NTSC);
    md_vdp_set_register(1, 0x14);
    md_vdp_set_register(0, 0x10);
    md_vdp_set_register(10, 1);
    for (line = 1; line < 224; line++) {
        md_advance_to((uint64_t)line * MD_MASTER_CLOCKS_PER_LINE);
        md_irq_dispatch_pending();
    }
    for (i = 0; i < handler_count; i++) {
        if (order[i] == 4)
            printf("%u\t%" PRIu64 "\n", handler_line[i], handler_service_clock[i]);
    }
    return 0;
}

static void test_ntsc_pal_frame_lines(void)
{
    power_region(NMD_NTSC);
    assert(md_vdp_current_line() == 0);
    assert(md_vdp_frame_clocks() == 262ull * MD_MASTER_CLOCKS_PER_LINE);
    md_advance_to(223ull * MD_MASTER_CLOCKS_PER_LINE + MD_MASTER_CLOCKS_PER_LINE - 1);
    assert(md_vdp_vblank_count() == 0);
    md_advance_to(224ull * MD_MASTER_CLOCKS_PER_LINE);
    assert(md_vdp_vblank_count() == 1);
    assert(md_vdp_current_line() == 224);
    md_advance_to(262ull * MD_MASTER_CLOCKS_PER_LINE);
    assert(md_vdp_frame_count() == 1);
    assert(md_vdp_current_line() == 0);

    power_region(NMD_PAL);
    assert(md_vdp_frame_clocks() == 312ull * MD_MASTER_CLOCKS_PER_LINE);
    md_vdp_set_register(1, 0x08);
    md_advance_to(239ull * MD_MASTER_CLOCKS_PER_LINE + MD_MASTER_CLOCKS_PER_LINE - 1);
    assert(md_vdp_vblank_count() == 0);
    md_advance_to(240ull * MD_MASTER_CLOCKS_PER_LINE);
    assert(md_vdp_vblank_count() == 1);
    md_advance_to(312ull * MD_MASTER_CLOCKS_PER_LINE);
    assert(md_vdp_frame_count() == 1);
    assert(md_vdp_current_line() == 0);
}

static void test_wait_and_frame_boundaries(void)
{
    uint64_t before;
    power_region(NMD_NTSC);
    before = md_clock;
    nmd_wait_vblank();
    assert(md_clock - before == 224ull * MD_MASTER_CLOCKS_PER_LINE);

    power_region(NMD_NTSC);
    nmd_run_frame();
    assert(md_clock == 224ull * MD_MASTER_CLOCKS_PER_LINE);
    before = md_clock;
    nmd_run_frame();
    assert(md_clock - before == 262ull * MD_MASTER_CLOCKS_PER_LINE);

    power_region(NMD_PAL);
    md_vdp_set_register(1, 0x08);
    nmd_run_frame();
    assert(md_clock == 240ull * MD_MASTER_CLOCKS_PER_LINE);
    before = md_clock;
    nmd_run_frame();
    assert(md_clock - before == 312ull * MD_MASTER_CLOCKS_PER_LINE);
}

static void test_hint_reload_and_hv_counter(void)
{
    unsigned line;
    uint16_t hv32, hv40;
    power_region(NMD_NTSC);
    md_vdp_set_register(0, 0x10);
    md_vdp_set_register(10, 1);
    for (line = 1; line <= 7; line++) {
        md_advance_to((uint64_t)line * MD_MASTER_CLOCKS_PER_LINE);
        md_irq_dispatch_pending();
    }
    assert(md_vdp_hint_count() == 4);
    assert(handler_count == 4);
    assert(order[0] == 4 && order[1] == 4 && order[2] == 4 && order[3] == 4);
    assert(handler_line[0] == 1 && handler_line[1] == 3 &&
           handler_line[2] == 5 && handler_line[3] == 7);
    assert(handler_clock[0] < handler_clock[1] && handler_clock[1] < handler_clock[2] &&
           handler_clock[2] < handler_clock[3]);

    power_region(NMD_NTSC);
    md_advance_to(10ull * MD_MASTER_CLOCKS_PER_LINE + MD_MASTER_CLOCKS_PER_LINE / 2);
    hv32 = md_vdp_hv_counter();
    assert((hv32 & 0xFFu) == 10);
    assert((hv32 >> 8) > 0 && (hv32 >> 8) <= 0x7F);
    assert(rd16(0xC00008) == hv32);
    hv32 = md_vdp_hv_counter();
    assert(rd8(0xC00008) == (uint8_t)(hv32 >> 8));
    hv32 = md_vdp_hv_counter();
    assert(rd8(0xC00009) == (uint8_t)hv32);
    md_vdp_set_register(12, 1);
    hv40 = md_vdp_hv_counter();
    assert((hv40 & 0xFFu) == 10);
    assert((hv40 >> 8) <= 0x9F);
}

static void test_priority_mask_and_pulse(void)
{
    power_region(NMD_NTSC);
    md_irq_request_at(2, 100);
    md_irq_request_at(4, 100);
    md_irq_request_at(6, 100);
    md_advance_to(100);
    md_irq_dispatch_pending();
    assert(handler_count == 3);
    assert(order[0] == 6 && order[1] == 4 && order[2] == 2);

    power_region(NMD_NTSC);
    nmd_cpu.sr = 0x2200;
    md_ext_irq_at(2, 100);
    md_ext_irq_at(0, 120);
    md_advance_to(100);
    md_irq_dispatch_pending();
    assert(handler_count == 0);
    nmd_cpu.sr = 0x2100;
    md_irq_dispatch_pending();
    assert(handler_count == 1 && order[0] == 2);
    assert(md_irq_last_service_clock == 100);
    assert(nmd_cpu.sr == 0x2100);
}

static void test_interrupt_boundaries_and_stop(void)
{
    unsigned want;
    uint64_t event_clock = MD_MASTER_CLOCKS_PER_LINE;
    power_region(NMD_NTSC);
    md_vdp_set_register(0, 0x10);
    md_vdp_set_register(10, 0);
    md_advance_to(event_clock - 14);
    (void)rd16(0xFF0000);
    assert(handler_count == 1 && order[0] == 4);
    assert(md_irq_last_service_clock == event_clock + 14);

    power_region(NMD_NTSC);
    md_vdp_set_register(1, 0x20);
    event_clock = 224ull * MD_MASTER_CLOCKS_PER_LINE;
    md_advance_to(event_clock - 14);
    (void)rd16(0xFF0000);
    assert(handler_count == 1 && order[0] == 6);
    assert(md_irq_last_service_clock == event_clock + 14);

    power_region(NMD_NTSC);
    md_vdp_set_register(0, 0x10);
    md_vdp_set_register(10, 0);
    nmd_stop();
    assert(handler_count == 1 && order[0] == 4);
    assert(md_irq_last_service_clock == MD_MASTER_CLOCKS_PER_LINE);

    power_region(NMD_NTSC);
    md_vdp_set_register(1, 0x20);
    nmd_stop();
    assert(handler_count == 1 && order[0] == 6);
    assert(md_irq_last_service_clock == 224ull * MD_MASTER_CLOCKS_PER_LINE);

    power_region(NMD_NTSC);
    md_ext_irq_at(2, 100);
    md_ext_irq_at(0, 120);
    nmd_stop();
    assert(handler_count == 1 && order[0] == 2);
    assert(md_irq_last_service_clock == 100);

    power_region(NMD_NTSC);
    md_ext_irq_at(2, 200);
    md_ext_irq_at(0, 220);
    want = 1;
    assert(nmd_wait_until(handler_ran, &want) > 0);
    assert(handler_count == 1 && order[0] == 2);
    assert(md_irq_last_service_clock == 200);
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "missing"))
        return missing_handler_mode();
    if (argc == 2 && !strcmp(argv[1], "hint-trace"))
        return hint_trace_mode();
    test_ntsc_pal_frame_lines();
    test_wait_and_frame_boundaries();
    test_hint_reload_and_hv_counter();
    test_priority_mask_and_pulse();
    test_interrupt_boundaries_and_stop();
    puts("interrupt_check: ok");
    return 0;
}
