/*
 * dispatch.c: shared master-clock scheduling, 68000 interrupt dispatch, and
 * game-facing wait operations.
 */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "md_internal.h"
#include "nk/fatal.h"
#include "nk/routines.h"

#define MD_IRQ_QUEUE_CAPACITY 64u
#define MD_NEVER UINT64_MAX

typedef struct md_irq_event {
    uint64_t clock;
    uint8_t level;
    uint8_t external;
} md_irq_event;

static const nmd_cart *active_cart;
static md_irq_event irq_events[MD_IRQ_QUEUE_CAPACITY];
static unsigned irq_event_count;
static uint8_t irq_pending;
static int ext_asserted;
static int irq_delivering;

uint64_t md_irq_taken;
uint64_t md_irq_last_service_clock;

static uint32_t vector_for_level(int level)
{
    switch (level) {
    case 2: return 0x68;
    case 4: return 0x70;
    case 6: return 0x78;
    default: nk_fatal("unsupported interrupt level %d", level);
    }
}

static unsigned interrupt_mask(void)
{
    return (unsigned)((nmd_cpu.sr >> 8) & 7u);
}

static int next_eligible_level(void)
{
    static const unsigned levels[] = { 6, 4, 2 };
    unsigned mask = interrupt_mask();
    unsigned i;
    for (i = 0; i < sizeof levels / sizeof levels[0]; i++) {
        unsigned level = levels[i];
        if ((irq_pending & (1u << level)) && level > mask)
            return (int)level;
    }
    return 0;
}

static const nmd_routine *find_routine(uint32_t addr)
{
    if (!active_cart || !active_cart->routines || !active_cart->routine_count)
        return NULL;
    if (active_cart->routine_count > INT_MAX)
        nk_fatal("the cartridge has too many 68000 routines");
    return (const nmd_routine *)nk_routine_find(active_cart->routines,
        (int)active_cart->routine_count, sizeof *active_cart->routines, addr & 0xFFFFFFu);
}

void md_irq_request(int level)
{
    if (level != 2 && level != 4 && level != 6)
        nk_fatal("unsupported interrupt request level %d", level);
    irq_pending |= (uint8_t)(1u << level);
}

static void queue_event(uint8_t level, uint64_t clock, int external)
{
    unsigned at;
    if (clock < md_clock)
        nk_fatal("interrupt event at $%llX precedes the 68000 clock $%llX",
                 (unsigned long long)clock, (unsigned long long)md_clock);
    if (irq_event_count == MD_IRQ_QUEUE_CAPACITY)
        nk_fatal("too many pending interrupt events");
    at = irq_event_count;
    while (at && irq_events[at - 1].clock > clock) {
        irq_events[at] = irq_events[at - 1];
        at--;
    }
    irq_events[at].clock = clock;
    irq_events[at].level = level;
    irq_events[at].external = (uint8_t)(external != 0);
    irq_event_count++;
}

void md_irq_request_at(int level, uint64_t clock)
{
    if (level != 2 && level != 4 && level != 6)
        nk_fatal("unsupported scheduled interrupt level %d", level);
    queue_event((uint8_t)level, clock, 0);
}

void md_ext_irq_at(int level, uint64_t clock)
{
    if (level != 0 && level != 2)
        nk_fatal("external interrupt line level must be 0 or 2");
    queue_event((uint8_t)level, clock, 1);
}

static uint64_t next_event_clock(void)
{
    uint64_t next_vdp = md_vdp_next_event();
    uint64_t next_irq = irq_event_count ? irq_events[0].clock : MD_NEVER;
    return next_vdp < next_irq ? next_vdp : next_irq;
}

static void process_irq_event(const md_irq_event *event)
{
    if (!event->external) {
        md_irq_request(event->level);
    } else if (!event->level) {
        ext_asserted = 0;
    } else if (!ext_asserted) {
        ext_asserted = 1;
        md_irq_request(2);
    }
}

