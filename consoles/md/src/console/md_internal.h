/* Internal contracts shared by the nativemd console modules. */
#ifndef NMD_MD_INTERNAL_H
#define NMD_MD_INTERNAL_H

#include <stdint.h>

#include "nmd/cart.h"
#include "nmd/machine.h"

extern uint64_t md_clock;
extern uint8_t md_z80_ram[0x2000];
extern uint8_t md_memory_mode;
extern uint8_t md_z80_busreq;
extern uint8_t md_z80_reset;
extern unsigned md_irq_defer_depth;

enum {
    MD_BUS_TRACE_READ = 2,
    MD_BUS_TRACE_WRITE = 3
};
typedef void (*md_bus_trace_fn)(uint64_t clock, uint8_t operation, uint8_t width,
                                uint32_t addr, uint32_t value);
extern md_bus_trace_fn md_bus_trace_hook;

/* The scheduler services device deadlines; interrupt checks run after an
 * access, except while a longword or TAS read-modify-write is indivisible. */
extern void (*md_clock_hook)(uint64_t to_clock);
extern void (*md_irq_check_hook)(void);

void md_bus_reset(const nmd_cart *cart);
void md_irq_defer_begin(void);
void md_irq_defer_end(void);

uint8_t nmd_bus_read8(uint32_t addr);
uint16_t nmd_bus_read16(uint32_t addr);
void nmd_bus_write8(uint32_t addr, uint8_t value);
void nmd_bus_write16(uint32_t addr, uint16_t value);

#endif
