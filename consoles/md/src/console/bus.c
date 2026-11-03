/*
 * bus.c: the 68000-visible bus and the base Mega Drive memory map.
 *
 * A native game pays for each memory bus cycle, not for the host C used to
 * calculate its operands. The master-clock scheduler is connected through
 * md_clock_hook when Stage 2's VDP timing is present.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "md_internal.h"
#include "nk/fatal.h"
#include "../sound/sound.h"

#define MD_ADDRESS_MASK 0x00FFFFFFu
#define MD_BUS_CYCLE_MASTER_CLOCKS 28ull /* 4 M68000 clocks, 7 master clocks each */

nmd_cpu_state nmd_cpu;
uint64_t md_clock;
uint8_t md_z80_ram[0x2000];
uint8_t md_memory_mode;
uint8_t md_z80_busreq;
uint8_t md_z80_busack;
uint8_t md_z80_reset;
unsigned md_irq_defer_depth;
void (*md_clock_hook)(uint64_t to_clock);
void (*md_irq_check_hook)(void);
md_bus_trace_fn md_bus_trace_hook;

static const nmd_cart *active_cart;
static uint8_t work_ram[0x10000];
static uint64_t md_68k_bus_busy_until;

static _Noreturn void md_vfatal(const char *fmt, va_list ap)
{
    fprintf(stderr, "nativemd: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(70);
}

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    md_vfatal(fmt, ap);
}

static uint32_t md_address(uint32_t addr)
{
    return addr & MD_ADDRESS_MASK;
}

static void md_bus_advance_to(uint64_t to_clock)
{
    if (md_clock_hook)
        md_clock_hook(to_clock);
    else
        md_clock = to_clock;

    if (md_clock < to_clock)
        md_clock = to_clock;
}

static void md_bus_cycle_complete(uint64_t access_start)
{
    uint64_t access_end = access_start + MD_BUS_CYCLE_MASTER_CLOCKS;
    if (md_68k_bus_busy_until < access_end)
        md_68k_bus_busy_until = access_end;
    if (md_clock > access_end)
        access_end = md_clock;
    md_bus_advance_to(access_end);
    while (md_dma_bus_owned())
        md_bus_advance_to(md_dma_next_event());
}

static void md_bus_begin_access(void)
{
    md_bus_advance_to(md_clock);
    while (md_68k_bus_busy_until > md_clock)
        md_bus_advance_to(md_68k_bus_busy_until);
}

static void md_bus_trace(uint8_t operation, uint8_t width, uint32_t addr, uint32_t value)
{
    if (md_bus_trace_hook)
        md_bus_trace_hook(md_clock, operation, width, addr, value);
}

static void md_bus_interrupt_check(void)
{
    if (!md_irq_defer_depth && md_irq_check_hook)
        md_irq_check_hook();
}

static void md_require_aligned(uint32_t addr, const char *operation)
{
    if (addr & 1)
        nk_fatal("68000 address error: odd-address %s at $%06X",
                 operation, (unsigned)addr);
}

static const uint8_t *md_rom_bytes(uint32_t addr, unsigned count)
{
    if (!active_cart || !active_cart->rom)
        nk_fatal("68000 read at $%06X without a cartridge ROM", (unsigned)addr);
    if (addr > active_cart->rom_size || count > active_cart->rom_size - addr)
        nk_fatal("68000 access outside cartridge ROM at $%06X", (unsigned)addr);
    return active_cart->rom + addr;
}

static int md_is_work_ram(uint32_t addr)
{
    return addr >= 0xE00000u;
}

uint16_t nmd_dma_source_read16(uint32_t addr)
{
    const uint8_t *bytes;
    uint32_t offset;

    addr = md_address(addr);
    md_require_aligned(addr, "DMA source read");
    if (addr <= 0x3FFFFEu) {
        bytes = md_rom_bytes(addr, 2);
        return (uint16_t)((uint16_t)bytes[0] << 8 | bytes[1]);
    }
    if (md_is_work_ram(addr)) {
        offset = addr & 0xFFFFu;
        return (uint16_t)((uint16_t)work_ram[offset] << 8 |
                          work_ram[(offset + 1u) & 0xFFFFu]);
    }
    nk_fatal("unsupported VDP DMA source at $%06X", (unsigned)addr);
}

