/* Native C routines for the Mega Drive's Z80 sound processor. */
#include <stddef.h>
#include <string.h>

#include "md_internal.h"
#include "nk/coro.h"
#include "nk/fatal.h"
#include "nk/jump.h"

#define MD_MASTER_CLOCKS_PER_Z80_TSTATE 15u
#define MD_Z80_COROUTINE_STACK (1u << 20)

nmd_z80_state nmd_z80;

static const nmd_cart *active_cart;
static Coro *z80_co;
static Coro *main_co;
static port_jmp_buf z80_restart_buffer;
static uint64_t z80_cycles;
static uint64_t z80_target;
static uint16_t restart_addr;
static int z80_in_coroutine;
static int z80_started;
static int z80_reset_requested;
static int z80_power_reset_requested;
static unsigned z80_bus_call_depth;
static int z80_vint_pending;

static const nmd_z80_routine *find_routine(uint16_t addr)
{
    size_t low = 0;
    size_t high = active_cart ? active_cart->z80_routine_count : 0;

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        uint16_t entry = active_cart->z80_routines[mid].addr;
        if (entry == addr)
            return &active_cart->z80_routines[mid];
        if (entry < addr)
            low = mid + 1;
        else
            high = mid;
    }
    return NULL;
}

static uint16_t z80_vint_vector(void)
{
    uint16_t vector_address;

    switch (nmd_z80.im & 3u) {
    case 0:
    case 1:
        return 0x0038u;
    case 2:
        vector_address = (uint16_t)(((uint16_t)nmd_z80.i << 8) | 0x00FFu);
        return (uint16_t)(zrd(vector_address) |
            ((uint16_t)zrd((uint16_t)(vector_address + 1u)) << 8));
    default:
        nk_fatal("invalid Z80 interrupt mode %u", nmd_z80.im);
    }
}

static void deliver_vint(void)
{
    uint16_t vector;
    uint16_t saved_pc;
    uint8_t saved_iff1, saved_iff2;
    const nmd_z80_routine *routine;

    if (!z80_vint_pending || !nmd_z80.iff1 || z80_bus_call_depth ||
        md_z80_busreq || md_z80_reset)
        return;

    z80_vint_pending = 0;
    saved_pc = nmd_z80.pc;
    saved_iff1 = nmd_z80.iff1;
    saved_iff2 = nmd_z80.iff2;
    nmd_z80.iff1 = 0;
    nmd_z80.iff2 = 0;
    vector = z80_vint_vector();
    routine = find_routine(vector);
    if (!routine || !routine->abi)
        nk_fatal("Z80 VINT vector $%04X has no C routine", vector);
    nmd_z80.pc = vector;
    routine->abi();
    nmd_z80.pc = saved_pc;
    nmd_z80.iff1 = saved_iff1;
    nmd_z80.iff2 = saved_iff2;
}

static void reset_registers(void)
{
    nmd_z80.pc = 0;
    nmd_z80.i = 0;
    nmd_z80.r = 0;
    nmd_z80.iff1 = 0;
    nmd_z80.iff2 = 0;
    nmd_z80.im = 0;
}

static void yield_to_main(void)
{
    z80_in_coroutine = 0;
    coro_switch(z80_co, main_co);
    z80_in_coroutine = 1;
    if (z80_power_reset_requested)
        port_longjmp(z80_restart_buffer);
    if ((z80_reset_requested || md_z80_reset) && !z80_bus_call_depth)
        port_longjmp(z80_restart_buffer);
}

static void z80_tstate(void)
{
    for (;;) {
        if (z80_power_reset_requested)
            port_longjmp(z80_restart_buffer);
        if ((z80_reset_requested || md_z80_reset) && !z80_bus_call_depth)
            port_longjmp(z80_restart_buffer);
        if (md_z80_busreq && !z80_bus_call_depth) {
            yield_to_main();
            continue;
        }
        deliver_vint();
        if (z80_cycles >= z80_target) {
            yield_to_main();
            continue;
        }
        break;
    }
    z80_cycles++;
}

static void z80_main(void *arg)
{
    (void)arg;
    z80_in_coroutine = 1;
    for (;;) {
        if (port_setjmp(z80_restart_buffer)) {
            if (z80_reset_requested || md_z80_reset) {
                reset_registers();
                z80_cycles = md_clock / MD_MASTER_CLOCKS_PER_Z80_TSTATE;
                z80_started = 0;
                z80_reset_requested = 0;
                z80_power_reset_requested = 0;
                z80_bus_call_depth = 0;
            } else {
                nmd_z80.pc = restart_addr;
                z80_started = 0;
            }
        }

        if (md_z80_reset) {
            reset_registers();
            z80_started = 0;
            yield_to_main();
            continue;
        }
        if (md_z80_busreq) {
            yield_to_main();
            continue;
        }
        if (!active_cart || !active_cart->z80_routine_count) {
            /* A cartridge without Z80 C routines has no sound program to run. */
            z80_started = 0;
            yield_to_main();
            continue;
        }
        if (!z80_started) {
            const nmd_z80_routine *routine = find_routine(nmd_z80.pc);
            if (!routine || !routine->abi)
                nk_fatal("Z80 entry $%04X has no C routine", nmd_z80.pc);
            z80_started = 1;
            nmd_z80.pc = routine->addr;
            routine->abi();
            nk_fatal("Z80 routine at $%04X returned", nmd_z80.pc);
        }
    }
}

