/* YM2612 port, timer, DAC, and timestamped sample state. */
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "sound.h"
#include "nk/fatal.h"

#define YM_STATUS_TIMER_A 0x01u
#define YM_STATUS_TIMER_B 0x02u
#define YM_STATUS_BUSY 0x80u
#define YM_MASTER_CLOCKS_PER_SAMPLE 1008u
#define YM_SAMPLE_CAPACITY 65536u
#define YM_TIMER_CLOCK_DENOMINATOR 1000000ull
#define YM_BUSY_START_DELAY 42ull
#define YM_BUSY_CYCLES 32ull
#define YM_CLOCK_INC 42ull
#define YM_NEVER UINT64_MAX

md_ym_state md_ym;

static uint64_t master_clock_hz;
static double ym_source_rate;
static int16_t sample_ring[YM_SAMPLE_CAPACITY];
static uint64_t sample_read;
static uint64_t sample_write;

static void add_clock(uint64_t clock, uint64_t delta, uint64_t *result)
{
    if (clock > UINT64_MAX - delta)
        nk_fatal("YM2612 clock overflow");
    *result = clock + delta;
}

static uint64_t timer_period_numerator(unsigned timer)
{
    uint64_t timer_value;
    uint64_t period_microseconds;

    if (!timer) {
        timer_value = ((uint64_t)md_ym.registers[0][0x24] << 2) |
                      (md_ym.registers[0][0x25] & 0x03u);
        period_microseconds = 18u * (1024u - timer_value);
    } else {
        timer_value = md_ym.registers[0][0x26];
        period_microseconds = 288u * (256u - timer_value);
    }
    return period_microseconds * master_clock_hz;
}

static uint64_t timer_first_delta(uint64_t numerator, uint64_t *fraction)
{
    uint64_t whole = numerator / YM_TIMER_CLOCK_DENOMINATOR;
    uint64_t remainder = numerator % YM_TIMER_CLOCK_DENOMINATOR;

    *fraction = remainder;
    if (remainder)
        whole++;
    return whole;
}

static uint64_t timer_next_delta(uint64_t numerator, uint64_t *fraction)
{
    uint64_t whole = numerator / YM_TIMER_CLOCK_DENOMINATOR;
    uint64_t remainder = numerator % YM_TIMER_CLOCK_DENOMINATOR;

    *fraction += remainder;
    if (*fraction >= YM_TIMER_CLOCK_DENOMINATOR) {
        *fraction -= YM_TIMER_CLOCK_DENOMINATOR;
        whole++;
    }
    return whole;
}

static void timer_start(unsigned timer, uint64_t clock)
{
    uint64_t numerator = timer_period_numerator(timer);
    uint64_t delta;
    uint64_t *fraction = timer ? &md_ym.timer_b_fraction : &md_ym.timer_a_fraction;
    uint64_t *deadline = timer ? &md_ym.timer_b_deadline : &md_ym.timer_a_deadline;

    delta = timer_first_delta(numerator, fraction);
    add_clock(clock, delta, deadline);
}

static void timer_reschedule(unsigned timer)
{
    uint64_t numerator = timer_period_numerator(timer);
    uint64_t *fraction = timer ? &md_ym.timer_b_fraction : &md_ym.timer_a_fraction;
    uint64_t *deadline = timer ? &md_ym.timer_b_deadline : &md_ym.timer_a_deadline;
    uint64_t delta = timer_next_delta(numerator, fraction);

    add_clock(*deadline, delta, deadline);
}

static void sample_push(int16_t sample)
{
    if (sample_write - sample_read >= YM_SAMPLE_CAPACITY)
        nk_fatal("YM2612 sample stream overflow before audio drain");
    sample_ring[sample_write % YM_SAMPLE_CAPACITY] = sample;
    sample_write++;
    md_ym.sample_count++;
}

static void sample_tick(void)
{
    sample_push(md_ym.dac_enabled ? md_ym.dac_sample : 0);
    if (md_ym.next_sample_clock > UINT64_MAX - YM_MASTER_CLOCKS_PER_SAMPLE)
        nk_fatal("YM2612 sample clock overflow");
    md_ym.next_sample_clock += YM_MASTER_CLOCKS_PER_SAMPLE;
}

void md_ym_reset(nmd_region region)
{
    memset(&md_ym, 0, sizeof md_ym);
    memset(sample_ring, 0, sizeof sample_ring);
    sample_read = 0;
    sample_write = 0;
    md_ym.busy_start = YM_NEVER;
    md_ym.busy_until = YM_NEVER;
    md_ym.timer_a_deadline = YM_NEVER;
    md_ym.timer_b_deadline = YM_NEVER;
    md_ym.next_sample_clock = 0;
    master_clock_hz = region == NMD_PAL ? 53203395ull : 53693175ull;
    ym_source_rate = (double)master_clock_hz / YM_MASTER_CLOCKS_PER_SAMPLE;
}

