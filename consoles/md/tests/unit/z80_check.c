/* The Z80 sound routine starts only after BUSREQ and RESET are released. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <nmd/nmd.h>
#include <nmd/z80.h>

#include "md_internal.h"

static const uint8_t rom[0x400] = { 0 };
static uint8_t dma_rom[0x400];
static unsigned entry_count;
static unsigned restart_count;
static unsigned power_entry_count;
static int leave_first_wait;
static uint8_t map_ram_before;
static uint8_t map_ram_after;
static uint8_t map_ym_status;
static uint8_t bank_window_value;
static uint8_t boundary_value;
static unsigned boundary_started;
static unsigned boundary_done;
static int boundary_start_access;
static uint8_t dma_bank_value;
static unsigned dma_busy_when_bank_read;
static unsigned dma_bank_done;

static void vdp_register_write(unsigned reg, uint8_t value);

static int ready_flag(void *ctx)
{
    return *(const int *)ctx;
}

static int never(void *ctx)
{
    (void)ctx;
    return 0;
}

static void restart_entry(void)
{
    restart_count++;
    nmd_z80.c = 0x11;
    for (;;)
        (void)nmd_z80_wait_until(never, NULL);
}

static void power_entry(void)
{
    power_entry_count++;
    (void)zrd(0x0000);
    nmd_z80_wait_until(never, NULL);
}

static void sound_entry(void)
{
    entry_count++;
    nmd_z80.a = 0x42;
    (void)nmd_z80_wait_until(ready_flag, &leave_first_wait);
    nmd_z80.b = 0x99;
    nmd_z80_restart(0x0010);
}

static void map_entry(void)
{
    uint16_t bank = 0x01C0;
    int bit;

    map_ram_before = zrd(0x0000);
    zwr(0x1234, 0xA5);
    map_ram_after = zrd(0x1234);
    zwr(0x4000, 0x22);
    zwr(0x4001, 0x55);
    map_ym_status = zrd(0x4000);
    zwr(0x7F11, 0x9F);
    zout(0x7F, 0x90);
    for (bit = 0; bit <= 8; bit++)
        zwr(0x6000, (uint8_t)((bank >> bit) & 1u));
    bank_window_value = zrd(0xFFFF);
    zwr(0x8001, 0x44);
    nmd_z80_wait_until(never, NULL);
}

static void boundary_entry(void)
{
    boundary_started++;
    (void)nmd_z80_wait_until(ready_flag, &boundary_start_access);
    boundary_value = zrd(0x8000);
    boundary_done++;
    nmd_z80_wait_until(never, NULL);
}

static void dma_bank_entry(void)
{
    dma_bank_value = zrd(0x8000);
    dma_busy_when_bank_read = (unsigned)md_dma_bus_owned();
    dma_bank_done++;
    nmd_z80_wait_until(never, NULL);
}

static void unmapped_entry(void)
{
    (void)zrd(0x2000);
    nmd_z80_wait_until(never, NULL);
}

static const nmd_z80_routine routines[] = {
    { 0x0000, sound_entry, "sound_entry" },
    { 0x0010, restart_entry, "restart_entry" },
};
static const nmd_z80_routine power_routines[] = {
    { 0x0000, power_entry, "power_entry" },
};
static const nmd_z80_routine missing_reset_routine[] = {
    { 0x0010, restart_entry, "restart_entry" },
};
static const nmd_z80_routine map_routines[] = {
    { 0x0000, map_entry, "map_entry" },
};
static const nmd_z80_routine boundary_routines[] = {
    { 0x0000, boundary_entry, "boundary_entry" },
};
static const nmd_z80_routine dma_bank_routines[] = {
    { 0x0000, dma_bank_entry, "dma_bank_entry" },
};
static const nmd_z80_routine unmapped_routines[] = {
    { 0x0000, unmapped_entry, "unmapped_entry" },
};

static nmd_cart cart = {
    .title = "Z80 check",
    .region = NMD_NTSC,
    .rom = rom,
    .rom_size = sizeof rom,
    .z80_routines = routines,
    .z80_routine_count = sizeof routines / sizeof routines[0],
};

static nmd_cart map_cart = {
    .title = "Z80 map check",
    .region = NMD_NTSC,
    .rom = rom,
    .rom_size = sizeof rom,
    .z80_routines = map_routines,
    .z80_routine_count = sizeof map_routines / sizeof map_routines[0],
};

static nmd_cart boundary_cart = {
    .title = "Z80 BUSREQ boundary check",
    .region = NMD_NTSC,
    .rom = rom,
    .rom_size = sizeof rom,
    .z80_routines = boundary_routines,
    .z80_routine_count = sizeof boundary_routines / sizeof boundary_routines[0],
};

static nmd_cart dma_bank_cart = {
    .title = "Z80 DMA bank check",
    .region = NMD_NTSC,
    .rom = dma_rom,
    .rom_size = sizeof dma_rom,
    .z80_routines = dma_bank_routines,
    .z80_routine_count = sizeof dma_bank_routines / sizeof dma_bank_routines[0],
};

static nmd_cart unmapped_cart = {
    .title = "Z80 unmapped check",
    .region = NMD_NTSC,
    .rom = rom,
    .rom_size = sizeof rom,
    .z80_routines = unmapped_routines,
    .z80_routine_count = sizeof unmapped_routines / sizeof unmapped_routines[0],
};

static nmd_cart power_cart = {
    .title = "Z80 mid-access power reset check",
    .region = NMD_NTSC,
    .rom = rom,
    .rom_size = sizeof rom,
    .z80_routines = power_routines,
    .z80_routine_count = sizeof power_routines / sizeof power_routines[0],
};

static int fail_if_missing_entry_returns(void)
{
    nmd_cart missing = cart;
    missing.z80_routines = missing_reset_routine;
    missing.z80_routine_count = sizeof missing_reset_routine / sizeof missing_reset_routine[0];
    nmd_power(&missing);
    wr16(0xA11200, 0x0100); /* Release RESET while BUSREQ remains held. */
    wr16(0xA11100, 0x0000); /* Release BUSREQ: the absent reset entry is fatal. */
    return 3;
}