static int md_is_z80_ram(uint32_t addr)
{
    return addr >= 0xA00000u && addr <= 0xA01FFFu;
}

static int md_is_vdp(uint32_t addr)
{
    return addr >= 0xC00000u && addr <= 0xDFFFFFu;
}

static uint32_t md_vdp_port(uint32_t addr)
{
    return addr & 0x1Fu;
}

static uint32_t md_vdp_word_port(uint32_t addr)
{
    uint32_t port = md_vdp_port(addr);
    if (port == 0x00u || port == 0x02u)
        return MD_VDP_DATA_PORT;
    if (port == 0x04u || port == 0x06u)
        return MD_VDP_CONTROL_PORT;
    if (port == 0x08u)
        return MD_VDP_HV_PORT;
    return UINT32_MAX;
}

static uint8_t md_read8(uint32_t addr, int z80_access)
{
    if (addr <= 0x3FFFFFu)
        return md_rom_bytes(addr, 1)[0];

    if (md_is_z80_ram(addr)) {
        if (!z80_access && !md_z80_busack)
            nk_fatal("68000 read of Z80 RAM at $%06X without BUSREQ", (unsigned)addr);
        return md_z80_ram[addr & 0x1FFFu];
    }

    if (md_is_work_ram(addr))
        return work_ram[addr & 0xFFFFu];

    if (addr == 0xA11100u)
        return md_z80_busack ? 0 : 1;

    if (addr == 0xA11101u)
        nk_fatal("68000 read from the unconnected low lane of Z80 BUSREQ at $%06X",
                 (unsigned)addr);

    if (addr >= 0xA04000u && addr <= 0xA04003u) {
        if (!z80_access && !md_z80_busack)
            nk_fatal("68000 YM2612 read at $%06X without BUSREQ acknowledgement",
                     (unsigned)addr);
        return md_ym_read((uint8_t)(addr - 0xA04000u), md_clock);
    }
    if (addr >= 0xA10000u && addr <= 0xA1001Fu)
        nk_fatal("I/O read state is not implemented at $%06X", (unsigned)addr);
    if (md_is_vdp(addr)) {
        uint32_t port = md_vdp_port(addr);
        if (port == 0x08u)
            return (uint8_t)(md_vdp_hv_counter() >> 8);
        if (port == 0x09u)
            return (uint8_t)md_vdp_hv_counter();
        nk_fatal("VDP read state is not implemented at $%06X", (unsigned)addr);
    }

    nk_fatal("unsupported 68000 byte read at $%06X", (unsigned)addr);
}

static uint16_t md_read16(uint32_t addr)
{
    if (addr <= 0x3FFFFEu) {
        const uint8_t *p = md_rom_bytes(addr, 2);
        return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
    }

    if (md_is_z80_ram(addr))
        nk_fatal("68000 word access to 8-bit Z80 RAM at $%06X", (unsigned)addr);

    if (md_is_work_ram(addr)) {
        uint32_t offset = addr & 0xFFFFu;
        return (uint16_t)((uint16_t)work_ram[offset] << 8 |
                          work_ram[(offset + 1u) & 0xFFFFu]);
    }

    if (addr == 0xA11100u)
        return md_z80_busack ? 0 : 0x0100;

    if (addr >= 0xA04000u && addr <= 0xA04003u)
        nk_fatal("YM2612 byte ports require byte access at $%06X", (unsigned)addr);
    if (addr >= 0xA10000u && addr <= 0xA1001Fu)
        nk_fatal("I/O read state is not implemented at $%06X", (unsigned)addr);
    if (md_is_vdp(addr)) {
        uint32_t port = md_vdp_word_port(addr);
        if (port != UINT32_MAX)
            return md_vdp_read16(port, md_clock);
        nk_fatal("prohibited VDP word read at $%06X", (unsigned)addr);
    }

    nk_fatal("unsupported 68000 word read at $%06X", (unsigned)addr);
}

