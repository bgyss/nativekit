/* Basic public bus calls must read cartridge ROM and round-trip work RAM. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <nmd/nmd.h>

#include "md_internal.h"

static const uint8_t rom[] = { 0x12, 0x34, 0x56, 0x78 };
static unsigned irq_checks;
static unsigned irq_checks_while_deferred;

static void check_interrupts(void)
{
    irq_checks++;
    if (md_irq_defer_depth)
        irq_checks_while_deferred++;
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
    return 0;
}