void md_advance_to(uint64_t to_clock)
{
    if (to_clock < md_clock)
        nk_fatal("the 68000 clock cannot move backward ($%llX to $%llX)",
                 (unsigned long long)md_clock, (unsigned long long)to_clock);

    for (;;) {
        uint64_t next = next_event_clock();
        if (next > to_clock)
            break;
        if (next < md_clock)
            nk_fatal("a scheduled hardware event precedes the 68000 clock");

        md_clock = next;
        if (md_vdp_next_event() == next)
            md_vdp_advance(next);
        while (irq_event_count && irq_events[0].clock == next) {
            md_irq_event event = irq_events[0];
            unsigned i;
            for (i = 1; i < irq_event_count; i++)
                irq_events[i - 1] = irq_events[i];
            irq_event_count--;
            process_irq_event(&event);
        }
    }
    md_clock = to_clock;
}

static void take_interrupt(int level)
{
    nmd_cpu_state saved = nmd_cpu;
    const nmd_routine *routine;
    uint32_t vector;
    uint32_t handler;

    irq_pending &= (uint8_t)~(1u << level);
    irq_delivering = 1;
    md_irq_last_service_clock = md_clock;

    md_irq_defer_begin();
    nmd_cpu.sr = (uint16_t)((saved.sr & 0x00FFu) | 0x2000u | ((unsigned)level << 8));
    nmd_cpu.a[7] = saved.a[7] - 4u;
    wr32(nmd_cpu.a[7], saved.pc);
    nmd_cpu.a[7] -= 2u;
    wr16(nmd_cpu.a[7], saved.sr);

    vector = vector_for_level(level);
    handler = rd32(vector);
    routine = find_routine(handler);
    if (!routine || !routine->abi)
        nk_fatal("interrupt vector $%02X points to $%06X with no C routine",
                 (unsigned)vector, (unsigned)(handler & 0xFFFFFFu));
    nmd_cpu.pc = handler & 0xFFFFFFu;
    md_irq_defer_end();

    routine->abi();
    nmd_cpu = saved;
    irq_delivering = 0;
    md_irq_taken++;
}

void md_irq_dispatch_pending(void)
{
    int level;
    if (irq_delivering || md_irq_defer_depth)
        return;
    while ((level = next_eligible_level()) != 0)
        take_interrupt(level);
}

void md_timing_reset(const nmd_cart *cart)
{
    active_cart = cart;
    irq_event_count = 0;
    irq_pending = 0;
    ext_asserted = 0;
    irq_delivering = 0;
    md_irq_taken = 0;
    md_irq_last_service_clock = 0;
    md_vdp_reset(cart);
    md_clock_hook = md_advance_to;
    md_irq_check_hook = md_irq_dispatch_pending;
}

uint64_t nmd_wait_until(int (*ready)(void *ctx), void *ctx)
{
    uint64_t from = md_clock;
    if (!ready)
        nk_fatal("nmd_wait_until requires a ready predicate");
    if (md_irq_defer_depth)
        nk_fatal("nmd_wait_until inside an indivisible 68000 operation");

    for (;;) {
        uint64_t taken = md_irq_taken;
        if (ready(ctx))
            break;
        if (md_irq_taken != taken)
            continue;
        md_advance_to(next_event_clock());
        md_irq_dispatch_pending();
    }
    return md_clock - from;
}

static int vblank_changed(void *ctx)
{
    return md_vdp_vblank_count() != *(const uint64_t *)ctx;
}

void nmd_wait_vblank(void)
{
    uint64_t before = md_vdp_vblank_count();
    (void)nmd_wait_until(vblank_changed, &before);
}

void nmd_stop(void)
{
    uint64_t taken;
    if (md_irq_defer_depth)
        nk_fatal("nmd_stop inside an indivisible 68000 operation");
    taken = md_irq_taken;
    md_irq_dispatch_pending();
    while (md_irq_taken == taken) {
        md_advance_to(next_event_clock());
        md_irq_dispatch_pending();
    }
}
