/*
 * dispatch.c: shared master-clock scheduling, 68000 interrupt dispatch,
 * game-facing wait operations, and the game's 68000 run.
 *
 * A cartridge with NMD_EXECUTION_NATIVE has its reset vector's C routine run
 * on a coroutine of its own: nmd_run_frame switches to it, and the first
 * point at which the 68000 could take an interrupt at or after the vertical
 * blank's start switches back. Nothing here executes 68000 code: the routine
 * is the game's C, and time passes only in its bus accesses and waits.
 */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "md_internal.h"
#include "nk/coro.h"
#include "nk/fatal.h"
#include "nk/jump.h"
#include "nk/routines.h"
#include "../sound/sound.h"

#define MD_IRQ_QUEUE_CAPACITY 64u
#define MD_NEVER UINT64_MAX
#define MD_M68K_COROUTINE_STACK (16u << 20)

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

static Coro *m68k_co;
static Coro *m68k_host_co;
static port_jmp_buf m68k_top;
static void (*m68k_entry)(void);
static uint32_t m68k_entry_addr;
static uint64_t m68k_frame_target;
static int m68k_live;
static int m68k_abandon_requested;

#ifdef NMD_TESTING
/* Errors a check plants to show it can fail (tests/unit/execution_check.c):
 * 1 the reset entry skipped, 2 the run's state lost across a frame's yield,
 * 3 an interrupt return that ignores the guest stack. Test builds only. */
int md_m68k_plant;
#define M68K_PLANTED(n) (md_m68k_plant == (n))
#else
#define M68K_PLANTED(n) 0
#endif

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

int md_m68k_native(void)
{
    return active_cart && active_cart->execution == NMD_EXECUTION_NATIVE;
}

static int on_m68k_coroutine(void)
{
    return m68k_co && coro_current() == m68k_co;
}

static void m68k_to_host(void)
{
    coro_switch(m68k_co, m68k_host_co);
    if (m68k_abandon_requested)
        port_longjmp(m68k_top);
    if (M68K_PLANTED(2)) {
        m68k_entry = find_routine(m68k_entry_addr)->abi;
        port_longjmp(m68k_top);
    }
}

/* The frame's end, where the 68000 could take an interrupt. */
static void m68k_frame_yield(void)
{
    if (on_m68k_coroutine() && md_clock >= m68k_frame_target)
        m68k_to_host();
}

static void m68k_main(void *arg)
{
    (void)arg;
    for (;;) {
        port_setjmp(m68k_top);
        if (m68k_abandon_requested || !m68k_entry) {
            /* A power-on dropped the run: wait for the next one's entry. */
            m68k_abandon_requested = 0;
            m68k_live = 0;
            coro_switch(m68k_co, m68k_host_co);
            continue;
        }
        {
            void (*entry)(void) = m68k_entry;
            m68k_entry = NULL;
            m68k_live = 1;
            if (M68K_PLANTED(1)) {
                for (;;)
                    nmd_wait_vblank();
            }
            entry();
        }
        nk_fatal("the 68000 routine at $%06X returned to no caller",
                 (unsigned)m68k_entry_addr);
    }
}

void md_m68k_abandon(void)
{
    if (on_m68k_coroutine())
        nk_fatal("nmd_power from the game's 68000 routine");
    m68k_entry = NULL;
    if (m68k_co && m68k_live) {
        m68k_abandon_requested = 1;
        m68k_host_co = coro_current();
        coro_switch(m68k_host_co, m68k_co);
    }
}

static void m68k_load_reset(const nmd_cart *cart)
{
    const nmd_routine *routine;
    uint32_t ssp, pc;

    if (!cart->rom || cart->rom_size < 8u)
        nk_fatal("native execution needs a ROM with reset vectors");
    ssp = (uint32_t)cart->rom[0] << 24 | (uint32_t)cart->rom[1] << 16 |
          (uint32_t)cart->rom[2] << 8 | cart->rom[3];
    pc = ((uint32_t)cart->rom[4] << 24 | (uint32_t)cart->rom[5] << 16 |
          (uint32_t)cart->rom[6] << 8 | cart->rom[7]) & 0xFFFFFFu;
    routine = find_routine(pc);
    if (!routine || !routine->abi)
        nk_fatal("reset vector $%06X has no C routine", (unsigned)pc);
    nmd_cpu.a[7] = ssp;
    nmd_cpu.ssp = ssp;
    nmd_cpu.pc = pc;
    m68k_entry = routine->abi;
    m68k_entry_addr = pc;
    m68k_frame_target = 0;
    if (!m68k_co)
        m68k_co = coro_new(m68k_main, NULL, MD_M68K_COROUTINE_STACK);
}

void md_m68k_run_frame(void)
{
    if (on_m68k_coroutine())
        nk_fatal("nmd_run_frame from the game's 68000 routine");
    m68k_frame_target = md_vdp_next_vblank_clock();
    m68k_host_co = coro_current();
    coro_switch(m68k_host_co, m68k_co);
}

void nmd_restart(uint32_t addr)
{
    const nmd_routine *routine;

    addr &= 0xFFFFFFu;
    if (!md_m68k_native() || !on_m68k_coroutine())
        nk_fatal("nmd_restart outside the game's 68000 routine");
    if (md_irq_defer_depth)
        nk_fatal("nmd_restart inside an indivisible 68000 operation");
    routine = find_routine(addr);
    if (!routine || !routine->abi)
        nk_fatal("nmd_restart to $%06X with no C routine", (unsigned)addr);
    irq_delivering = 0;
    nmd_cpu.pc = addr;
    m68k_entry = routine->abi;
    m68k_entry_addr = addr;
    port_longjmp(m68k_top);
}

