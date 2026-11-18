/* The Z80 sound routine starts only after BUSREQ and RESET are released. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <nmd/nmd.h>
#include <nmd/z80.h>

#include "md_internal.h"
#include "sound.h"

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
static uint8_t m68k_bank_value;
static uint64_t z80_write_clocks[8];
static unsigned z80_write_clock_count;
static uint64_t dma_bank_read_clock;
static unsigned dma_bank_trace_count;
static unsigned z80_vint_started;
static unsigned z80_vint_count;
static unsigned z80_vint_resumed;
static uint8_t z80_vint_iff1_at_entry;
static uint8_t z80_vint_iff2_at_entry;
static uint16_t z80_vint_pc_at_entry;
static int z80_vint_enable_on_start;
static int z80_vint_mode_on_start;
static int z80_vint_continue;
static const nmd_cost_profile z80_profile = {
    .version = NMD_COST_PROFILE_VERSION,
    .z80_tenths = {
        { 0 },
        { [NMD_Z80_KIND_PSG] = 10u },
    },
};

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

static void m68k_bank_entry(void)
{
    m68k_bank_value = zrd(0x8000);
    nmd_z80_wait_until(never, NULL);
}

static void clocked_write_entry(void)
{
    unsigned i;
    for (i = 0; i < 5u; i++)
        zwr(0x7F11, (uint8_t)(0x90u + i));
    nmd_z80_wait_until(never, NULL);
}

static void z80_vint_main_entry(void)
{
    z80_vint_started++;
    nmd_z80.im = (uint8_t)z80_vint_mode_on_start;
    nmd_z80.i = 0x12;
    nmd_z80.iff1 = (uint8_t)z80_vint_enable_on_start;
    nmd_z80.iff2 = (uint8_t)z80_vint_enable_on_start;
    nmd_z80.pc = 0x0010;
    (void)nmd_z80_wait_until(ready_flag, &z80_vint_continue);
    z80_vint_resumed++;
    nmd_z80_wait_until(never, NULL);
}

static void z80_vint_handler(void)
{
    z80_vint_count++;
    z80_vint_iff1_at_entry = nmd_z80.iff1;
    z80_vint_iff2_at_entry = nmd_z80.iff2;
    z80_vint_pc_at_entry = nmd_z80.pc;
}

static void record_z80_write_clock(uint64_t clock, uint8_t operation,
                                   uint8_t width, uint32_t addr, uint32_t value)
{
    (void)value;
    if (operation == MD_Z80_TRACE_WRITE && width == 1 && addr == 0x7F11u) {
        assert(z80_write_clock_count < sizeof z80_write_clocks / sizeof z80_write_clocks[0]);
        z80_write_clocks[z80_write_clock_count++] = clock;
    }
}

static void record_dma_bank_read(uint64_t clock, uint8_t operation,
                                 uint8_t width, uint32_t addr, uint32_t value)
{
    (void)value;
    if (operation == MD_Z80_TRACE_READ && width == 1 && addr == 0x8000u) {
        dma_bank_read_clock = clock;
        dma_bank_trace_count++;
    }
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
static const nmd_z80_routine m68k_bank_routines[] = {
    { 0x0000, m68k_bank_entry, "m68k_bank_entry" },
};
static const nmd_z80_routine clocked_write_routines[] = {
    { 0x0000, clocked_write_entry, "clocked_write_entry" },
};
static const nmd_z80_routine z80_vint_routines[] = {
    { 0x0000, z80_vint_main_entry, "z80_vint_main_entry" },
    { 0x0020, z80_vint_handler, "z80_vint_im2_handler" },
    { 0x0038, z80_vint_handler, "z80_vint_handler" },
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

static nmd_cart m68k_bank_cart = {
    .title = "68000 Z80 bank-register check",
    .region = NMD_NTSC,
    .rom = rom,
    .rom_size = sizeof rom,
    .z80_routines = m68k_bank_routines,
    .z80_routine_count = sizeof m68k_bank_routines / sizeof m68k_bank_routines[0],
};

static nmd_cart clocked_write_cart = {
    .title = "Z80 coarse/fine clock check",
    .region = NMD_NTSC,
    .rom = rom,
    .rom_size = sizeof rom,
    .z80_routines = clocked_write_routines,
    .z80_routine_count = sizeof clocked_write_routines / sizeof clocked_write_routines[0],
};

static nmd_cart z80_vint_cart = {
    .title = "Z80 VINT check",
    .region = NMD_NTSC,
    .rom = rom,
    .rom_size = sizeof rom,
    .z80_routines = z80_vint_routines,
    .z80_routine_count = sizeof z80_vint_routines / sizeof z80_vint_routines[0],
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
    assert(map_ym_status & 0x80u); /* The data write's busy window is still active. */
    assert(bank_window_value == 0xC3); /* Nine bits select $E00000. */
    assert(rd8(0xE00001) == 0x44); /* $8001 maps to $E00001. */
    wr16(0xA11200, 0x0000);
    wr16(0xA11100, 0x0100);
    wr16(0xA11200, 0x0100);
    while (rd16(0xA11100) & 0x0100u) {
    }
    wr8(0xA04000, 0x22);
    wr8(0xA04001, 0x55);
    assert((rd8(0xA04000) & 0x7Fu) == (map_ym_status & 0x7Fu));
    /* Both CPUs see the same sticky status bits; the busy bit is time-dependent. */
    wr16(0xA11100, 0x0000);
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

