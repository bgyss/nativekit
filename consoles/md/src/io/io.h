/* Host input staged for the Mega Drive controller ports. */
#ifndef NMD_IO_H
#define NMD_IO_H

#include <stdint.h>

typedef struct nmd_cart nmd_cart;

void md_io_reset(const nmd_cart *cart);
void md_io_input(const uint32_t *buttons);
uint32_t md_io_button_mask(int port);
uint8_t md_io_read8(uint32_t addr);
uint16_t md_io_read16(uint32_t addr);
void md_io_write8(uint32_t addr, uint8_t value);
void md_io_write16(uint32_t addr, uint16_t value);

#endif
