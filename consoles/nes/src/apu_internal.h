#ifndef NNES_APU_INTERNAL_H
#define NNES_APU_INTERNAL_H

#include "nnes/nes.h"

typedef struct {
    uint64_t elapsed_tenths;
    uint64_t next_frame_irq_tenths;
    uint8_t frame_irq_flag;
    uint8_t frame_irq_inhibit;
    uint8_t five_step_mode;
    uint8_t frame_irq_set_this_tick;
    uint8_t channel_enable;
    uint8_t registers[0x14];
} nnes_apu;

void nnes_apu_init(nnes_apu *apu);
nnes_result nnes_apu_advance_tenths(nnes_apu *apu, uint64_t tenths);
nnes_result nnes_apu_read_status(nnes_apu *apu, uint8_t open_bus,
                                 uint8_t *value);
nnes_result nnes_apu_write_status(nnes_apu *apu, uint8_t value);
nnes_result nnes_apu_write_frame_counter(nnes_apu *apu, uint8_t value);
nnes_result nnes_apu_write_register(nnes_apu *apu, uint16_t address,
                                    uint8_t value);

#endif
