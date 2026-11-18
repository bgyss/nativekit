/* A loop whose accesses take no console time must not hang the front end.
 * NSHVC_WATCHDOG=N ends a run after N seconds without a heartbeat.
 * Default: 30 seconds; explicit 0: off.
 * The timer thread reads only atomic heartbeat state, never the console.
 */
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "nk/host.h"
#include "watchdog.h"

static atomic_uint_fast64_t last_frame;
static uint64_t deadline_ns;
static long watch_secs;
static int started;
static uint64_t last_check;

void snes_watchdog_frame(void)
{
    atomic_store_explicit(&last_frame, host_ticks_ns(), memory_order_relaxed);
}

static void watchdog(void)
{
    uint64_t now = host_ticks_ns();
    uint64_t last = atomic_load_explicit(&last_frame, memory_order_relaxed);
    /* The host can park the game while inactive; sleep also suspends this
       timer. Grant a fresh deadline before judging a resumed process. */
    if (!host_app_active() || (last_check && now - last_check > 1000000000ull)) {
        atomic_store_explicit(&last_frame, now, memory_order_relaxed);
        last_check = now;
        return;
    }
    last_check = now;
    if (now - last < deadline_ns)
        return;
    fprintf(stderr, "nativeshvc: no frame in %ld seconds: a loop that waits for the console's time, "
            "which its accesses do not take (wait with nshvc_wait_until or spc_wait_until)\n", watch_secs);
    fflush(stderr);
    _Exit(70);
}

void snes_watchdog_start(int headless)
{
    const char *e;
    if (started)
        return;
    started = 1;
    e = getenv("NSHVC_WATCHDOG");
    (void)headless;
    watch_secs = e ? atol(e) : 30;
    if (watch_secs <= 0)
        return;
    deadline_ns = (uint64_t)watch_secs * 1000000000u;
    snes_watchdog_frame();
    host_every(100, watchdog);
}
