/* Internal contracts shared by the nativemd console modules. */
#ifndef NMD_MD_INTERNAL_H
#define NMD_MD_INTERNAL_H

#include <stdint.h>

#include "nmd/cart.h"
#include "nmd/machine.h"
#include "../vdp/vdp.h"
#include "../vdp/dma.h"

#define MD_MASTER_CLOCKS_PER_LINE 3420u

extern uint64_t md_clock;
extern uint8_t md_z80_ram[0x2000];
extern uint8_t md_memory_mode;
extern uint8_t md_z80_busreq;
extern uint8_t md_z80_busack;
extern uint8_t md_z80_reset;
extern unsigned md_irq_defer_depth;

enum {
    MD_BUS_TRACE_READ = 2,
    MD_BUS_TRACE_WRITE = 3,
    MD_Z80_TRACE_READ = 2,
    MD_Z80_TRACE_WRITE = 3
};
typedef void (*md_bus_trace_fn)(uint64_t clock, uint8_t operation, uint8_t width,
                                uint32_t addr, uint32_t value);
extern md_bus_trace_fn md_bus_trace_hook;
typedef void (*md_z80_bus_trace_fn)(uint64_t clock, uint8_t operation, uint8_t width,
                                    uint32_t addr, uint32_t value);
extern md_z80_bus_trace_fn md_z80_bus_trace_hook;

/* The scheduler services device deadlines; interrupt checks run after an
 * access, except while a longword or TAS read-modify-write is indivisible. */
extern void (*md_clock_hook)(uint64_t to_clock);
extern void (*md_irq_check_hook)(void);

void md_bus_reset(const nmd_cart *cart);
void md_irq_defer_begin(void);
void md_irq_defer_end(void);

void md_timing_reset(const nmd_cart *cart);
void md_advance_to(uint64_t to_clock);
void md_z80_power(const nmd_cart *cart);
void md_z80_sync_to(uint64_t master_clock);
uint64_t md_z80_next_event(void);
void md_z80_request_vint(void);
uint64_t md_z80_cycle_count(void);
void md_z80_bus_access_begin(unsigned tstates);
void md_z80_bus_access_end(void);
int md_z80_bus_call_active(void);
void md_z80_bus_reset(void);
void md_z80_bank_serial_write(uint8_t value);
uint8_t md_z80_window_read8(uint32_t addr);
void md_z80_window_write8(uint32_t addr, uint8_t value);
int md_z80_window_bus_ready(void);
void md_z80_window_bus_reserve(void);
/* `level` 2 asserts the EXT interrupt request; 0 releases the input line. */
void md_ext_irq_at(int level, uint64_t clock);
void md_irq_request_at(int level, uint64_t clock);
void md_irq_request(int level);
void md_irq_clear(int level);
void md_irq_dispatch_pending(void);
extern uint64_t md_irq_taken;
extern uint64_t md_irq_last_service_clock;

uint8_t nmd_bus_read8(uint32_t addr);
uint16_t nmd_bus_read16(uint32_t addr);
void nmd_bus_write8(uint32_t addr, uint8_t value);
void nmd_bus_write16(uint32_t addr, uint16_t value);
uint16_t nmd_dma_source_read16(uint32_t addr);

#endif