static int fail_if_unmapped_access_returns(void)
{
    nmd_power(&unmapped_cart);
    wr16(0xA11200, 0x0100);
    wr16(0xA11100, 0x0000);
    md_advance_to(md_clock + 100);
    return 3;
}

static void test_z80_memory_map_and_bank_window(void)
{
    nmd_power(&map_cart);
    wr8(0xE07FFF, 0xC3);
    wr8(0xE00001, 0x00);
    wr16(0xA11200, 0x0100);
    wr16(0xA11100, 0x0000);
    md_advance_to(md_clock + 2000);

    assert(map_ram_before == 0);
    assert(map_ram_after == 0xA5);
    assert(map_ym_status == 0);
    assert(bank_window_value == 0xC3); /* Nine bits select $E00000. */
    assert(rd8(0xE00001) == 0x44); /* $8001 maps to $E00001. */
    wr8(0xA04000, 0x22);
    wr8(0xA04001, 0x55);
    assert(rd8(0xA04000) == map_ym_status); /* Both CPUs share YM status. */
}

static void test_busreq_acknowledges_at_z80_bus_boundary(void)
{
    uint16_t status;

    boundary_started = 0;
    boundary_done = 0;
    boundary_start_access = 0;
    boundary_value = 0xFF;
    nmd_power(&boundary_cart);
    wr16(0xA11200, 0x0100);
    wr16(0xA11100, 0x0000);
    assert(boundary_started == 1);
    assert(boundary_done == 0); /* The Z80 routine is stopped at its explicit wait. */
    md_advance_to(md_clock + 100);
    assert(!md_z80_bus_call_active());

    vdp_register_write(1, 0x44); /* Display on, DMA initially disabled. */
    vdp_register_write(12, 0);
    vdp_register_write(15, 2);
    vdp_register_write(19, 2);
    vdp_register_write(20, 0);
    vdp_register_write(21, 0);
    vdp_register_write(22, 1); /* DMA source $000200. */
    vdp_register_write(23, 0);
    wr32(0xC00004, 0x40000080);
    vdp_register_write(1, 0x54);
    md_advance_to(md_clock + 50);
    md_dma_start(md_clock);
    assert(md_dma_bus_owned());

    boundary_start_access = 1;
    md_advance_to(md_clock + 15);
    assert(md_z80_bus_call_active());
    assert(md_z80_busack == 0);

    wr16(0xA11100, 0x0100);
    assert(boundary_done == 0);
    assert(!md_dma_bus_owned());
    assert(!md_z80_bus_call_active()); /* DMA cleared before the access boundary. */
    assert(md_z80_busack == 1);
    status = rd16(0xA11100);
    assert(status == 0); /* BUSREQ is acknowledged after the Z80 access ends. */
    assert(boundary_done == 0); /* BUSREQ parks the coroutine at the boundary. */

    wr16(0xA11100, 0x0000);
    assert(boundary_done == 1);
    assert(boundary_value == 0);
}

