/*
 * live.c: the game-facing nmd bus and CPU state, served by the reference
 * core's live machine (D05 lockstep). Test-only: linked with a game's C in
 * place of the nmd runtime, never into a game.
 *
 * ROM and RAM accesses go to the machine at once, without moving its clock.
 * Every other access is hardware: it must be the next one of the original's
 * recorded call (same address, width and direction) and is made on the real
 * machine starting at the clock the original's started at. Nothing is
 * played back: values read and written are the live machine's and the C's.
 */
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "nmd/machine.h"
#include "nmd/z80.h"
#include "live.h"

nmd_cpu_state nmd_cpu;
nmd_z80_state nmd_z80;

#define UNTIMED UINT64_MAX

static nmd_live_m68k_bus m68k_bus;
static nmd_live_sync sync_fn;
static const nmd_live_sync_point *sync_points;
static uint32_t sync_count, sync_next;
static nmd_live_z80_bus z80_bus;
static uint32_t rom_size;
/* One call under way for each CPU: a 68000 hardware access lets the Z80
 * run, and a Z80 routine can be substituted inside it. */
static struct call {
    const nmd_live_slot *slots;
    uint32_t slot_count, slot_next;
    nmd_live_result result;
    jmp_buf stop;
    int running;
} calls[2];
#define M68K (&calls[0])
#define Z80 (&calls[1])

static void fail(struct call *call, int status, uint32_t address, uint32_t width, int write)
{
    call->result.status = status;
    call->result.address = address;
    call->result.width = width;
    call->result.write = write;
    call->result.slot = call->slot_next;
    if (call->running)
        longjmp(call->stop, 1);
}

void nmd_live_bind(nmd_live_m68k_bus m68k, nmd_live_z80_bus z80, uint32_t image_size)
{
    m68k_bus = m68k;
    z80_bus = z80;
    rom_size = image_size;
}

void nmd_live_syncs(nmd_live_sync sync, const nmd_live_sync_point *points, uint32_t count)
{
    sync_fn = sync;
    sync_points = points;
    sync_count = count;
    sync_next = 0;
}

uint32_t nmd_live_syncs_made(void)
{
    return sync_next;
}

void nmd_live_begin(int cpu, const nmd_live_slot *list, uint32_t count)
{
    struct call *call = &calls[cpu - 1];

    call->slots = list;
    call->slot_count = count;
    call->slot_next = 0;
    memset(&call->result, 0, sizeof call->result);
}

const nmd_live_result *nmd_live_run(int cpu, void (*routine)(void))
{
    struct call *call = &calls[cpu - 1];

    call->running = 1;
    if (!setjmp(call->stop))
        routine();
    call->running = 0;
    call->result.slots_used = call->slot_next;
    return &call->result;
}

static const nmd_live_slot *timed(struct call *call, uint32_t address, uint32_t width, int write)
{
    const nmd_live_slot *slot;

    if (call->slot_next >= call->slot_count)
        fail(call, NMD_LIVE_PAST_RECORD, address, width, write);
    slot = &call->slots[call->slot_next];
    if (slot->address != address || slot->width != width || slot->write != write)
        fail(call, NMD_LIVE_ORDER, address, width, write);
    call->slot_next++;
    call->result.hardware++;
    return slot;
}

/* 68000 */

static int m68k_memory(uint32_t address)
{
    return address < rom_size || address >= 0xE00000u;
}

static uint32_t m68k_access(uint32_t address, uint32_t width, int write, uint32_t value)
{
    address &= 0xFFFFFFu;
    M68K->result.accesses++;
    if (width == 2 && (address & 1))
        fail(M68K, NMD_LIVE_ODD, address, width, write);
    if (m68k_memory(address))
        return m68k_bus(address, width, (uint32_t)write, value, UNTIMED, 0, 0, 0);
    else {
        const nmd_live_slot *slot = timed(M68K, address, width, write);

        while (sync_next < sync_count && sync_points[sync_next].clock <= slot->clock) {
            const nmd_live_sync_point *point = &sync_points[sync_next++];

            sync_fn(point->clock, point->refresh_counter, point->refresh_sync);
        }

        return m68k_bus(address, width, (uint32_t)write, value, slot->clock,
                        slot->refresh_counter, slot->refresh_sync, slot->prefetch);
    }
}