static void md_write8(uint32_t addr, uint8_t value, int z80_access)
{
    if (addr <= 0x3FFFFFu)
        nk_fatal("68000 byte write to cartridge ROM at $%06X", (unsigned)addr);

    if (md_is_z80_ram(addr)) {
        if (!z80_access && !md_z80_busack)
            nk_fatal("68000 write of Z80 RAM at $%06X without BUSREQ", (unsigned)addr);
        md_z80_ram[addr & 0x1FFFu] = value;
        return;
    }

    if (md_is_work_ram(addr)) {
        work_ram[addr & 0xFFFFu] = value;
        return;
    }

    if (addr == 0xA11000u) {
        md_memory_mode = value & 1u;
        return;
    }
    if (addr == 0xA11001u)
        return; /* the control register is connected to the high data lane */

    if (addr == 0xA11100u) {
        md_z80_busreq = value & 1u;
        if (!md_z80_busreq)
            md_z80_busack = 0;
        return;
    }
    if (addr == 0xA11101u)
        return;

    if (addr == 0xA11200u) {
        md_z80_reset = (value & 1u) == 0;
        return;
    }
    if (addr == 0xA11201u)
        return;

    if (addr >= 0xA04000u && addr <= 0xA04003u) {
        if (!md_z80_busack)
            nk_fatal("68000 YM2612 write at $%06X without BUSREQ acknowledgement",
                     (unsigned)addr);
        md_ym_write((uint8_t)(addr - 0xA04000u), value, md_clock);
        return;
    }

    if (addr == 0xA06000u || addr == 0xA06001u ||
        (addr >= 0xA10000u && addr <= 0xA1001Fu))
        return; /* mapped writes; chip state belongs to later stages */

    if (md_is_vdp(addr)) {
        uint32_t port = md_vdp_port(addr);
        if (port <= 3u) {
            md_vdp_write8(port, value, md_clock);
            return;
        }
        if (port == 0x11u) {
            md_psg_write(value, md_clock);
            return;
        }
    }

    nk_fatal("unsupported 68000 byte write at $%06X", (unsigned)addr);
}

static void md_write16(uint32_t addr, uint16_t value)
{
    if (addr <= 0x3FFFFEu)
        nk_fatal("68000 word write to cartridge ROM at $%06X", (unsigned)addr);

    if (md_is_z80_ram(addr))
        nk_fatal("68000 word access to 8-bit Z80 RAM at $%06X", (unsigned)addr);

    if (md_is_work_ram(addr)) {
        uint32_t offset = addr & 0xFFFFu;
        work_ram[offset] = (uint8_t)(value >> 8);
        work_ram[(offset + 1u) & 0xFFFFu] = (uint8_t)value;
        return;
    }

    if (addr == 0xA11000u) {
        md_memory_mode = (uint8_t)((value >> 8) & 1u);
        return;
    }
    if (addr == 0xA11100u) {
        md_z80_busreq = (uint8_t)((value >> 8) & 1u);
        if (!md_z80_busreq)
            md_z80_busack = 0;
        return;
    }
    if (addr == 0xA11200u) {
        md_z80_reset = ((value >> 8) & 1u) == 0;
        return;
    }

    if (addr >= 0xA04000u && addr <= 0xA04002u) {
        if (!md_z80_busack)
            nk_fatal("68000 YM2612 write at $%06X without BUSREQ acknowledgement",
                     (unsigned)addr);
        md_ym_write((uint8_t)(addr - 0xA04000u), (uint8_t)(value >> 8), md_clock);
        return;
    }

    if (addr == 0xA06000u ||
        (addr >= 0xA10000u && addr <= 0xA1001Eu))
        return; /* mapped writes; chip state belongs to later stages */

    if (md_is_vdp(addr)) {
        uint32_t port = md_vdp_word_port(addr);
        if (port == MD_VDP_DATA_PORT || port == MD_VDP_CONTROL_PORT) {
            md_vdp_write16(port, value, md_clock);
            return;
        }
    }

    nk_fatal("unsupported 68000 word write at $%06X", (unsigned)addr);
}

int md_z80_window_bus_ready(void)
{
    return md_68k_bus_busy_until <= md_clock && !md_dma_bus_owned();
}

void md_z80_window_bus_reserve(void)
{
    if (!md_z80_window_bus_ready())
        nk_fatal("Z80 bank access reserved the 68000 bus while it was busy");
    md_68k_bus_busy_until = md_clock + MD_BUS_CYCLE_MASTER_CLOCKS;
}

uint8_t md_z80_window_read8(uint32_t addr)
{
    return md_read8(md_address(addr), 1);
}

void md_z80_window_write8(uint32_t addr, uint8_t value)
{
    md_write8(md_address(addr), value, 1);
}

