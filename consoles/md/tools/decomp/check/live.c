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
 *
 * Each call's C runs on its own stack (a coroutine), so it can pause where
 * the original was interrupted or the frame ended: where a draft reaches that
 * instruction with the original's registers, else before its first hardware
 * access after the point, or at its end. The runner lets the machine take the
 * interrupt and continues the C when the handler returns. The original's
 * syncs between instructions are made at their clocks, in time order with
 * the pauses.
 */
#define _XOPEN_SOURCE 700
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

#include "nmd/machine.h"
#include "nmd/z80.h"
#include "live.h"

nmd_cpu_state nmd_cpu;
nmd_z80_state nmd_z80;

#define UNTIMED UINT64_MAX
#define MAX_CALLS 32
#define STACK_BYTES (1u << 20)

enum { RUNNING, PAUSED, FINISHED, FAILED };

typedef struct call {
    int used, cpu, state, at_end;
    const nmd_live_slot *slots;
    uint32_t slot_count, slot_next;
    const nmd_live_sync_point *syncs;
    uint32_t sync_count, sync_next;
    const nmd_live_pause *pauses;
    uint32_t pause_count, pause_next;
    nmd_live_result result;
    void (*routine)(void);
    ucontext_t self, caller;
    char *stack;
} call;

static nmd_live_m68k_bus m68k_bus;
static nmd_live_z80_bus z80_bus;
static nmd_live_sync sync_fn;
static uint32_t rom_size;
static call calls[MAX_CALLS];
static call *current[2];        /* the call whose C is running, per CPU */
static call *starting;          /* the call the trampoline enters */

void nmd_live_bind(nmd_live_m68k_bus m68k, nmd_live_z80_bus z80, nmd_live_sync sync,
                   uint32_t image_size)
{
    m68k_bus = m68k;
    z80_bus = z80;
    sync_fn = sync;
    rom_size = image_size;
}

static void leave(call *c, int state)
{
    c->state = state;
    swapcontext(&c->self, &c->caller);
}

static void fail(call *c, int status, uint32_t address, uint32_t width, int write)
{
    c->result.status = status;
    c->result.address = address;
    c->result.width = width;
    c->result.write = write;
    c->result.slot = c->slot_next;
    leave(c, FAILED);           /* never continued */
    abort();
}

/* The C is where the original paused only once it has made every hardware
 * access the original made before the pause: in a polling loop the registers
 * repeat, and an earlier pass would match them. */
static int accesses_done(const call *c, const nmd_live_pause *pause)
{
    return c->slot_next >= c->slot_count || c->slots[c->slot_next].clock >= pause->clock;
}

/* Make the syncs the original made before the next pause. */
static void syncs_to(call *c, const nmd_live_pause *pause)
{
    while (c->sync_next < c->sync_count && c->sync_next < pause->syncs_before) {
        const nmd_live_sync_point *point = &c->syncs[c->sync_next++];

        sync_fn(point->clock, point->refresh_counter, point->refresh_sync);
    }
}

/* Make the syncs at or before `clock` and pause at the interrupts before it
 * (all remaining ones with `all`), in the original's order. */
static void reach(call *c, uint64_t clock, int all)
{
    for (;;) {
        int have_sync = c->sync_next < c->sync_count && c->syncs[c->sync_next].clock <= clock;
        int have_pause = c->pause_next < c->pause_count &&
                         (all || c->pauses[c->pause_next].clock < clock);

        if (have_sync && (!have_pause ||
                          c->sync_next < c->pauses[c->pause_next].syncs_before)) {
            const nmd_live_sync_point *point = &c->syncs[c->sync_next++];

            sync_fn(point->clock, point->refresh_counter, point->refresh_sync);
        } else if (have_pause && c->pauses[c->pause_next].kind == NMD_LIVE_PAUSE_EXCEPTION) {
            fail(c, NMD_LIVE_EXCEPTION, c->pauses[c->pause_next].pc, 0, 0);
        } else if (have_pause) {
            c->pause_next++;
            leave(c, PAUSED);
        } else {
            return;
        }
    }
}

/* A draft starts the original instruction at `address`: pause here when it is
 * where the original was interrupted and the registers are the original's
 * there (the syncs before the pause first). */
