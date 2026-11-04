/* Host input staged for the Mega Drive controller ports. */
#ifndef NMD_IO_H
#define NMD_IO_H

#include <stdint.h>

void md_io_input(const uint32_t *buttons);
uint32_t md_io_button_mask(int port);

#endif
