/* A deterministic host: backgrounding and sleep are not game stalls. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "nk/host.h"
#include "watchdog.h"

static uint64_t now = 1;
static int active = 1;
static void (*tick)(void);
uint64_t host_ticks_ns(void) { return now; }
int host_app_active(void) { return active; }
void host_every(uint32_t ms, void (*fn)(void)) { (void)ms; tick = fn; }

static void step(void)
{
    now += 100000000;
    tick();
}

int main(int argc, char **argv)
{
    int i;
    snes_watchdog_start(0);
    if (!tick)
        return 1;
    if (argc > 1 && !strcmp(argv[1], "--stall")) {
        for (i = 0; i < 12; i++)
            step();
        fprintf(stderr, "watchdog_check: a stalled game was not stopped\n");
        return 1;
    }
    active = 0;
    for (i = 0; i < 30; i++)
        step();
    active = 1;
    step();
    /* A process suspended across sleep misses timer callbacks as well. */
    now += 5000000000ull;
    tick();
    for (i = 0; i < 5; i++)
        step();
    puts("watchdog_check: background, resume and sleep passed");
    return 0;
}