void nmd_live_at(uint32_t address)
{
    call *c = current[0];
    const nmd_live_pause *pause;
    int index;

    /* the original can stop twice before one instruction: the frame ends, then an
     * interrupt is taken as the next frame begins */
    while (c && c->pause_next < c->pause_count) {
        pause = &c->pauses[c->pause_next];
        if (pause->kind != NMD_LIVE_PAUSE_INTERRUPT || pause->pc != address ||
            (nmd_cpu.sr & 0x1F) != pause->ccr || !accesses_done(c, pause))
            return;
        for (index = 0; index < 8; index++)
            if (nmd_cpu.d[index] != pause->d[index] || nmd_cpu.a[index] != pause->a[index])
                return;
        syncs_to(c, pause);
        c->pause_next++;
        leave(c, PAUSED);
    }
}

/* The C takes the exception at `address` (a TRAP): when the original took it
 * there, stop (the syncs before it first); the machine runs the instruction
 * and its handler, and the C goes on after it. 0 when the original took none
 * here. */
int nmd_live_exception(uint32_t address)
{
    call *c = current[0];
    const nmd_live_pause *pause;

    if (!c || c->pause_next >= c->pause_count)
        return 0;
    pause = &c->pauses[c->pause_next];
    if (pause->kind != NMD_LIVE_PAUSE_EXCEPTION || pause->pc != address ||
        !accesses_done(c, pause))
        return 0;
    syncs_to(c, pause);
    c->pause_next++;
    leave(c, PAUSED);
    return 1;
}

/* The Z80 C starts the original instruction at `address`: pause here when the
 * original's run slice ended before it, with these registers, once the C has
 * made the accesses the original made before. */
void nmd_live_zat(uint16_t address)
{
    call *c = current[1];

    while (c && c->pause_next < c->pause_count) {
        const nmd_live_pause *pause = &c->pauses[c->pause_next];
        const uint8_t main[8] = {nmd_z80.a, nmd_z80.f, nmd_z80.b, nmd_z80.c, nmd_z80.d,
                                 nmd_z80.e, nmd_z80.h, nmd_z80.l};
        const uint8_t alt[8] = {nmd_z80.a2, nmd_z80.f2, nmd_z80.b2, nmd_z80.c2, nmd_z80.d2,
                                nmd_z80.e2, nmd_z80.h2, nmd_z80.l2};
        int index;

        if (pause->pc != address || pause->index[0] != nmd_z80.ix ||
            pause->index[1] != nmd_z80.iy || pause->index[2] != nmd_z80.sp ||
            !accesses_done(c, pause))
            return;
        for (index = 0; index < 8; index++)
            if (pause->d[index] != main[index] || pause->a[index] != alt[index])
                return;
        c->pause_next++;
        leave(c, PAUSED);
    }
}

static void trampoline(void)
{
    call *c = starting;

    c->routine();
    c->at_end = 1;
    reach(c, UNTIMED, 1);       /* the syncs and pauses after the last hardware access */
    leave(c, FINISHED);
}

static int switch_in(call *c)
{
    call *outer = current[c->cpu - 1];

    current[c->cpu - 1] = c;
    c->state = RUNNING;
    swapcontext(&c->caller, &c->self);
    current[c->cpu - 1] = outer;
    return c->state;
}

/* 0 finished, 1 paused, 2 a difference (see the result), -1 no room. */
int nmd_live_start(int cpu, const nmd_live_slot *slots, uint32_t slot_count,
                   const nmd_live_sync_point *syncs, uint32_t sync_count,
                   const nmd_live_pause *pauses, uint32_t pause_count, void (*routine)(void),
                   int *handle)
{
    int index, state;
    call *c;

    for (index = 0; index < MAX_CALLS && calls[index].used; index++) {
    }
    if (index == MAX_CALLS)
        return -1;
    c = &calls[index];
    if (!c->stack && !(c->stack = malloc(STACK_BYTES)))
        return -1;
    c->used = 1;
    c->cpu = cpu;
    c->slots = slots;
    c->slot_count = slot_count;
    c->slot_next = 0;
    c->syncs = syncs;
    c->sync_count = sync_count;
    c->sync_next = 0;
    c->pauses = pauses;
    c->pause_count = pause_count;
    c->pause_next = 0;
    c->routine = routine;
    c->at_end = 0;
    memset(&c->result, 0, sizeof c->result);
    getcontext(&c->self);
    c->self.uc_stack.ss_sp = c->stack;
    c->self.uc_stack.ss_size = STACK_BYTES;
    c->self.uc_link = NULL;
    makecontext(&c->self, trampoline, 0);
    *handle = index;
    starting = c;
    state = switch_in(c);
    return state == FINISHED ? 0 : state == PAUSED ? 1 : 2;
}

