/*
 * harness.c: the lockstep's live runtime (check/live.c) on a fake machine.
 *
 * A routine polls a hardware port four times; each pass starts at the same
 * instruction with the same registers. The original was interrupted between
 * its second and third poll. The C must pause there, after two polls, not at
 * the first pass whose registers match; and must not pause at all where the
 * original took no interrupt. Nothing here comes from a game.
 */
#include <stdio.h>
#include <string.h>

#include "nmd/machine.h"
#include "live.h"

void nmd_live_at(uint32_t address);

static unsigned bus_reads;

static uint32_t bus(uint32_t address, uint32_t width, uint32_t write, uint32_t value,
                    uint64_t clock, uint32_t refresh_counter, uint64_t refresh_sync,
                    uint32_t prefetch)
{
    (void)address; (void)width; (void)write; (void)value; (void)clock;
    (void)refresh_counter; (void)refresh_sync; (void)prefetch;
    bus_reads++;
    return 0;
}

static uint32_t z80_bus(uint32_t address, uint32_t write, uint32_t value, uint64_t clock)
{
    (void)address; (void)write; (void)value; (void)clock;
    return 0;
}

static void sync(uint64_t clock, uint32_t refresh_counter, uint64_t refresh_sync)
{
    (void)clock; (void)refresh_counter; (void)refresh_sync;
}

static void poller(void)
{
    for (int pass = 0; pass < 4; pass++) {
        nmd_live_at(0x100);
        rd16(0xC00004);
    }
    nmd_live_at(0x106);
}

static unsigned loop_visits;

static void ram_loop(void)
{
    for (int i = 0; i < 2; i++) {
        nmd_live_at(0x100);
        loop_visits++;
    }
}

static int fails;

static void expect(int condition, const char *what)
{
    if (!condition) {
        fprintf(stderr, "live_check: %s\n", what);
        fails++;
    }
}

int main(void)
{
    static const nmd_live_slot slots[4] = {
        {10, 0, 0xC00004, 0, 0, 2, 0}, {20, 0, 0xC00004, 0, 0, 2, 0},
        {30, 0, 0xC00004, 0, 0, 2, 0}, {40, 0, 0xC00004, 0, 0, 2, 0},
    };
    nmd_live_pause pause;
    int handle, status;

    nmd_live_bind(bus, z80_bus, sync, 0x400000);
    memset(&nmd_cpu, 0, sizeof nmd_cpu);
    memset(&pause, 0, sizeof pause);
    pause.clock = 25;               /* after the second poll, before the third */
    pause.pc = 0x100;
    pause.kind = NMD_LIVE_PAUSE_INTERRUPT;

    status = nmd_live_start(1, slots, 4, NULL, 0, &pause, 1, poller, &handle);
    expect(status == 1, "the C did not pause");
    expect(nmd_live_result_of(handle)->slots_used == 2 && bus_reads == 2,
           "the C paused at a pass whose registers match before the polls the original made");
    expect(!nmd_live_at_end(handle), "the C paused at its end, not at the mark");
    status = nmd_live_continue(handle);
    expect(status == 0 && nmd_live_result_of(handle)->slots_used == 4 && bus_reads == 4,
           "the C did not finish its polls after the pause");
    nmd_live_release(handle);

    /* no interrupt in the original: no pause */
    memset(&nmd_cpu, 0, sizeof nmd_cpu);
    bus_reads = 0;
    status = nmd_live_start(1, slots, 4, NULL, 0, NULL, 0, poller, &handle);
    expect(status == 0 && bus_reads == 4, "the C paused where the original was not interrupted");
    nmd_live_release(handle);

    /* Two stops before one instruction, then a stop at its next visit. */
    nmd_live_pause repeated[3] = {0};
    for (int i = 0; i < 3; i++)
        repeated[i].pc = 0x100;
    repeated[2].index[0] = 1;
    status = nmd_live_start(1, NULL, 0, NULL, 0, repeated, 3, ram_loop, &handle);
    expect(status == 1 && loop_visits == 0, "missed first loop pause");
    status = nmd_live_continue(handle);
    expect(status == 1 && loop_visits == 0, "advanced between stops at one instruction");
    status = nmd_live_continue(handle);
    expect(status == 1 && loop_visits == 1, "merged pauses across distinct visits");
    status = nmd_live_continue(handle);
    expect(status == 0 && loop_visits == 2, "lost a loop iteration");
    nmd_live_release(handle);

    if (fails)
        return 1;
    printf("live_check: ok (a polling pass pauses after the original's polls, not at the "
           "first matching pass; no pause without an interrupt)\n");
    return 0;
}
