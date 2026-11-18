/* Basic public bus calls must read cartridge ROM and round-trip work RAM. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <nmd/nmd.h>

#include "md_internal.h"
#include "md_console.h"

static const uint8_t rom[] = { 0x12, 0x34, 0x56, 0x78 };
static const nmd_cost_profile m68k_profile = {
    .version = NMD_COST_PROFILE_VERSION,
    .m68k_tenths = {
        { [NMD_KIND_ROM] = 10u },
        { [NMD_KIND_WORK_RAM] = 20u },
    },
};
static const nmd_cost_profile fractional_profile = {
    .version = NMD_COST_PROFILE_VERSION,
    .m68k_tenths = {
        { [NMD_KIND_ROM] = 3u },
    },
};
static unsigned counted_reads;
static unsigned counted_writes;
static uint32_t counted_read_addr;
static uint32_t counted_write_addr;
static uint8_t counted_read_value;
static uint8_t counted_write_value;
static unsigned irq_checks;
static unsigned irq_checks_while_deferred;

static void count_read(uint32_t addr, uint8_t value)
{
    counted_reads++;
    counted_read_addr = addr;
    counted_read_value = value;
}

static void count_write(uint32_t addr, uint8_t value)
{
    counted_writes++;
    counted_write_addr = addr;
    counted_write_value = value;
}

static void check_interrupts(void)
{
    irq_checks++;
    if (md_irq_defer_depth)
        irq_checks_while_deferred++;
}

static void vdp_register(unsigned reg, uint8_t value)
{
    wr16(0xC00004u, (uint16_t)(0x8000u | (reg << 8) | value));
}

static void test_m68k_cost_profile_kind_weights(void)
{
    nmd_cart cart = {
        .title = "M68K cost profile check",
        .region = NMD_NTSC,
        .rom = rom,
        .rom_size = sizeof rom,
        .cost_profile = &m68k_profile,
    };
    uint64_t before;

    assert(nmd_access_kind(0x000000u) == NMD_KIND_ROM);
    assert(nmd_access_kind(0xE00000u) == NMD_KIND_WORK_RAM);
    assert(nmd_access_kind(0xA00000u) == NMD_KIND_Z80_RAM);
    assert(nmd_access_kind(0xA10001u) == NMD_KIND_IO);
    assert(nmd_access_kind(0xC00004u) == NMD_KIND_VDP);
    assert(nmd_access_kind(0xC00011u) == NMD_KIND_SOUND);

    nmd_power(&cart);
    before = md_clock;
    assert(rd8(0) == 0x12u);
    assert(md_clock - before == 29u);
    before = md_clock;
    wr8(0xE00000u, 0x5Au);
    assert(md_clock - before == 30u);
}

static void test_nmd_access_kind_reporting(void)
{
    nmd_cart cart = {
        .title = "native count-hook check",
        .region = NMD_NTSC,
        .rom = rom,
        .rom_size = sizeof rom,
    };

    assert(nmd_console.kind_count == NMD_KINDS + NMD_Z80_KINDS);
    assert(strcmp(nmd_console.kind_names[NMD_KIND_ROM], "rom") == 0);
    assert(strcmp(nmd_console.kind_names[NMD_KINDS + NMD_Z80_KIND_PSG], "z80 psg") == 0);
    assert(nmd_console.access_kind(0x01007F11u) == NMD_KINDS + NMD_Z80_KIND_PSG);
    assert(nmd_console.count_group);
    assert(nmd_console.count_group(0x00000000u) == 0);
    assert(nmd_console.count_group(0x01007F11u) == 1);

    nmd_power(&cart);
    counted_reads = counted_writes = 0;
    nmd_console.counts_hooks(count_read, count_write);
    (void)rd8(0);
    wr8(0xE00000u, 0x5Au);
    nmd_console.counts_hooks(NULL, NULL);
    assert(counted_reads == 1 && counted_read_addr == 0 && counted_read_value == 0x12u);
    assert(counted_writes == 1 && counted_write_addr == 0xE00000u &&
           counted_write_value == 0x5Au);
}

static void test_m68k_cost_profile_tenths_carry(void)
{
    nmd_cart cart = {
        .title = "M68K fractional cost check",
        .region = NMD_NTSC,
        .rom = rom,
        .rom_size = sizeof rom,
        .cost_profile = &fractional_profile,
    };
    static const uint64_t expected[] = { 28u, 28u, 28u, 29u };
    unsigned i;

    nmd_power(&cart);
    for (i = 0; i < sizeof expected / sizeof expected[0]; i++) {
        uint64_t before = md_clock;
        (void)rd8(0);
        assert(md_clock - before == expected[i]);
    }
}

static void test_cost_profile_null_is_zero(void)
{
    nmd_cart cart = {
        .title = "null cost profile check",
        .region = NMD_NTSC,
        .rom = rom,
        .rom_size = sizeof rom,
        .cost_profile = NULL,
    };
    uint64_t before;

    nmd_power(&cart);
    before = md_clock;
    assert(rd8(0) == 0x12u);
    assert(md_clock - before == 28u);
}

static void test_m68k_costs_off_keeps_devices_running(void)
{
    nmd_cart cart = {
        .title = "costs off device clock check",
        .region = NMD_NTSC,
        .rom = rom,
        .rom_size = sizeof rom,
    };
    uint64_t before;
    uint64_t vblank;

    nmd_power(&cart);
    vdp_register(1, 0x10u);
    vdp_register(15, 2u);
    vdp_register(19, 1u);
    vdp_register(20, 0u);
    vdp_register(21, 0u);
    vdp_register(22, 0u);
    vdp_register(23, 0u);

    nmd_set_cpu_costs(0);
    before = md_clock;
    assert(rd8(0) == 0x12u);
    assert(md_clock == before);

    before = md_clock;
    wr32(0xC00004u, 0x40000080u);
    assert(md_clock > before);
    assert(!md_dma_bus_owned());

    vblank = md_vdp_next_vblank_clock();
    assert(vblank > md_clock);
    md_advance_to(vblank);
    assert(md_vdp_read16(MD_VDP_CONTROL_PORT, md_clock) & 0x0008u);
    nmd_set_cpu_costs(1);
}

static int fail_if_fatal_mode_returns(const char *mode)
{
    if (!strcmp(mode, "odd-word-read"))
        (void)rd16(0xFF0001);
    else if (!strcmp(mode, "odd-word-write"))
        wr16(0xFF0001, 0x1234);
    else if (!strcmp(mode, "odd-long-read"))
        (void)rd32(0xFF0001);
    else if (!strcmp(mode, "odd-long-write"))
        wr32(0xFF0001, 0x12345678);
    else if (!strcmp(mode, "rom-write"))
        wr8(0, 0xFF);
    else if (!strcmp(mode, "reserved-read"))
        (void)rd8(0x400000);
    else if (!strcmp(mode, "z80-without-busreq")) {
        wr16(0xA11100, 0);
        (void)rd8(0xA00000);
    } else if (!strcmp(mode, "z80-word"))
        (void)rd16(0xA00000);
    else if (!strcmp(mode, "ym-without-busreq")) {
        wr16(0xA11100, 0x0000);
        (void)rd8(0xA04000);
    }
    else if (!strcmp(mode, "bank-without-busreq")) {
        wr16(0xA11100, 0x0000);
        wr8(0xA06000, 1);
    }
    else if (!strcmp(mode, "vdp-word-read-0a"))
        (void)rd16(0xC0000A);
    else if (!strcmp(mode, "vdp-word-read-d-0a"))
        (void)rd16(0xD0000A);
    else if (!strcmp(mode, "vdp-byte-read-0a"))
        (void)rd8(0xC0000A);
    else if (!strcmp(mode, "vdp-byte-read-d-0a"))
        (void)rd8(0xD0000A);
    else
        return 2;
    return 3;
}

int main(int argc, char **argv)
{
    nmd_cart cart = {
        .title = "bus check",
        .region = NMD_NTSC,
        .rom = rom,
        .rom_size = sizeof rom,
    };

    nmd_power(&cart);
    if (argc == 2)
        return fail_if_fatal_mode_returns(argv[1]);

    assert(md_clock == 0);
    assert(rd8(0) == 0x12);
    assert(rd16(0) == 0x1234);
    assert(rd32(0) == 0x12345678);
    assert(md_clock == 7u * 4u * 4u);

    wr16(0xE00000, 0xABCD);
    assert(rd16(0xFF0000) == 0xABCD);
    wr32(0xFF0002, 0x13579BDF);
    assert(rd32(0xE00002) == 0x13579BDF);

    wr16(0xFF0010, 0x12AB);
    assert(rd8(0xFF0010) == 0x12);
    assert(rd8(0xFF0011) == 0xAB);
    wr8(0xFF0010, 0xCD);
    wr8(0xFF0011, 0xEF);
    assert(rd16(0xFF0010) == 0xCDEF);

    assert(rd16(0x01000000) == 0x1234);
    wr16(0x01FF0020, 0x5678);
    assert(rd16(0xE00020) == 0x5678);

    {
        uint64_t before = md_clock;
        (void)rd16(0xFF0000);
        assert(md_clock - before == 28);
        before = md_clock;
        md_irq_check_hook = check_interrupts;
        irq_checks = irq_checks_while_deferred = 0;
        (void)rd32(0xFF0000);
        assert(md_clock - before == 56);
        assert(irq_checks == 1);
        assert(irq_checks_while_deferred == 0);
        md_irq_check_hook = NULL;
    }

    wr16(0xA11000, 0x0100);
    assert(md_memory_mode == 1);
    wr8(0xA11001, 0);
    assert(md_memory_mode == 1);
    wr8(0xA11000, 0);
    assert(md_memory_mode == 0);

    {
        uint64_t before = md_clock;
        wr16(0xA11200, 0x0000);
        wr16(0xA11100, 0x0100);
        wr16(0xA11200, 0x0100);
        while (rd16(0xA11100) & 0x0100u) {
        }
        wr16(0xA04000, 0x3000);
        wr8(0xA04001, 0x00);
        wr8(0xA06000, 0x01);
        wr8(0xA10003, 0xFF);
        wr16(0xC00004, 0x8104);
        wr16(0xC00006, 0x8104);
        wr16(0xD00004, 0x8104);
        wr16(0xC00000, 0x000E);
        wr8(0xC00011, 0x9F);
        wr16(0xA11100, 0x0000);
        assert(md_clock - before >= 14u * 28u);
        assert((md_clock - before) % 28u == 0);
    }

    wr16(0xA11100, 0x0100);
    assert(md_z80_busreq == 1);
    assert(rd16(0xA11100) == 0);
    wr8(0xA00000, 0x5A);
    assert(rd8(0xA00000) == 0x5A);
    wr16(0xA11100, 0x0000);
    assert(md_z80_busreq == 0);
    assert(rd16(0xA11100) == 0x0100);

    wr16(0xA11200, 0x0100);
    assert(md_z80_reset == 0);
    wr8(0xA11200, 0);
    assert(md_z80_reset == 1);

    wr8(0xFF0030, 0x00);
    nmd_cpu.sr = 0xA51F;
    {
        uint64_t before = md_clock;
        md_irq_check_hook = check_interrupts;
        irq_checks = irq_checks_while_deferred = 0;
        assert(nmd_tas(0xFF0030) == 0x00);
        assert(md_clock - before == 56);
        assert(irq_checks == 1);
        assert(irq_checks_while_deferred == 0);
        md_irq_check_hook = NULL;
    }
    assert(rd8(0xFF0030) == 0x80);
    assert((nmd_cpu.sr & 0x001Fu) == 0x0014u);

    wr8(0xFF0030, 0x80);
    nmd_cpu.sr = 0xA517;
    assert(nmd_tas(0xFF0030) == 0x80);
    assert(rd8(0xFF0030) == 0x80);
    assert((nmd_cpu.sr & 0x001Fu) == 0x0018u);
    assert(md_irq_defer_depth == 0);
    test_m68k_cost_profile_kind_weights();
    test_m68k_cost_profile_tenths_carry();
    test_cost_profile_null_is_zero();
    test_m68k_costs_off_keeps_devices_running();
    test_nmd_access_kind_reporting();
    return 0;
}
