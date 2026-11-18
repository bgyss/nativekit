#ifndef NNES_INTERNAL_H
#define NNES_INTERNAL_H

#include "nnes/nes.h"

/* Seed the CPU bus latch with an omitted instruction operand byte before a
 * helper emits the instruction's first indexed dummy/data bus cycle. */
nnes_result nnes_seed_open_bus(nnes *console, uint8_t value);

#endif