static uint64_t run_z80_bank_window_dma_case(int stepwise)
{
    unsigned i;

    memset(dma_rom, 0, sizeof dma_rom);
    dma_rom[0] = 0x5A;
    dma_bank_done = 0;
    dma_bank_trace_count = 0;
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

    md_z80_bus_trace_hook = record_dma_bank_read;
    wr16(0xA11200, 0x0100);
    wr16(0xA11100, 0x0000);
    if (stepwise) {
        for (i = 0; i < 2000 && !dma_bank_done; i++)
            md_advance_to(md_clock + 15);
    } else {
        md_advance_to(md_clock + 2000);
    }
    md_z80_bus_trace_hook = NULL;

    assert(dma_bank_done == 1);
    assert(dma_busy_when_bank_read == 0);
    assert(dma_bank_value == 0x5A);
    assert(dma_bank_trace_count == 1u);
    return dma_bank_read_clock;
}

static void test_z80_bank_window_waits_for_dma(void)
{
    uint64_t coarse_clock = run_z80_bank_window_dma_case(0);
    uint64_t fine_clock = run_z80_bank_window_dma_case(1);

    assert(coarse_clock == fine_clock);
}

static void test_68000_programs_z80_bank_register(void)
{
    const uint16_t bank = 0x01C0;
    unsigned bit;

    m68k_bank_value = 0;
    nmd_power(&m68k_bank_cart);
    wr8(0xE00000, 0xA5); /* Bank $1C0 reaches this work-RAM byte. */
    for (bit = 0; bit < 9u; bit++) {
        uint8_t value = (uint8_t)((bank >> bit) & 1u);
        if (bit & 1u)
            wr8(0xA06000, value);
        else
            wr16(0xA06000, (uint16_t)value << 8);
    }
    wr16(0xA11200, 0x0100);
    wr16(0xA11100, 0x0000);
    md_advance_to(md_clock + 1000u);
    assert(m68k_bank_value == 0xA5);
}

static int capture_z80_write_clocks(int stepwise, uint64_t *clocks, int16_t *pcm)
{
    uint64_t start;
    unsigned i;
    const uint64_t end_offset = 70000u;

    z80_write_clock_count = 0;
    md_z80_bus_trace_hook = record_z80_write_clock;
    nmd_power(&clocked_write_cart);
    wr16(0xA11200, 0x0100);
    wr16(0xA11100, 0x0000);
    start = md_clock;
    if (stepwise) {
        for (i = 1; i <= 1000u; i++)
            md_advance_to(start + i);
        for (i = 1015u; i < end_offset; i += 15u)
            md_advance_to(start + i);
        md_advance_to(start + end_offset);
    } else {
        md_advance_to(start + end_offset);
    }
    md_z80_bus_trace_hook = NULL;
    assert(z80_write_clock_count == 5u);
    memcpy(clocks, z80_write_clocks, sizeof z80_write_clocks[0] * 5u);
    return md_sound_audio_take(pcm, 256);
}

