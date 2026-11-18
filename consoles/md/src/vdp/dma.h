/* Internal VDP DMA engine interface. */
#ifndef NMD_DMA_H
#define NMD_DMA_H

#include <stdint.h>

void md_dma_reset(void);
void md_dma_start(uint64_t clock);
void md_dma_advance(uint64_t to_clock);
uint64_t md_dma_next_event(void);
int md_dma_busy(void);
int md_dma_bus_owned(void);
int md_dma_waiting_seed(void);
void md_dma_seed(uint16_t value, uint64_t clock);

#endif
