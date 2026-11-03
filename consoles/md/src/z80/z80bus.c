/* The Z80 memory and I/O map, including the 68000 bank window. */
#include <stdint.h>

#include "md_internal.h"
#include "nmd/z80.h"
#include "nk/fatal.h"
#include "../sound/sound.h"

static uint16_t z80_bank;
md_z80_bus_trace_fn md_z80_bus_trace_hook;

static void trace_z80_bus(uint8_t operation, uint16_t addr, uint8_t value)
{
    if (md_z80_bus_trace_hook)
        md_z80_bus_trace_hook(md_clock, operation, 1, addr, value);
}

void md_z80_bus_reset(void)
{
    z80_bank = 0;
}

static int bank_bus_ready(void *ctx)
{
    (void)ctx;
    return md_z80_window_bus_ready();
}

static uint32_t bank_address(uint16_t addr)
{
    return ((uint32_t)z80_bank << 15) | (uint32_t)(addr & 0x7FFFu);
}

uint8_t zrd(uint16_t addr)
{
    uint8_t value;

    md_z80_bus_access_begin(3);
    if (addr <= 0x1FFFu) {
        value = md_z80_ram[addr];
    } else if (addr >= 0x4000u && addr <= 0x4003u) {
        value = md_ym_read((uint8_t)(addr - 0x4000u), md_clock);
    } else if (addr == 0x6000u) {
        nk_fatal("Z80 read from write-only bank register at $6000");
    } else if (addr >= 0x8000u) {
        uint32_t mapped;
        (void)nmd_z80_wait_until(bank_bus_ready, NULL);
        md_z80_window_bus_reserve();
        mapped = bank_address(addr);
        value = md_z80_window_read8(mapped);
    } else {
        nk_fatal("unsupported Z80 memory read at $%04X", addr);
    }
    trace_z80_bus(MD_Z80_TRACE_READ, addr, value);
    md_z80_bus_access_end();
    return value;
}

void zwr(uint16_t addr, uint8_t value)
{
    md_z80_bus_access_begin(3);
    if (addr <= 0x1FFFu) {
        md_z80_ram[addr] = value;
    } else if (addr >= 0x4000u && addr <= 0x4003u) {
        md_ym_write((uint8_t)(addr - 0x4000u), value, md_clock);
    } else if (addr == 0x6000u) {
        z80_bank = (uint16_t)(((z80_bank >> 1) | ((uint16_t)(value & 1u) << 8)) & 0x01FFu);
    } else if (addr >= 0x8000u) {
        uint32_t mapped;
        (void)nmd_z80_wait_until(bank_bus_ready, NULL);
        md_z80_window_bus_reserve();
        mapped = bank_address(addr);
        md_z80_window_write8(mapped, value);
    } else {
        nk_fatal("unsupported Z80 memory write at $%04X", addr);
    }
    trace_z80_bus(MD_Z80_TRACE_WRITE, addr, value);
    md_z80_bus_access_end();
}

uint8_t zin(uint8_t port)
{
    md_z80_bus_access_begin(4);
    if (port == 0x7Fu)
        nk_fatal("PSG is write-only at Z80 I/O port $7F");
    nk_fatal("unsupported Z80 I/O read at port $%02X", port);
}

void zout(uint8_t port, uint8_t value)
{
    md_z80_bus_access_begin(4);
    if (port == 0x7Fu) {
        md_psg_write(value, md_clock);
        if (md_z80_bus_trace_hook)
            md_z80_bus_trace_hook(md_clock, MD_Z80_TRACE_WRITE, 1, port, value);
        md_z80_bus_access_end();
        return;
    }
    nk_fatal("unsupported Z80 I/O write at port $%02X", port);
}