static void test_z80_bus_timestamps_ignore_caller_step_size(void)
{
    uint64_t coarse[5], fine[5];
    int16_t coarse_pcm[512], fine_pcm[512];
    int coarse_frames, fine_frames;
    unsigned i;

    coarse_frames = capture_z80_write_clocks(0, coarse, coarse_pcm);
    fine_frames = capture_z80_write_clocks(1, fine, fine_pcm);
    for (i = 0; i < 5u; i++)
        assert(coarse[i] == fine[i]);
    assert(coarse_frames > 0 && coarse_frames == fine_frames);
    assert(memcmp(coarse_pcm, fine_pcm,
                  (size_t)coarse_frames * 2u * sizeof coarse_pcm[0]) == 0);
}

static void test_z80_cost_profile_tstates(void)
{
    unsigned i;

    clocked_write_cart.cost_profile = &z80_profile;
    z80_write_clock_count = 0;
    md_z80_bus_trace_hook = record_z80_write_clock;
    nmd_power(&clocked_write_cart);
    nmd_set_cpu_costs(0);
    wr16(0xA11200u, 0x0100u);
    wr16(0xA11100u, 0x0000u);
    md_advance_to(md_clock + 70000u);
    md_z80_bus_trace_hook = NULL;

    assert(z80_write_clock_count == 5u);
    for (i = 1; i < z80_write_clock_count; i++)
        assert(z80_write_clocks[i] - z80_write_clocks[i - 1u] == 60u);

    clocked_write_cart.cost_profile = NULL;
    nmd_set_cpu_costs(1);
}

static void test_z80_vint_mask_busreq_and_return(void)
{
    z80_vint_mode_on_start = 1;
    z80_vint_enable_on_start = 0;
    z80_vint_started = z80_vint_count = z80_vint_resumed = 0;
    z80_vint_continue = 0;
    nmd_power(&z80_vint_cart);
    md_vdp_set_register(1, 0x20); /* Enable VDP VINT. */
    wr16(0xA11200, 0x0100);
    wr16(0xA11100, 0x0000);
    assert(z80_vint_started == 1);
    md_advance_to(md_vdp_next_vblank_clock());
    assert(z80_vint_count == 0); /* IFF1 masks the first VINT. */

    z80_vint_enable_on_start = 1;
    z80_vint_mode_on_start = 2;
    z80_vint_started = z80_vint_count = z80_vint_resumed = 0;
    z80_vint_continue = 0;
    nmd_power(&z80_vint_cart);
    md_vdp_set_register(1, 0x20);
    wr8(0xA012FF, 0x20); /* IM2 vector at I:$FF points to the registered $0020 routine. */
    wr8(0xA01300, 0x00);
    wr16(0xA11200, 0x0100);
    assert(z80_vint_started == 0); /* BUSREQ still holds the Z80. */
    md_advance_to(md_vdp_next_vblank_clock());
    assert(z80_vint_count == 0); /* The VINT waits while BUSREQ owns the bus. */

    wr16(0xA11100, 0x0000);
    md_advance_to(md_clock + 100u); /* IM2 vector reads consume Z80 bus cycles. */
    assert(z80_vint_started == 1);
    assert(z80_vint_count == 1);
    assert(z80_vint_iff1_at_entry == 0 && z80_vint_iff2_at_entry == 0);
    assert(z80_vint_pc_at_entry == 0x0020);
    assert(nmd_z80.pc == 0x0010 && nmd_z80.iff1 && nmd_z80.iff2);

    z80_vint_continue = 1;
    md_advance_to(md_clock + 15u);
    assert(z80_vint_resumed == 1); /* The interrupted coroutine continues. */
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

    nmd_z80.i = 0x42;
    nmd_z80.r = 0x99;
    wr16(0xA11200, 0x0000); /* Assert RESET during the running routine. */
    md_advance_to(md_clock + 100);
    assert(nmd_z80.pc == 0 && nmd_z80.iff1 == 0 && nmd_z80.iff2 == 0);
    assert(nmd_z80.i == 0 && nmd_z80.r == 0);
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
    test_68000_programs_z80_bank_register();
    test_z80_bus_timestamps_ignore_caller_step_size();
    test_z80_cost_profile_tstates();
    test_z80_vint_mask_busreq_and_return();
    return 0;
}
