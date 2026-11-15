#include "apu_internal.h"

#include <limits.h>
#include <string.h>

enum {
    /* Approximate NTSC 4-step IRQ timing and half-frame length clocks. The
     * parity-dependent $4017 reset delay and quarter-frame clocks are not yet
     * modeled. */
    NTSC_FRAME_IRQ_FIRST_CPU_CYCLE = 29829,
    NTSC_FRAME_IRQ_PERIOD_CPU_CYCLES = 29830,
    NTSC_HALF_FRAME_FIRST_CPU_CYCLE = 14913,
    NTSC_HALF_FRAME_PERIOD_CPU_CYCLES = 14913
};

static const uint8_t length_counter_table[32] = {
    10, 254, 20, 2, 40, 4, 80, 6,
    160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22,
    192, 24, 72, 26, 16, 28, 32, 30
};

void nnes_apu_init(nnes_apu *apu)
{
    if (!apu) return;
    memset(apu, 0, sizeof(*apu));
    apu->next_frame_irq_tenths =
        (uint64_t)NTSC_FRAME_IRQ_FIRST_CPU_CYCLE * 10;
    apu->next_length_clock_tenths =
        (uint64_t)NTSC_HALF_FRAME_FIRST_CPU_CYCLE * 10;
}

nnes_result nnes_apu_advance_tenths(nnes_apu *apu, uint64_t tenths)
{
    if (!apu) return NNES_INVALID_ARGUMENT;
    if (tenths == 0) return NNES_OK;
    if (tenths > UINT64_MAX - apu->elapsed_tenths)
        return NNES_CYCLE_OVERFLOW;

    apu->frame_irq_set_this_tick = 0;
    apu->elapsed_tenths += tenths;
    while (apu->elapsed_tenths >= apu->next_frame_irq_tenths) {
        if (!apu->frame_irq_inhibit && !apu->five_step_mode) {
            apu->frame_irq_flag = 1;
            if (apu->elapsed_tenths == apu->next_frame_irq_tenths)
                apu->frame_irq_set_this_tick = 1;
        }
        uint64_t period = (uint64_t)NTSC_FRAME_IRQ_PERIOD_CPU_CYCLES * 10;
        if (period > UINT64_MAX - apu->next_frame_irq_tenths)
            return NNES_CYCLE_OVERFLOW;
        apu->next_frame_irq_tenths += period;
    }
    while (apu->elapsed_tenths >= apu->next_length_clock_tenths) {
        for (unsigned channel = 0; channel < 4; ++channel) {
            if (apu->length_counter[channel] > 0 &&
                !apu->length_halt[channel])
                --apu->length_counter[channel];
        }
        uint64_t period =
            (uint64_t)NTSC_HALF_FRAME_PERIOD_CPU_CYCLES * 10;
        if (period > UINT64_MAX - apu->next_length_clock_tenths)
            return NNES_CYCLE_OVERFLOW;
        apu->next_length_clock_tenths += period;
    }
    return NNES_OK;
}

nnes_result nnes_apu_read_status(nnes_apu *apu, uint8_t open_bus,
                                 uint8_t *value)
{
    if (!apu || !value) return NNES_INVALID_ARGUMENT;

    /* Pulse, triangle, and noise status reflects their length counters; bit 5
     * is open bus. DMC playback and its status bit are not modeled yet. */
    *value = (uint8_t)(open_bus & 0x20);
    for (unsigned channel = 0; channel < 4; ++channel) {
        if (apu->length_counter[channel] > 0)
            *value |= (uint8_t)(1u << channel);
    }
    if (apu->frame_irq_flag) *value |= 0x40;
    if (apu->frame_irq_flag && !apu->frame_irq_set_this_tick)
        apu->frame_irq_flag = 0;
    return NNES_OK;
}

nnes_result nnes_apu_write_status(nnes_apu *apu, uint8_t value)
{
    if (!apu) return NNES_INVALID_ARGUMENT;
    apu->channel_enable = (uint8_t)(value & 0x1f);
    for (unsigned channel = 0; channel < 4; ++channel) {
        if ((apu->channel_enable & (uint8_t)(1u << channel)) == 0)
            apu->length_counter[channel] = 0;
    }
    /* DMC playback and its IRQ are not implemented in this hardware slice. */
    return NNES_OK;
}

nnes_result nnes_apu_write_register(nnes_apu *apu, uint16_t address,
                                    uint8_t value)
{
    if (!apu || address < 0x4000 || address > 0x4013)
        return NNES_INVALID_ARGUMENT;
    apu->registers[address - 0x4000] = value;
    switch (address) {
    case 0x4000:
        apu->length_halt[0] = (uint8_t)((value & 0x20) != 0);
        break;
    case 0x4004:
        apu->length_halt[1] = (uint8_t)((value & 0x20) != 0);
        break;
    case 0x4008:
        apu->length_halt[2] = (uint8_t)((value & 0x80) != 0);
        break;
    case 0x400c:
        apu->length_halt[3] = (uint8_t)((value & 0x20) != 0);
        break;
    case 0x4003:
    case 0x4007:
    case 0x400b:
    case 0x400f: {
        unsigned channel = (address - 0x4003) / 4;
        if (apu->channel_enable & (uint8_t)(1u << channel))
            apu->length_counter[channel] =
                length_counter_table[value >> 3];
        break;
    }
    default:
        break;
    }
    return NNES_OK;
}

nnes_result nnes_apu_write_frame_counter(nnes_apu *apu, uint8_t value)
{
    if (!apu) return NNES_INVALID_ARGUMENT;
    apu->five_step_mode = (uint8_t)((value & 0x80) != 0);
    apu->frame_irq_inhibit = (uint8_t)((value & 0x40) != 0);
    apu->frame_irq_set_this_tick = 0;
    if (apu->frame_irq_inhibit) apu->frame_irq_flag = 0;

    uint64_t period = (uint64_t)NTSC_FRAME_IRQ_PERIOD_CPU_CYCLES * 10;
    if (period > UINT64_MAX - apu->elapsed_tenths)
        return NNES_CYCLE_OVERFLOW;
    apu->next_frame_irq_tenths = apu->elapsed_tenths + period;
    period = (uint64_t)NTSC_HALF_FRAME_FIRST_CPU_CYCLE * 10;
    if (period > UINT64_MAX - apu->elapsed_tenths)
        return NNES_CYCLE_OVERFLOW;
    apu->next_length_clock_tenths = apu->elapsed_tenths + period;
    return NNES_OK;
}
