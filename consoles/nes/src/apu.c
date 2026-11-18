#include "apu_internal.h"

#include <limits.h>
#include <string.h>

enum {
    /* Approximate NTSC 4-step IRQ edge and period. The parity-dependent $4017
     * reset delay and quarter/half-frame channel clocks are not modeled yet. */
    NTSC_FRAME_IRQ_FIRST_CPU_CYCLE = 29829,
    NTSC_FRAME_IRQ_PERIOD_CPU_CYCLES = 29830
};

void nnes_apu_init(nnes_apu *apu)
{
    if (!apu) return;
    memset(apu, 0, sizeof(*apu));
    apu->next_frame_irq_tenths =
        (uint64_t)NTSC_FRAME_IRQ_FIRST_CPU_CYCLE * 10;
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
    return NNES_OK;
}

nnes_result nnes_apu_read_status(nnes_apu *apu, uint8_t open_bus,
                                 uint8_t *value)
{
    if (!apu || !value) return NNES_INVALID_ARGUMENT;

    /* This slice has no channel length counters or DMC engine yet. Bit 5 is
     * disconnected from the APU and retains the CPU's pre-read bus value. */
    *value = (uint8_t)(open_bus & 0x20);
    if (apu->frame_irq_flag) *value |= 0x40;
    if (apu->frame_irq_flag && !apu->frame_irq_set_this_tick)
        apu->frame_irq_flag = 0;
    return NNES_OK;
}

nnes_result nnes_apu_write_status(nnes_apu *apu, uint8_t value)
{
    if (!apu) return NNES_INVALID_ARGUMENT;
    apu->channel_enable = (uint8_t)(value & 0x1f);
    /* DMC playback and its IRQ are not implemented in this hardware slice. */
    return NNES_OK;
}

nnes_result nnes_apu_write_register(nnes_apu *apu, uint16_t address,
                                    uint8_t value)
{
    if (!apu || address < 0x4000 || address > 0x4013)
        return NNES_INVALID_ARGUMENT;
    /* Keep CPU writes observable and per-console even though pulse, triangle,
     * noise, and DMC units do not yet synthesize audio or advance counters. */
    apu->registers[address - 0x4000] = value;
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
    return NNES_OK;
}
