/* Candidate test-hook ABI. No emulator is instrumented by this file alone. */
#ifndef NMD_TRACE_ABI_H
#define NMD_TRACE_ABI_H
#include <stdint.h>

#define NMD_TRACE_ABI_VERSION 1u
#define NMD_TRACE_CAP_M68K_CPU (UINT64_C(1) << 0)
enum { NMD_TRACE_M68K = 1, NMD_TRACE_Z80, NMD_TRACE_VDP, NMD_TRACE_YM2612, NMD_TRACE_PSG };
enum { NMD_TRACE_FETCH = 1, NMD_TRACE_READ, NMD_TRACE_WRITE, NMD_TRACE_DMA };
typedef void (*nmd_trace_callback)(void *user, uint64_t clock, uint8_t device,
                                   uint8_t operation, uint8_t width, uint32_t address, uint32_t value);
uint32_t nmd_trace_version(void);
uint64_t nmd_trace_capabilities(void);
int nmd_trace_register(uint32_t version, nmd_trace_callback callback, void *user);
void nmd_trace_reset(void);
void nmd_trace_adjust(uint64_t deduction);
void nmd_trace_emit(uint32_t local_clock, uint8_t device, uint8_t operation,
                    uint8_t width, uint32_t address, uint32_t value);
int nmd_trace_error(void);
#endif