uint8_t rd8(uint32_t addr) { return (uint8_t)m68k_access(addr, 1, 0, 0); }
uint16_t rd16(uint32_t addr) { return (uint16_t)m68k_access(addr, 2, 0, 0); }
void wr8(uint32_t addr, uint8_t value) { m68k_access(addr, 1, 1, value); }
void wr16(uint32_t addr, uint16_t value) { m68k_access(addr, 2, 1, value); }

uint32_t rd32(uint32_t addr)
{
    uint32_t high = rd16(addr);

    return high << 16 | rd16(addr + 2);
}

void wr32(uint32_t addr, uint32_t value)
{
    wr16(addr, (uint16_t)(value >> 16));
    wr16(addr + 2, (uint16_t)value);
}

uint8_t nmd_tas(uint32_t addr)
{
    uint8_t value = rd8(addr);

    wr8(addr, value | 0x80);
    return value;
}

void nmd_set_sr(uint16_t sr)
{
    if ((sr ^ nmd_cpu.sr) & 0x2000) {
        uint32_t other = (sr & 0x2000) ? nmd_cpu.ssp : nmd_cpu.usp;

        if (sr & 0x2000)
            nmd_cpu.usp = nmd_cpu.a[7];
        else
            nmd_cpu.ssp = nmd_cpu.a[7];
        nmd_cpu.a[7] = other;
    }
    nmd_cpu.sr = sr;
}

static uint64_t wait_until(struct call *call, int (*ready)(void *ctx), void *ctx)
{
    uint64_t polls = 0;

    while (!ready(ctx))
        if (++polls > 100000000u)
            fail(call, NMD_LIVE_UNSUPPORTED, 0, 0, 0);
    return polls;
}

uint64_t nmd_wait_until(int (*ready)(void *ctx), void *ctx)
{
    return wait_until(M68K, ready, ctx);
}

void nmd_wait_vblank(void) { fail(M68K, NMD_LIVE_UNSUPPORTED, 0, 0, 0); }
void nmd_stop(void) { fail(M68K, NMD_LIVE_UNSUPPORTED, 0, 0, 0); }

void nmd_restart(uint32_t addr)
{
    fail(M68K, NMD_LIVE_UNSUPPORTED, addr, 0, 0);
    for (;;) {
    }
}

/* Z80 */

uint8_t zrd(uint16_t addr)
{
    Z80->result.accesses++;
    return (uint8_t)z80_bus(addr, 0, 0, addr < 0x2000 ? UNTIMED : timed(Z80, addr, 1, 0)->clock);
}

void zwr(uint16_t addr, uint8_t value)
{
    Z80->result.accesses++;
    z80_bus(addr, 1, value, addr < 0x2000 ? UNTIMED : timed(Z80, addr, 1, 1)->clock);
}

uint8_t zin(uint8_t port) { fail(Z80, NMD_LIVE_UNSUPPORTED, port, 1, 0); return 0; }
void zout(uint8_t port, uint8_t value) { (void)value; fail(Z80, NMD_LIVE_UNSUPPORTED, port, 1, 1); }
void nmd_z80_restart(uint16_t addr) { fail(Z80, NMD_LIVE_UNSUPPORTED, addr, 0, 0); }

uint64_t nmd_z80_wait_until(int (*ready)(void *ctx), void *ctx)
{
    return wait_until(Z80, ready, ctx);
}

/* a routine with no C cannot be stood in for on a live machine: the runner
 * leaves such calls to the original before the C is entered */
void nmd_check_call_fallback(int cpu, uint32_t address)
{
    fail(&calls[cpu == 2], NMD_LIVE_UNSUPPORTED, address, 0, 0);
}

void __cyg_profile_func_enter(void *function, void *site) { (void)function; (void)site; }
void __cyg_profile_func_exit(void *function, void *site) { (void)function; (void)site; }