double md_ym_source_rate(void)
{
    return ym_source_rate;
}

uint64_t md_ym_next_event(void)
{
    uint64_t next = YM_NEVER;

    if (md_ym.timer_a_running)
        next = md_ym.timer_a_deadline;
    if (md_ym.timer_b_running && md_ym.timer_b_deadline < next)
        next = md_ym.timer_b_deadline;
    return next;
}

void md_ym_advance_to(uint64_t master_clock)
{
    if (master_clock < md_ym.current_clock)
        nk_fatal("YM2612 timestamp moved backward");

    for (;;) {
        uint64_t next_timer = md_ym_next_event();
        int timer_due = next_timer <= master_clock;
        int sample_due = md_ym.next_sample_clock < master_clock;

        if (!timer_due && !sample_due)
            break;

        if (timer_due && (!sample_due || next_timer <= md_ym.next_sample_clock)) {
            if (md_ym.timer_a_running && md_ym.timer_a_deadline == next_timer) {
                if (md_ym.registers[0][0x27] & 0x04u)
                    md_ym.status |= YM_STATUS_TIMER_A;
                timer_reschedule(0);
            }
            if (md_ym.timer_b_running && md_ym.timer_b_deadline == next_timer) {
                if (md_ym.registers[0][0x27] & 0x08u)
                    md_ym.status |= YM_STATUS_TIMER_B;
                timer_reschedule(1);
            }
        } else {
            sample_tick();
        }
    }
    md_ym.current_clock = master_clock;
}

int md_ym_take_samples(int16_t *samples, int max_samples)
{
    int count = 0;

    if (max_samples < 0 || (max_samples && !samples))
        nk_fatal("invalid YM2612 sample drain buffer");
    while (sample_read != sample_write && count < max_samples) {
        samples[count++] = sample_ring[sample_read % YM_SAMPLE_CAPACITY];
        sample_read++;
    }
    return count;
}

static uint8_t ym_status(uint64_t clock)
{
    uint8_t status = md_ym.status;

    if (clock >= md_ym.busy_start && clock < md_ym.busy_until)
        status |= YM_STATUS_BUSY;
    return status;
}

uint8_t md_ym_read(uint8_t port, uint64_t clock)
{
    md_ym_advance_to(clock);
    if (port > 3u)
        nk_fatal("YM2612 port %u is outside 0-3", (unsigned)port);
    /* Sega documents status reads from all four byte ports. */
    return ym_status(clock);
}

static void timer_control_write(uint8_t value, uint8_t old, uint64_t clock)
{
    if (value & 0x10u)
        md_ym.status &= (uint8_t)~YM_STATUS_TIMER_A;
    if (value & 0x20u)
        md_ym.status &= (uint8_t)~YM_STATUS_TIMER_B;

    if (value & 0x01u) {
        if (!(old & 0x01u)) {
            md_ym.timer_a_running = 1;
            timer_start(0, clock);
        }
    } else {
        md_ym.timer_a_running = 0;
        md_ym.timer_a_deadline = YM_NEVER;
    }
    if (value & 0x02u) {
        if (!(old & 0x02u)) {
            md_ym.timer_b_running = 1;
            timer_start(1, clock);
        }
    } else {
        md_ym.timer_b_running = 0;
        md_ym.timer_b_deadline = YM_NEVER;
    }
}

static void register_write(unsigned bank, uint8_t address, uint8_t value,
                           uint64_t clock)
{
    uint8_t old = md_ym.registers[bank][address];

    md_ym.registers[bank][address] = value;
    if (bank != 0)
        return;

    if (address == 0x27u) {
        timer_control_write(value, old, clock);
    } else if (address == 0x2Bu) {
        md_ym.dac_enabled = (uint8_t)((value & 0x80u) != 0);
    } else if (address == 0x2Au) {
        md_ym.dac_data = value;
        if (md_ym.dac_enabled)
            md_ym.dac_sample = (int16_t)(((int)value - 0x80) * 64);
    }
}

void md_ym_write(uint8_t port, uint8_t value, uint64_t clock)
{
    unsigned bank;

    md_ym_advance_to(clock);
    if (port > 3u)
        nk_fatal("YM2612 port %u is outside 0-3", (unsigned)port);
    bank = port >> 1;
    if (!(port & 1u)) {
        md_ym.address_latch[bank] = value;
        return;
    }

    md_ym.busy_start = clock + YM_BUSY_START_DELAY;
    if (md_ym.busy_start < clock ||
        md_ym.busy_start > UINT64_MAX - YM_BUSY_CYCLES * YM_CLOCK_INC)
        md_ym.busy_until = UINT64_MAX;
    else
        md_ym.busy_until = md_ym.busy_start + YM_BUSY_CYCLES * YM_CLOCK_INC;
    register_write(bank, md_ym.address_latch[bank], value, clock);
}