int nmd_live_continue(int handle)
{
    int state = switch_in(&calls[handle]);

    return state == FINISHED ? 0 : state == PAUSED ? 1 : 2;
}

const nmd_live_result *nmd_live_result_of(int handle)
{
    call *c = &calls[handle];

    c->result.slots_used = c->slot_next;
    return &c->result;
}

uint32_t nmd_live_syncs_made(int handle)
{
    return calls[handle].sync_next;
}

int nmd_live_at_end(int handle)
{
    return calls[handle].at_end;
}

uint32_t nmd_live_pauses_made(int handle)
{
    return calls[handle].pause_next;
}

void nmd_live_release(int handle)
{
    calls[handle].used = 0;
}

static const nmd_live_slot *timed(call *c, uint32_t address, uint32_t width, int write)
{
    const nmd_live_slot *slot;

    if (c->slot_next >= c->slot_count)
        fail(c, NMD_LIVE_PAST_RECORD, address, width, write);
    slot = &c->slots[c->slot_next];
    if (slot->address != address || slot->width != width || slot->write != write)
        fail(c, NMD_LIVE_ORDER, address, width, write);
    reach(c, slot->clock, 0);
    c->slot_next++;
    c->result.hardware++;
    return slot;
}

/* 68000 */

static int m68k_memory(uint32_t address)
{
    return address < rom_size || address >= 0xE00000u;
}

static uint32_t m68k_access(uint32_t address, uint32_t width, int write, uint32_t value)
{
    call *c = current[0];

    address &= 0xFFFFFFu;
    c->result.accesses++;
    if (width == 2 && (address & 1))
        fail(c, NMD_LIVE_ODD, address, width, write);
    if (m68k_memory(address))
        return m68k_bus(address, width, (uint32_t)write, value, UNTIMED, 0, 0, 0);
    else {
        const nmd_live_slot *slot = timed(c, address, width, write);

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

static uint64_t wait_until(call *c, int (*ready)(void *ctx), void *ctx)
{
    uint64_t polls = 0;

    while (!ready(ctx))
        if (++polls > 100000000u)
            fail(c, NMD_LIVE_UNSUPPORTED, 0, 0, 0);
    return polls;
}

uint64_t nmd_wait_until(int (*ready)(void *ctx), void *ctx)
{
    return wait_until(current[0], ready, ctx);
}

void nmd_wait_vblank(void) { fail(current[0], NMD_LIVE_UNSUPPORTED, 0, 0, 0); }
void nmd_stop(void) { fail(current[0], NMD_LIVE_UNSUPPORTED, 0, 0, 0); }

void nmd_restart(uint32_t addr)
{
    fail(current[0], NMD_LIVE_UNSUPPORTED, addr, 0, 0);
    for (;;) {
    }
}

/* Z80 */

uint8_t zrd(uint16_t addr)
{
    current[1]->result.accesses++;
    return (uint8_t)z80_bus(addr, 0, 0, addr < 0x2000 ? UNTIMED : timed(current[1], addr, 1, 0)->clock);
}

void zwr(uint16_t addr, uint8_t value)
{
    current[1]->result.accesses++;
    z80_bus(addr, 1, value, addr < 0x2000 ? UNTIMED : timed(current[1], addr, 1, 1)->clock);
}

uint8_t zin(uint8_t port) { fail(current[1], NMD_LIVE_UNSUPPORTED, port, 1, 0); return 0; }
void zout(uint8_t port, uint8_t value) { (void)value; fail(current[1], NMD_LIVE_UNSUPPORTED, port, 1, 1); }
void nmd_z80_restart(uint16_t addr) { fail(current[1], NMD_LIVE_UNSUPPORTED, addr, 0, 0); }

uint64_t nmd_z80_wait_until(int (*ready)(void *ctx), void *ctx)
{
    return wait_until(current[1], ready, ctx);
}

/* a routine with no C cannot be stood in for on a live machine: the runner
 * leaves such calls to the original before the C is entered */
void nmd_check_call_fallback(int cpu, uint32_t address)
{
    fail(current[cpu == 2], NMD_LIVE_UNSUPPORTED, address, 0, 0);
}

void __cyg_profile_func_enter(void *function, void *site) { (void)function; (void)site; }
void __cyg_profile_func_exit(void *function, void *site) { (void)function; (void)site; }