static void vdp_register_write(unsigned reg, uint8_t value)
{
    wr16(0xC00004, (uint16_t)(0x8000u | (reg << 8) | value));
}

static void test_z80_bank_window_waits_for_dma(void)
{
    unsigned i;

    memset(dma_rom, 0, sizeof dma_rom);
    dma_rom[0] = 0x5A;
    nmd_power(&dma_bank_cart);
    vdp_register_write(1, 0x44); /* Display on; DMA temporarily disabled. */
    vdp_register_write(12, 0);
    vdp_register_write(15, 2);
    vdp_register_write(19, 2);
    vdp_register_write(20, 0);
    vdp_register_write(21, 0);
    vdp_register_write(22, 1); /* Source $000200. */
    vdp_register_write(23, 0);
    wr32(0xC00004, 0x40000080); /* Set VRAM destination and DMA command. */
    vdp_register_write(1, 0x54); /* Enable display and DMA. */
    md_advance_to(md_clock + 50); /* Apply the scheduled register write. */
    md_dma_start(md_clock); /* Hold the 68000 bus across the Z80 bank read. */
    assert(md_dma_bus_owned());

    wr16(0xA11200, 0x0100);
    wr16(0xA11100, 0x0000);
    for (i = 0; i < 2000 && !dma_bank_done; i++)
        md_advance_to(md_clock + 15);

    assert(dma_bank_done == 1);
    assert(dma_busy_when_bank_read == 0);
    assert(dma_bank_value == 0x5A);
}

static void test_reset_busreq_and_resume(void)
{
    nmd_power(&cart);
    entry_count = 0;
    restart_count = 0;
    leave_first_wait = 0;
    assert(nmd_z80.a == 0);

    md_advance_to(150);
    assert(md_z80_cycle_count() == 10); /* 15 master clocks per Z80 T-state. */
    md_advance_to(1000);
    assert(entry_count == 0); /* BUSREQ and RESET are held at power on. */
    wr16(0xA11200, 0x0100);
    md_advance_to(md_clock + 100);
    assert(entry_count == 0); /* Releasing RESET alone does not start the Z80. */

    wr16(0xA11100, 0x0000);
    assert(entry_count == 1);
    assert(nmd_z80.a == 0x42 && nmd_z80.b == 0);

    wr16(0xA11100, 0x0100); /* Reacquire the bus while the routine is waiting. */
    leave_first_wait = 1;
    md_advance_to(md_clock + 300);
    assert(nmd_z80.b == 0); /* The waiting Z80 stays suspended under BUSREQ. */

    wr16(0xA11100, 0x0000);
    md_advance_to(md_clock + 300);
    assert(nmd_z80.b == 0x99 && nmd_z80.c == 0x11);
    assert(nmd_z80.pc == 0x0010 && restart_count == 1);

    wr16(0xA11200, 0x0000); /* Assert RESET during the running routine. */
    md_advance_to(md_clock + 100);
    assert(nmd_z80.pc == 0 && nmd_z80.iff1 == 0 && nmd_z80.iff2 == 0);
    wr16(0xA11200, 0x0100);
    md_advance_to(md_clock + 300);
    assert(entry_count == 2 && nmd_z80.a == 0x42);
    assert(nmd_z80.pc == 0x0010 && restart_count == 2);

    /* Powering the native machine again reuses the coroutine with clean state. */
    nmd_power(&cart);
    assert(nmd_z80.a == 0 && nmd_z80.b == 0 && nmd_z80.pc == 0);
    assert(entry_count == 2);
}

static void test_power_resets_mid_bus_call(void)
{
    power_entry_count = 0;
    nmd_power(&power_cart);
    wr16(0xA11200, 0x0100);
    wr16(0xA11100, 0x0000);
    assert(power_entry_count == 1);
    assert(md_z80_bus_call_active());

    nmd_power(&power_cart);
    assert(!md_z80_bus_call_active());
    assert(nmd_z80.pc == 0 && nmd_z80.iff1 == 0 && nmd_z80.iff2 == 0);

    wr16(0xA11200, 0x0100);
    wr16(0xA11100, 0x0000);
    md_advance_to(md_clock + 1000);
    assert(power_entry_count == 2);
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "missing-entry"))
        return fail_if_missing_entry_returns();
    if (argc == 2 && !strcmp(argv[1], "unmapped-access"))
        return fail_if_unmapped_access_returns();
    test_reset_busreq_and_resume();
    test_power_resets_mid_bus_call();
    test_z80_memory_map_and_bank_window();
    test_busreq_acknowledges_at_z80_bus_boundary();
    test_z80_bank_window_waits_for_dma();
    return 0;
}
