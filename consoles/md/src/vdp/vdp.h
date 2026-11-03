/* Internal VDP state and port interface. */
#ifndef NMD_VDP_H
#define NMD_VDP_H

#include <stdint.h>

#include "nmd/cart.h"

enum {
    MD_VDP_DATA_PORT = 0,
    MD_VDP_CONTROL_PORT = 4,
    MD_VDP_HV_PORT = 8,
    MD_VDP_TRACE_READ = 2,
    MD_VDP_TRACE_WRITE = 3,
    MD_VDP_TRACE_DMA = 4
};

typedef void (*md_vdp_trace_fn)(uint64_t clock, uint8_t operation, uint8_t width,
                                uint32_t port, uint32_t value);
extern md_vdp_trace_fn md_vdp_trace_hook;

void md_vdp_reset(const nmd_cart *cart);
void md_vdp_set_register(unsigned reg, uint8_t value);
void md_vdp_advance(uint64_t to_clock);
uint64_t md_vdp_next_event(void);
uint64_t md_vdp_next_vblank_clock(void);
uint64_t md_vdp_frame_clocks(void);
uint64_t md_vdp_frame_count(void);
uint64_t md_vdp_vblank_count(void);
uint64_t md_vdp_hint_count(void);
unsigned md_vdp_current_line(void);
unsigned md_vdp_field(void);
int md_vdp_active_display(void);
void md_vdp_dimensions(int *width, int *height);
uint16_t md_vdp_hv_counter(void);
uint8_t md_vdp_register(unsigned reg);
uint8_t md_vdp_vram_byte(uint16_t addr);
uint16_t md_vdp_cram_value(unsigned index);
uint16_t md_vdp_vsram_value(unsigned index);
uint8_t md_vdp_code(void);
uint16_t md_vdp_address(void);
void md_vdp_set_address(uint16_t value);
void md_vdp_dma_write(uint8_t write_code, uint8_t width, uint16_t addr, uint16_t value);
void md_vdp_dma_write_byte(uint16_t addr, uint8_t value);
uint8_t md_vdp_dma_read_copy_byte(uint16_t addr);
void md_vdp_trace_dma(uint64_t clock, uint8_t width, uint32_t addr, uint16_t value);

uint16_t md_vdp_read16(uint32_t port, uint64_t clock);
void md_vdp_write16(uint32_t port, uint16_t value, uint64_t clock);
void md_vdp_write8(uint32_t port, uint8_t value, uint64_t clock);

#endif