void md_bus_reset(const nmd_cart *cart)
{
    active_cart = cart;
    memset(&nmd_cpu, 0, sizeof nmd_cpu);
    nmd_cpu.sr = 0x2700; /* reset starts in supervisor mode with all IRQs masked */
    memset(work_ram, 0, sizeof work_ram);
    memset(md_z80_ram, 0, sizeof md_z80_ram);
    md_z80_bus_reset();
    md_clock = 0;
    md_memory_mode = 0;
    md_z80_busreq = 1;
    md_z80_busack = 1;
    md_z80_reset = 1;
    md_68k_bus_busy_until = 0;
    md_irq_defer_depth = 0;
}

void md_irq_defer_begin(void)
{
    md_irq_defer_depth++;
}

void md_irq_defer_end(void)
{
    if (!md_irq_defer_depth)
        nk_fatal("unbalanced 68000 interrupt deferral");
    md_irq_defer_depth--;
    if (!md_irq_defer_depth && md_irq_check_hook)
        md_irq_check_hook();
}

uint8_t nmd_bus_read8(uint32_t addr)
{
    uint8_t value;
    uint64_t access_start;
    addr = md_address(addr);
    md_bus_begin_access();
    access_start = md_clock;
    value = md_read8(addr, 0);
    md_bus_cycle_complete(access_start);
    md_bus_trace(MD_BUS_TRACE_READ, 1, addr, value);
    md_bus_interrupt_check();
    return value;
}

uint16_t nmd_bus_read16(uint32_t addr)
{
    uint16_t value;
    uint64_t access_start;
    addr = md_address(addr);
    md_require_aligned(addr, "word read");
    md_bus_begin_access();
    access_start = md_clock;
    value = md_read16(addr);
    md_bus_cycle_complete(access_start);
    md_bus_trace(MD_BUS_TRACE_READ, 2, addr, value);
    md_bus_interrupt_check();
    return value;
}

void nmd_bus_write8(uint32_t addr, uint8_t value)
{
    uint64_t access_start;
    addr = md_address(addr);
    md_bus_begin_access();
    access_start = md_clock;
    md_write8(addr, value, 0);
    md_bus_cycle_complete(access_start);
    md_bus_trace(MD_BUS_TRACE_WRITE, 1, addr, value);
    md_bus_interrupt_check();
}

void nmd_bus_write16(uint32_t addr, uint16_t value)
{
    uint64_t access_start;
    addr = md_address(addr);
    md_require_aligned(addr, "word write");
    md_bus_begin_access();
    access_start = md_clock;
    md_write16(addr, value);
    md_bus_cycle_complete(access_start);
    md_bus_trace(MD_BUS_TRACE_WRITE, 2, addr, value);
    md_bus_interrupt_check();
}

uint8_t rd8(uint32_t addr)
{
    return nmd_bus_read8(addr);
}

uint16_t rd16(uint32_t addr)
{
    return nmd_bus_read16(addr);
}

uint32_t rd32(uint32_t addr)
{
    uint32_t value;
    addr = md_address(addr);
    md_require_aligned(addr, "longword read");
    md_irq_defer_begin();
    value = (uint32_t)nmd_bus_read16(addr) << 16;
    value |= nmd_bus_read16(addr + 2u);
    md_irq_defer_end();
    return value;
}

void wr8(uint32_t addr, uint8_t value)
{
    nmd_bus_write8(addr, value);
}

void wr16(uint32_t addr, uint16_t value)
{
    nmd_bus_write16(addr, value);
}

void wr32(uint32_t addr, uint32_t value)
{
    addr = md_address(addr);
    md_require_aligned(addr, "longword write");
    md_irq_defer_begin();
    nmd_bus_write16(addr, (uint16_t)(value >> 16));
    nmd_bus_write16(addr + 2u, (uint16_t)value);
    md_irq_defer_end();
}

uint8_t nmd_tas(uint32_t addr)
{
    uint8_t old_value;
    md_irq_defer_begin();
    old_value = nmd_bus_read8(addr);
    nmd_bus_write8(addr, (uint8_t)(old_value | 0x80u));
    nmd_cpu.sr = (uint16_t)((nmd_cpu.sr & ~0x000Fu) |
                            ((old_value & 0x80u) ? 0x0008u : 0) |
                            (old_value == 0 ? 0x0004u : 0));
    md_irq_defer_end();
    return old_value;
}