/* The status register as the 68000 loads it: a change of the S bit changes
 * which stack pointer A7 is. */
static void load_sr(uint16_t sr)
{
    if ((nmd_cpu.sr ^ sr) & 0x2000u) {
        if (sr & 0x2000u) {
            nmd_cpu.usp = nmd_cpu.a[7];
            nmd_cpu.a[7] = nmd_cpu.ssp;
        } else {
            nmd_cpu.ssp = nmd_cpu.a[7];
            nmd_cpu.a[7] = nmd_cpu.usp;
        }
    }
    nmd_cpu.sr = sr;
}

void nmd_set_sr(uint16_t sr)
{
    load_sr(sr);
    if (!md_irq_defer_depth)
        md_irq_dispatch_pending();
}

void md_irq_request(int level)
{
    if (level != 2 && level != 4 && level != 6)
        nk_fatal("unsupported interrupt request level %d", level);
    irq_pending |= (uint8_t)(1u << level);
}

void md_irq_clear(int level)
{
    if (level != 2 && level != 4 && level != 6)
        nk_fatal("unsupported interrupt clear level %d", level);
    irq_pending &= (uint8_t)~(1u << level);
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
    uint64_t next_dma = md_dma_next_event();
    uint64_t next_ym = md_ym_next_event();
    uint64_t next_z80 = md_z80_next_event();
    uint64_t next_irq = irq_event_count ? irq_events[0].clock : MD_NEVER;
    uint64_t next = next_vdp < next_dma ? next_vdp : next_dma;
    next = next < next_ym ? next : next_ym;
    next = next < next_z80 ? next : next_z80;
    return next < next_irq ? next : next_irq;
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
        md_sound_advance_to(next);
        if (md_vdp_next_event() == next)
            md_vdp_advance(next);
        if (md_dma_next_event() == next)
            md_dma_advance(next);
        while (irq_event_count && irq_events[0].clock == next) {
            md_irq_event event = irq_events[0];
            unsigned i;
            for (i = 1; i < irq_event_count; i++)
                irq_events[i - 1] = irq_events[i];
            irq_event_count--;
            process_irq_event(&event);
        }
        md_z80_sync_to(md_clock);
    }
    md_clock = to_clock;
    md_sound_advance_to(to_clock);
    md_z80_sync_to(md_clock);
}

/* Native execution: the exception's frame is the game's to read and change,
 * and the return is the 68000's RTE from that frame. Nothing else is
 * restored: a handler that changes a register has changed it. */
static void take_interrupt_native(int level)
{
    const nmd_routine *routine;
    uint16_t entry_sr = nmd_cpu.sr;
    uint32_t entry_pc = nmd_cpu.pc;
    uint32_t vector;
    uint32_t handler;
    uint32_t return_pc;
    uint16_t return_sr;
#ifdef NMD_TESTING
    nmd_cpu_state planted_state = nmd_cpu;
#endif

    irq_pending &= (uint8_t)~(1u << level);
    md_irq_last_service_clock = md_clock;

    md_irq_defer_begin();
    load_sr((uint16_t)((entry_sr & 0x00FFu) | 0x2000u | ((unsigned)level << 8)));
    nmd_cpu.a[7] -= 4u;
    wr32(nmd_cpu.a[7], entry_pc);
    nmd_cpu.a[7] -= 2u;
    wr16(nmd_cpu.a[7], entry_sr);

    vector = vector_for_level(level);
    handler = rd32(vector);
    routine = find_routine(handler);
    if (!routine || !routine->abi)
        nk_fatal("interrupt vector $%02X points to $%06X with no C routine",
                 (unsigned)vector, (unsigned)(handler & 0xFFFFFFu));
    nmd_cpu.pc = handler & 0xFFFFFFu;
    md_irq_taken++;
    md_irq_defer_end();

    routine->abi();

    md_irq_defer_begin();
    return_sr = rd16(nmd_cpu.a[7]);
    nmd_cpu.a[7] += 2u;
    return_pc = rd32(nmd_cpu.a[7]);
    nmd_cpu.a[7] += 4u;
    load_sr(return_sr);
    nmd_cpu.pc = return_pc;
#ifdef NMD_TESTING
    if (M68K_PLANTED(3)) {
        nmd_cpu = planted_state;
        return_pc = entry_pc;
    }
#endif
    md_irq_defer_depth--; /* the caller's loop makes the interrupt check */
    if (return_pc != entry_pc) {
        /* The handler left another return address: the interrupted C cannot
         * be resumed there, so its frames are dropped for that routine. */
        if (!find_routine(return_pc))
            nk_fatal("interrupt handler returned to $%06X with no C routine",
                     (unsigned)(return_pc & 0xFFFFFFu));
        nmd_restart(return_pc);
    }
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
    if (md_irq_defer_depth)
        return;
    m68k_frame_yield();
    if (md_m68k_native()) {
        /* Nesting is the status register's: a handler runs at its level. */
        while ((level = next_eligible_level()) != 0)
            take_interrupt_native(level);
        return;
    }
    if (irq_delivering)
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
    md_dma_reset();
    md_sound_reset(cart ? cart->region : NMD_NTSC);
    md_clock_hook = md_advance_to;
    md_irq_check_hook = md_irq_dispatch_pending;
    md_z80_power(cart);
    if (md_m68k_native())
        m68k_load_reset(cart);
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