void md_z80_power(const nmd_cart *cart)
{
    size_t i;

    if (active_cart && active_cart->z80_routine_count && !active_cart->z80_routines)
        nk_fatal("Z80 routine count has no routine table");
    if (cart && cart->z80_routine_count && !cart->z80_routines)
        nk_fatal("Z80 routine count has no routine table");
    for (i = 1; cart && i < cart->z80_routine_count; i++) {
        if (cart->z80_routines[i - 1].addr >= cart->z80_routines[i].addr)
            nk_fatal("Z80 routine table is not sorted by unique entry address");
    }

    if (z80_co && z80_started) {
        main_co = coro_current();
        z80_power_reset_requested = 1;
        z80_reset_requested = 1;
        z80_in_coroutine = 0;
        coro_switch(main_co, z80_co);
    }

    active_cart = cart;
    memset(&nmd_z80, 0, sizeof nmd_z80);
    z80_cycles = md_clock / MD_MASTER_CLOCKS_PER_Z80_TSTATE;
    z80_target = z80_cycles;
    restart_addr = 0;
    z80_started = 0;
    z80_reset_requested = 0;
    z80_power_reset_requested = 0;
    z80_bus_call_depth = 0;
    z80_vint_pending = 0;
    if (!z80_co)
        z80_co = coro_new(z80_main, NULL, MD_Z80_COROUTINE_STACK);
}

void md_z80_sync_to(uint64_t master_clock)
{
    uint64_t target = master_clock / MD_MASTER_CLOCKS_PER_Z80_TSTATE;

    if (!z80_co || z80_in_coroutine)
        return;
    main_co = coro_current();

    if (md_z80_reset) {
        if (z80_started && z80_bus_call_depth) {
            z80_target = target;
            z80_in_coroutine = 0;
            coro_switch(main_co, z80_co);
        } else if (z80_started) {
            z80_reset_requested = 1;
            z80_in_coroutine = 0;
            coro_switch(main_co, z80_co);
        }
        if (!z80_bus_call_depth)
            md_z80_busack = md_z80_busreq;
        if (z80_cycles < target)
            z80_cycles = target;
        return;
    }
    if (md_z80_busreq) {
        if (z80_started && z80_bus_call_depth) {
            z80_target = target;
            z80_in_coroutine = 0;
            coro_switch(main_co, z80_co);
            md_z80_busack = (uint8_t)(z80_bus_call_depth == 0);
        } else {
            md_z80_busack = 1;
            if (z80_cycles < target)
                z80_cycles = target;
        }
        return;
    }
    md_z80_busack = 0;
    if (!active_cart || !active_cart->z80_routine_count) {
        if (z80_cycles < target)
            z80_cycles = target;
        return;
    }

    z80_target = target;
    if (z80_cycles <= z80_target) {
        z80_in_coroutine = 0;
        coro_switch(main_co, z80_co);
    }
}

uint64_t md_z80_next_event(void)
{
    if (!z80_co || z80_in_coroutine || md_z80_reset || md_z80_busreq ||
        !active_cart || !active_cart->z80_routine_count)
        return UINT64_MAX;
    if (z80_cycles >= UINT64_MAX / MD_MASTER_CLOCKS_PER_Z80_TSTATE - 1u)
        return UINT64_MAX;
    return (z80_cycles + 1u) * MD_MASTER_CLOCKS_PER_Z80_TSTATE;
}

void md_z80_request_vint(void)
{
    z80_vint_pending = 1;
}

uint64_t md_z80_cycle_count(void)
{
    return z80_cycles;
}

void md_z80_bus_access_begin(unsigned tstates)
{
    unsigned i;

    if (!z80_in_coroutine || !z80_started)
        nk_fatal("Z80 bus access outside a running Z80 routine");
    if (!tstates)
        nk_fatal("Z80 bus access must consume at least one T-state");
    z80_bus_call_depth++;
    for (i = 0; i < tstates; i++)
        z80_tstate();
}

void md_z80_bus_access_end(void)
{
    if (!z80_in_coroutine || !z80_started || !z80_bus_call_depth)
        nk_fatal("unbalanced Z80 bus access");
    z80_bus_call_depth--;
    if (!z80_bus_call_depth) {
        if (z80_reset_requested || md_z80_reset)
            port_longjmp(z80_restart_buffer);
        if (md_z80_busreq)
            yield_to_main();
        else
            deliver_vint();
    }
}

int md_z80_bus_call_active(void)
{
    return z80_bus_call_depth != 0;
}

void nmd_z80_restart(uint16_t addr)
{
    if (!z80_in_coroutine || !z80_started)
        nk_fatal("nmd_z80_restart outside a running Z80 routine");
    restart_addr = addr;
    z80_reset_requested = 0;
    port_longjmp(z80_restart_buffer);
}

uint64_t nmd_z80_wait_until(int (*ready)(void *ctx), void *ctx)
{
    uint64_t from;
    if (!z80_in_coroutine || !z80_started)
        nk_fatal("nmd_z80_wait_until outside a running Z80 routine");
    if (!ready)
        nk_fatal("nmd_z80_wait_until requires a ready predicate");
    from = z80_cycles;
    while (!ready(ctx))
        z80_tstate();
    return z80_cycles - from;
}
