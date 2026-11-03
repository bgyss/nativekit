/* YM2612 port latches and register storage; timers and operators follow later. */
#include <stdint.h>
#include <string.h>

#include "sound.h"
#include "nk/fatal.h"

static uint8_t address_latch[2];
static uint8_t registers[2][256];
static uint64_t current_clock;

void md_ym_reset(void)
{
    memset(address_latch, 0, sizeof address_latch);
    memset(registers, 0, sizeof registers);
    current_clock = 0;
}

static void advance_to(uint64_t clock)
{
    if (clock < current_clock)
        nk_fatal("YM2612 timestamp moved backward");
    current_clock = clock;
}

uint8_t md_ym_read(uint8_t port, uint64_t clock)
{
    advance_to(clock);
    if (port > 3u)
        nk_fatal("YM2612 port %u is outside 0-3", (unsigned)port);
    /* The busy/timer status bits are added with the shared sound scheduler. */
    return 0;
}

void md_ym_write(uint8_t port, uint8_t value, uint64_t clock)
{
    unsigned bank;

    advance_to(clock);
    if (port > 3u)
        nk_fatal("YM2612 port %u is outside 0-3", (unsigned)port);
    bank = port >> 1;
    if (!(port & 1u))
        address_latch[bank] = value;
    else
        registers[bank][address_latch[bank]] = value;
}
