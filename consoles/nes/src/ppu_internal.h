#ifndef NNES_PPU_INTERNAL_H
#define NNES_PPU_INTERNAL_H

#include "nnes/nes.h"

typedef struct {
    uint8_t ctrl;
    uint8_t mask;
    uint8_t status;
    uint8_t oam_address;
    uint8_t oam[256];
    uint8_t nametable[4096];
    uint8_t palette[32];
    uint8_t io_latch;
    uint8_t read_buffer;
    uint8_t write_toggle;
    uint8_t fine_x;
    uint8_t nmi_pending;
    uint8_t time_remainder;
    uint16_t v;
    uint16_t t;
    uint64_t frame_dot;
} nnes_ppu;

typedef struct {
    void *user;
    nnes_ppu_read_fn read;
    nnes_ppu_write_fn write;
    nnes_ppu_mirroring_fn mirroring;
} nnes_ppu_mapper;

void nnes_ppu_init(nnes_ppu *ppu);
void nnes_ppu_advance_tenths(nnes_ppu *ppu, uint64_t tenths);
nnes_result nnes_ppu_cpu_read(nnes_ppu *ppu, const nnes_ppu_mapper *mapper,
                              uint16_t address, uint8_t *value);
nnes_result nnes_ppu_cpu_write(nnes_ppu *ppu, const nnes_ppu_mapper *mapper,
                               uint16_t address, uint8_t value);
nnes_result nnes_ppu_take_pending_nmi(nnes_ppu *ppu, int *pending);

#endif
