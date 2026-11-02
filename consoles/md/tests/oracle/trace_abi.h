/* Candidate test-hook ABI. No emulator is instrumented by this file alone. */
#ifndef NMD_TRACE_ABI_H
#define NMD_TRACE_ABI_H
#include <stdint.h>

#define NMD_TRACE_ABI_VERSION 2u
#define NMD_TRACE_CAP_M68K_CPU (UINT64_C(1) << 0)
#define NMD_TRACE_CAP_VDP_PORT (UINT64_C(1) << 1)
#define NMD_TRACE_CAP_PSG_WRITE (UINT64_C(1) << 2)
#define NMD_TRACE_CAP_YM_WRITE (UINT64_C(1) << 3)
#define NMD_TRACE_CAP_YM_PORT (UINT64_C(1) << 4)
#define NMD_TRACE_CAP_Z80_CPU (UINT64_C(1) << 5)
#define NMD_TRACE_CAP_VDP_DMA_68K (UINT64_C(1) << 6)
#define NMD_TRACE_CAP_CHIP_AUDIO (UINT64_C(1) << 7)
#define NMD_TRACE_CAP_VDP_DMA_FILL (UINT64_C(1) << 8)
#define NMD_TRACE_CAP_VDP_DMA_COPY (UINT64_C(1) << 9)
#define NMD_TRACE_CAP_VDP_HINT_LINE (UINT64_C(1) << 10)
#define NMD_TRACE_DMA_FILL_ADDR_TAG UINT32_C(0x10000000)
#define NMD_TRACE_DMA_COPY_ADDR_TAG UINT32_C(0x20000000)
enum { NMD_TRACE_M68K = 1, NMD_TRACE_Z80, NMD_TRACE_VDP, NMD_TRACE_YM2612, NMD_TRACE_PSG };
enum { NMD_TRACE_FETCH = 1, NMD_TRACE_READ, NMD_TRACE_WRITE, NMD_TRACE_DMA, NMD_TRACE_IRQ };
typedef void (*nmd_trace_callback)(void *user, uint64_t clock, uint8_t device,
                                   uint8_t operation, uint8_t width, uint32_t address, uint32_t value);
typedef void (*nmd_trace_audio_callback)(void *user, uint8_t device, int16_t left, int16_t right);
uint32_t nmd_trace_version(void);
uint64_t nmd_trace_capabilities(void);
int nmd_trace_register(uint32_t version, nmd_trace_callback callback, void *user);
int nmd_trace_audio_register(uint32_t version, nmd_trace_audio_callback callback, void *user);
int nmd_trace_audio_active(void);
void nmd_trace_audio_emit(uint8_t device, int16_t left, int16_t right);
void nmd_trace_reset(void);
void nmd_trace_adjust(uint64_t deduction);
void nmd_trace_emit(uint32_t local_clock, uint8_t device, uint8_t operation,
                    uint8_t width, uint32_t address, uint32_t value);
int nmd_trace_error(void);
#endif
