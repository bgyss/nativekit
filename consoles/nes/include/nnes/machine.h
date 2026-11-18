/*
 * machine.h: the NES machine model seen by a game's native C.
 *
 * As with SNES machine.h, a decompiled game makes its memory accesses through
 * the console API, keeps the original CPU registers in a shared register file,
 * and dispatches computed code addresses through the cartridge's native
 * routine table. NES helpers take the console explicitly so separate NES
 * instances keep separate state. They return errors instead of inventing
 * values for unfinished hardware. No helper interprets 6502 instructions.
 * Each represented bus access increments a per-console CPU-bus-cycle counter
 * and advances the basic NTSC PPU and APU frame-counter clocks. Rendering, APU
 * channel units/mixing, DMC DMA, and general interrupt scheduling remain
 * future slices. PPU NMI is delivered at wait-poll boundaries only.
 */
#ifndef NNES_MACHINE_H
#define NNES_MACHINE_H

#include "nnes/nes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t by_access[2][NNES_KINDS]; /* [read/write][nnes_access_kind] */
} nnes_access_counts;

enum {
    NNES_P_C = 0x01, /* carry */
    NNES_P_Z = 0x02, /* zero */
    NNES_P_I = 0x04, /* interrupt disable */
    NNES_P_D = 0x08, /* decimal flag; 2A03 arithmetic remains binary */
    NNES_P_B = 0x10, /* stack image only, not a stored CPU flag */
    NNES_P_U = 0x20, /* unused bit; set in pushed status bytes */
    NNES_P_V = 0x40, /* overflow */
    NNES_P_N = 0x80  /* negative */
};

/* CLC/SEC, CLI/SEI, CLD/SED, and CLV can update P through these helpers.
 * B remains stack-only and U remains set in the stored status register. */
static inline uint8_t nnes_p_set_flags(uint8_t p, uint8_t flags)
{
    return (uint8_t)(((p | flags | NNES_P_U) & (uint8_t)~NNES_P_B));
}

static inline uint8_t nnes_p_clear_flags(uint8_t p, uint8_t flags)
{
    return (uint8_t)(((p & (uint8_t)~flags & (uint8_t)~NNES_P_B) | NNES_P_U));
}

/* Update N/Z as an 8-bit load or pull does, preserve C/I/D/V, keep U set,
 * and clear B from the stored status. */
static inline uint8_t nnes_nz8(uint8_t p, uint8_t value)
{
    p = nnes_p_clear_flags(p, NNES_P_N | NNES_P_Z);
    if (value == 0) p |= NNES_P_Z;
    if (value & 0x80) p |= NNES_P_N;
    return p;
}

/* 6502 zero-page indexed addressing wraps within page zero. */
static inline uint8_t nnes_zp_index(uint8_t address, uint8_t index)
{
    return (uint8_t)(address + index);
}

typedef struct {
    uint16_t base;
    uint16_t address;
    uint8_t page_crossed;
} nnes_indexed_address;

/* Add an 8-bit X/Y index in the 16-bit CPU address space. This reports a page
 * crossing but does not emit an instruction-specific dummy bus access. */
static inline nnes_indexed_address nnes_add_index16(uint16_t base,
                                                    uint8_t index)
{
    uint16_t address = (uint16_t)(base + index);
    nnes_indexed_address result = {
        base, address, (uint8_t)((base & 0xff00) != (address & 0xff00))
    };
    return result;
}

/* Bus address used during page fixup: high byte from base, low from target. */
static inline uint16_t nnes_page_fixup_dummy_address(uint16_t base,
                                                     uint16_t effective)
{
    return (uint16_t)((base & 0xff00) | (effective & 0x00ff));
}

/* A native-C implementation of one ALU operation's result and status. These
 * helpers model individual operations for decompiled C; they do not fetch or
 * execute instructions. The 2A03 ignores D for ADC/SBC arithmetic. */
typedef struct {
    uint8_t value;
    uint8_t p;
} nnes_alu8_result;

/* A register load or transfer result: the value is unchanged and N/Z update. */
static inline nnes_alu8_result nnes_load8(uint8_t p, uint8_t value)
{
    nnes_alu8_result out = {value, nnes_nz8(p, value)};
    return out;
}

static inline nnes_alu8_result nnes_adc8(uint8_t p, uint8_t a, uint8_t value)
{
    uint16_t sum = (uint16_t)a + value + ((p & NNES_P_C) != 0);
    uint8_t result = (uint8_t)sum;
    uint8_t flags = (uint8_t)(p & (uint8_t)~(NNES_P_C | NNES_P_V | NNES_P_B));
    flags |= NNES_P_U;
    if (sum & 0x100) flags |= NNES_P_C;
    if ((uint8_t)~(a ^ value) & (a ^ result) & 0x80) flags |= NNES_P_V;
    nnes_alu8_result out = {result, nnes_nz8(flags, result)};
    return out;
}

static inline nnes_alu8_result nnes_sbc8(uint8_t p, uint8_t a, uint8_t value)
{
    uint16_t sum = (uint16_t)a + (uint8_t)~value + ((p & NNES_P_C) != 0);
    uint8_t result = (uint8_t)sum;
    uint8_t flags = (uint8_t)(p & (uint8_t)~(NNES_P_C | NNES_P_V | NNES_P_B));
    flags |= NNES_P_U;
    if (sum & 0x100) flags |= NNES_P_C;
    if ((a ^ result) & (a ^ value) & 0x80) flags |= NNES_P_V;
    nnes_alu8_result out = {result, nnes_nz8(flags, result)};
    return out;
}

static inline uint8_t nnes_cmp8(uint8_t p, uint8_t lhs, uint8_t rhs)
{
    uint8_t result = (uint8_t)(lhs - rhs);
    p = nnes_p_clear_flags(p, NNES_P_C);
    if (lhs >= rhs) p = nnes_p_set_flags(p, NNES_P_C);
    return nnes_nz8(p, result);
}

static inline uint8_t nnes_bit8(uint8_t p, uint8_t a, uint8_t value)
{
    p = nnes_p_clear_flags(p, NNES_P_N | NNES_P_V | NNES_P_Z);
    p = nnes_p_set_flags(p, value & (NNES_P_N | NNES_P_V));
    if ((a & value) == 0) p |= NNES_P_Z;
    return p;
}

/* 2A03 logical ALU operations preserve C/I/D/V and update N/Z. */
static inline nnes_alu8_result nnes_and8(uint8_t p, uint8_t a, uint8_t value)
{
    uint8_t result = (uint8_t)(a & value);
    return nnes_load8(p, result);
}

static inline nnes_alu8_result nnes_ora8(uint8_t p, uint8_t a, uint8_t value)
{
    uint8_t result = (uint8_t)(a | value);
    return nnes_load8(p, result);
}

static inline nnes_alu8_result nnes_eor8(uint8_t p, uint8_t a, uint8_t value)
{
    uint8_t result = (uint8_t)(a ^ value);
    return nnes_load8(p, result);
}

static inline nnes_alu8_result nnes_asl8(uint8_t p, uint8_t value)
{
    uint8_t result = (uint8_t)(value << 1);
    p = (uint8_t)((p & (uint8_t)~(NNES_P_C | NNES_P_B)) | NNES_P_U |
                  ((value >> 7) & NNES_P_C));
    nnes_alu8_result out = {result, nnes_nz8(p, result)};
    return out;
}

static inline nnes_alu8_result nnes_lsr8(uint8_t p, uint8_t value)
{
    uint8_t result = (uint8_t)(value >> 1);
    p = (uint8_t)((p & (uint8_t)~(NNES_P_C | NNES_P_B)) | NNES_P_U |
                  (value & NNES_P_C));
    nnes_alu8_result out = {result, nnes_nz8(p, result)};
    return out;
}

static inline nnes_alu8_result nnes_rol8(uint8_t p, uint8_t value)
{
    uint8_t result = (uint8_t)((value << 1) | ((p & NNES_P_C) != 0));
    p = (uint8_t)((p & (uint8_t)~(NNES_P_C | NNES_P_B)) | NNES_P_U |
                  ((value >> 7) & NNES_P_C));
    nnes_alu8_result out = {result, nnes_nz8(p, result)};
    return out;
}

static inline nnes_alu8_result nnes_ror8(uint8_t p, uint8_t value)
{
    uint8_t result = (uint8_t)((value >> 1) | ((p & NNES_P_C) ? 0x80 : 0));
    p = (uint8_t)((p & (uint8_t)~(NNES_P_C | NNES_P_B)) | NNES_P_U |
                  ((value & 1) ? NNES_P_C : 0));
    nnes_alu8_result out = {result, nnes_nz8(p, result)};
    return out;
}

static inline nnes_alu8_result nnes_inc8(uint8_t p, uint8_t value)
{
    uint8_t result = (uint8_t)(value + 1);
    nnes_alu8_result out = {result, nnes_nz8(p, result)};
    return out;
}

static inline nnes_alu8_result nnes_dec8(uint8_t p, uint8_t value)
{
    uint8_t result = (uint8_t)(value - 1);
    nnes_alu8_result out = {result, nnes_nz8(p, result)};
    return out;
}

/* Stable undocumented 2A03 load/store combinations. Apply the LAX result's
 * value to both A and X and its P to the CPU; SAX returns A&X and leaves P. */
static inline nnes_alu8_result nnes_lax8(uint8_t p, uint8_t value)
{
    return nnes_load8(p, value);
}

static inline nnes_result nnes_lax8_commit(nnes_cpu_state *cpu,
                                           nnes_alu8_result result)
{
    if (!cpu) return NNES_INVALID_ARGUMENT;
    cpu->a = result.value;
    cpu->x = result.value;
    cpu->p = result.p;
    return NNES_OK;
}

static inline uint8_t nnes_sax8(uint8_t a, uint8_t x)
{
    return (uint8_t)(a & x);
}

typedef struct {
    uint8_t a, x, s, p;
} nnes_las8_result;

/* LAS: A, X, and S all become memory&S, with N/Z set from the result. */
static inline nnes_las8_result nnes_las8(uint8_t p, uint8_t s,
                                         uint8_t memory)
{
    uint8_t value = (uint8_t)(memory & s);
    nnes_las8_result out = {value, value, value, nnes_nz8(p, value)};
    return out;
}

static inline nnes_result nnes_las8_commit(nnes_cpu_state *cpu,
                                           nnes_las8_result result)
{
    if (!cpu) return NNES_INVALID_ARGUMENT;
    cpu->a = result.a;
    cpu->x = result.x;
    cpu->s = result.s;
    cpu->p = result.p;
    return NNES_OK;
}

/* Stable undocumented immediate ALU combinations. Each result is applied to
 * A except AXS, which returns the new X; the caller applies register updates. */
static inline nnes_alu8_result nnes_anc8(uint8_t p, uint8_t a,
                                         uint8_t immediate)
{
    nnes_alu8_result out = nnes_and8(p, a, immediate);
    out.p = nnes_p_clear_flags(out.p, NNES_P_C);
    if (out.value & 0x80) out.p = nnes_p_set_flags(out.p, NNES_P_C);
    return out;
}

static inline nnes_alu8_result nnes_alr8(uint8_t p, uint8_t a,
                                         uint8_t immediate)
{
    nnes_alu8_result anded = nnes_and8(p, a, immediate);
    return nnes_lsr8(anded.p, anded.value);
}

static inline nnes_alu8_result nnes_arr8(uint8_t p, uint8_t a,
                                         uint8_t immediate)
{
    uint8_t anded = (uint8_t)(a & immediate);
    uint8_t result = (uint8_t)((anded >> 1) |
                               ((p & NNES_P_C) ? 0x80 : 0));
    p = nnes_p_clear_flags(p, NNES_P_C | NNES_P_V);
    if (result & 0x40) p = nnes_p_set_flags(p, NNES_P_C);
    if (!!(result & 0x40) != !!(result & 0x20))
        p = nnes_p_set_flags(p, NNES_P_V);
    nnes_alu8_result out = {result, nnes_nz8(p, result)};
    return out;
}

static inline nnes_alu8_result nnes_axs8(uint8_t p, uint8_t a, uint8_t x,
                                         uint8_t immediate)
{
    uint8_t lhs = (uint8_t)(a & x);
    uint8_t result = (uint8_t)(lhs - immediate);
    nnes_alu8_result out = {result, nnes_cmp8(p, lhs, immediate)};
    return out;
}

typedef struct {
    uint8_t memory;
    uint8_t a;
    uint8_t p;
} nnes_rmw8_result;

/* Stable undocumented 2A03 combinations that first modify memory, then
 * combine that value with A. Apply result.a/result.p and finish the bus pair
 * by writing result.memory with nnes_wr8_rmw; memory writes remain explicit. */
static inline nnes_rmw8_result nnes_slo8(uint8_t p, uint8_t a,
                                         uint8_t memory)
{
    nnes_alu8_result shifted = nnes_asl8(p, memory);
    nnes_alu8_result combined = nnes_ora8(shifted.p, a, shifted.value);
    nnes_rmw8_result out = {shifted.value, combined.value, combined.p};
    return out;
}

static inline nnes_rmw8_result nnes_rla8(uint8_t p, uint8_t a,
                                         uint8_t memory)
{
    nnes_alu8_result rotated = nnes_rol8(p, memory);
    nnes_alu8_result combined = nnes_and8(rotated.p, a, rotated.value);
    nnes_rmw8_result out = {rotated.value, combined.value, combined.p};
    return out;
}

static inline nnes_rmw8_result nnes_sre8(uint8_t p, uint8_t a,
                                         uint8_t memory)
{
    nnes_alu8_result shifted = nnes_lsr8(p, memory);
    nnes_alu8_result combined = nnes_eor8(shifted.p, a, shifted.value);
    nnes_rmw8_result out = {shifted.value, combined.value, combined.p};
    return out;
}

static inline nnes_rmw8_result nnes_rra8(uint8_t p, uint8_t a,
                                         uint8_t memory)
{
    nnes_alu8_result rotated = nnes_ror8(p, memory);
    nnes_alu8_result combined = nnes_adc8(rotated.p, a, rotated.value);
    nnes_rmw8_result out = {rotated.value, combined.value, combined.p};
    return out;
}

/* DCP/ISB perform DEC/INC on memory, then CMP/SBC using the resulting value. */
static inline nnes_rmw8_result nnes_dcp8(uint8_t p, uint8_t a,
                                         uint8_t memory)
{
    nnes_alu8_result decremented = nnes_dec8(p, memory);
    nnes_rmw8_result out = {
        decremented.value, a,
        nnes_cmp8(decremented.p, a, decremented.value)
    };
    return out;
}

static inline nnes_rmw8_result nnes_isc8(uint8_t p, uint8_t a,
                                         uint8_t memory)
{
    nnes_alu8_result incremented = nnes_inc8(p, memory);
    nnes_alu8_result subtracted = nnes_sbc8(incremented.p, a,
                                            incremented.value);
    nnes_rmw8_result out = {
        incremented.value, subtracted.value, subtracted.p
    };
    return out;
}

/* Commit a pure ALU result to a CPU register or local RMW value, keeping the
 * destination and P update together. Bus writes still use the explicit APIs. */
static inline nnes_result nnes_alu8_commit(nnes_cpu_state *cpu,
                                           uint8_t *destination,
                                           nnes_alu8_result result)
{
    if (!cpu || !destination) return NNES_INVALID_ARGUMENT;
    *destination = result.value;
    cpu->p = result.p;
    return NNES_OK;
}

typedef enum {
    NNES_BRANCH_BCC,
    NNES_BRANCH_BCS,
    NNES_BRANCH_BEQ,
    NNES_BRANCH_BNE,
    NNES_BRANCH_BMI,
    NNES_BRANCH_BPL,
    NNES_BRANCH_BVC,
    NNES_BRANCH_BVS
} nnes_branch_condition;

/* Evaluate one of the eight 6502 conditional branches from P. */
static inline int nnes_branch_taken(uint8_t p, nnes_branch_condition branch)
{
    switch (branch) {
    case NNES_BRANCH_BCC: return (p & NNES_P_C) == 0;
    case NNES_BRANCH_BCS: return (p & NNES_P_C) != 0;
    case NNES_BRANCH_BEQ: return (p & NNES_P_Z) != 0;
    case NNES_BRANCH_BNE: return (p & NNES_P_Z) == 0;
    case NNES_BRANCH_BMI: return (p & NNES_P_N) != 0;
    case NNES_BRANCH_BPL: return (p & NNES_P_N) == 0;
    case NNES_BRANCH_BVC: return (p & NNES_P_V) == 0;
    case NNES_BRANCH_BVS: return (p & NNES_P_V) != 0;
    }
    return 0;
}

/* next_pc is the address following a branch's displacement byte. */
static inline uint16_t nnes_branch_target(uint16_t next_pc, uint8_t displacement)
{
    int16_t offset = displacement < 0x80 ? (int16_t)displacement :
                     (int16_t)displacement - 0x100;
    return (uint16_t)(next_pc + offset);
}

/* Apply a native-C relative branch using a named condition and next_pc (after
 * the displacement byte). Returns whether it was taken, updates the CPU PC,
 * and models taken-branch dummy reads, including the old-page address on a
 * page crossing. Opcode and operand fetches remain part of the native
 * translation. Dummy-read errors propagate; entry from a cartridge callback
 * or open RMW pair is rejected. */
nnes_result nnes_branch_relative(nnes *console,
                                nnes_branch_condition branch,
                                uint16_t next_pc, uint8_t displacement,
                                int *taken);

/* A bus byte access. Failed reads leave the output byte unchanged. */
nnes_result nnes_rd8(nnes *console, uint16_t address, uint8_t *value);
nnes_result nnes_wr8(nnes *console, uint16_t address, uint8_t value);
/* Absolute data read with the omitted instruction's high operand byte seeded
 * on the CPU open bus before the data cycle. */
nnes_result nnes_rd8_absolute(nnes *console, uint16_t address,
                              uint8_t *value);
/* Zero-page data read with the omitted one-byte operand seeded on open bus. */
nnes_result nnes_rd8_zp(nnes *console, uint8_t address, uint8_t *value);
/* Ordinary read with discarded value; it retains bus side effects, cycle, and
 * native-work profile accounting. Dummy reads below skip profile accounting. */
nnes_result nnes_rd8_discard(nnes *console, uint16_t address);
/* Explicit 6502 dummy bus cycles: side effects and one base cycle are kept,
 * while native-work profile weights and access-count statistics are skipped. */
nnes_result nnes_rd8_dummy(nnes *console, uint16_t address);
nnes_result nnes_wr8_dummy(nnes *console, uint16_t address, uint8_t value);
/* Dummy read at cpu.pc+1 for a one-byte implied or accumulator instruction;
 * cpu.pc points to that instruction's opcode. */
nnes_result nnes_dummy_next_pc(nnes *console);
/* Zero-page indexed data modes ($zp,X and $zp,Y) dummy-read the unindexed
 * operand before the indexed data access. Since decompiled C omits the
 * instruction fetch, the helper seeds CPU open bus from that operand byte. */
nnes_result nnes_rd8_zp_indexed(nnes *console, uint8_t base, uint8_t index,
                                uint8_t *value);
nnes_result nnes_wr8_zp_indexed(nnes *console, uint8_t base, uint8_t index,
                                uint8_t value);
/* Zero-page RMW read with the omitted operand byte seeded before the pair. */
nnes_result nnes_rd8_zp_rmw(nnes *console, uint8_t address, uint8_t *value);
/* Absolute indexed and ($zp),Y ordinary loads/stores. Reads dummy-read only
 * on a page crossing; writes always dummy-read the partially indexed address.
 * The first indexed cycle seeds CPU open bus from the base high byte (the
 * omitted absolute operand byte or the fetched indirect pointer high byte),
 * so open-bus values and dummy-read metadata match the original instruction.
 * For indexed RMW instructions, use the specialized helpers below. */
nnes_result nnes_rd8_indexed(nnes *console, nnes_indexed_address address,
                             uint8_t *value);
nnes_result nnes_wr8_indexed(nnes *console, nnes_indexed_address address,
                             uint8_t value);
/* Absolute,X / absolute,Y byte accesses from a base and 8-bit index. */
nnes_result nnes_rd8_absolute_indexed(nnes *console, uint16_t base,
                                      uint8_t index, uint8_t *value);
nnes_result nnes_wr8_absolute_indexed(nnes *console, uint16_t base,
                                      uint8_t index, uint8_t value);
/* Count of CPU bus cycles represented by reads and writes so far. Every bus
 * access costs one CPU cycle; this is not elapsed time or complete instruction
 * timing because native C has not modeled internal/dummy cycles generally. */
uint64_t nnes_bus_cycles(const nnes *console);
/* Snapshot/reset native-C, profile-eligible read/write counts by memory kind.
 * Console-owned, wait-poll, and dummy accesses still count in nnes_bus_cycles
 * but are excluded here. Snapshots are rejected during an open RMW pair. */
nnes_result nnes_get_access_counts(const nnes *console,
                                  nnes_access_counts *counts);
nnes_result nnes_reset_access_counts(nnes *console);
/* Whole CPU cycles elapsed. The tenths accessor retains fractional profile
 * charges rather than rounding them away. */
uint64_t nnes_cpu_cycles(const nnes *console);
uint64_t nnes_cpu_cycle_tenths(const nnes *console);
/* Access-profile weights are on by default. Disabling them leaves the one
 * base cycle per bus access and explicit cycle charges in effect. */
nnes_result nnes_set_cpu_costs(nnes *console, int on);
nnes_result nnes_charge_cpu_tenths(nnes *console, uint64_t tenths);
nnes_result nnes_charge_cpu_cycles(nnes *console, uint64_t cycles);
/* Deliver a pending PPU NMI at the current native instruction boundary.
 * Call only after complete machine helpers; this rejects an open RMW pair and
 * bus/cartridge callbacks. `delivered` is set to zero when no edge is pending. */
nnes_result nnes_interrupt_checkpoint(nnes *console, int *delivered);
/* A 6502 read-modify-write pair. After rd8_rmw, no bus access or routine
 * dispatch is allowed before the matching-address write. The write emits the
 * original value as an unprofiled dummy write, then the modified data write,
 * and closes the pair. */
nnes_result nnes_rd8_rmw(nnes *console, uint16_t address, uint8_t *value);
nnes_result nnes_wr8_rmw(nnes *console, uint16_t address, uint8_t value);
/* Indexed RMW forms always make the addressing dummy read before the data
 * read, regardless of page crossing. Finish with nnes_wr8_rmw at the resolved
 * effective address. */
nnes_result nnes_rd8_zp_indexed_rmw(nnes *console, uint8_t base, uint8_t index,
                                   uint8_t *value);
/* Variant that returns the wrapped zero-page effective address needed for
 * the matching nnes_wr8_rmw call. */
nnes_result nnes_rd8_zp_indexed_rmw_resolved(
    nnes *console, uint8_t base, uint8_t index,
    uint8_t *resolved, uint8_t *value);
nnes_result nnes_rd8_indexed_rmw(nnes *console, nnes_indexed_address address,
                                 uint8_t *value);
/* Absolute,X / absolute,Y RMW. On success, resolved.address is the required
 * matching address for nnes_wr8_rmw; no bus access may intervene. */
nnes_result nnes_rd8_absolute_indexed_rmw(
    nnes *console, uint16_t base, uint8_t index,
    nnes_indexed_address *resolved, uint8_t *value);
/* Unofficial indexed RMW forms used by stable composite opcodes. Resolve the
 * zero-page pointer, emit all mode-specific dummy reads, and return the address
 * needed by nnes_wr8_rmw. */
nnes_result nnes_rd8_indirect_x_rmw(nnes *console, uint8_t pointer, uint8_t x,
                                    uint16_t *resolved, uint8_t *value);
nnes_result nnes_rd8_indirect_y_rmw(nnes *console, uint8_t pointer, uint8_t y,
                                    nnes_indexed_address *resolved,
                                    uint8_t *value);

/* Little-endian word accesses are two ordered byte bus accesses in one native
 * call. The current slice has no asynchronous interrupt scheduler, and
 * explicit interrupt entry is rejected from bus callbacks, so interrupts do
 * not split these helpers. Address+1 wraps in 16-bit space; a zero-page
 * pointer wraps its high byte at $FF. */
nnes_result nnes_rd16(nnes *console, uint16_t address, uint16_t *value);
nnes_result nnes_rd16_zp(nnes *console, uint8_t address, uint16_t *value);
/* Resolve 6502 ($zp,X) and ($zp),Y effective addresses. The X form emits its
 * mandatory dummy read at the unindexed zero-page operand, then wraps X and
 * the pointer high byte within page zero. The Y form reads the pointer and
 * returns base/effective/page-cross data; the caller emits any instruction-
 * specific dummy read and final data access. Adding Y wraps in 16-bit space. */
nnes_result nnes_resolve_zp_indirect_x(nnes *console, uint8_t pointer,
                                      uint8_t x, uint16_t *address);
nnes_result nnes_resolve_zp_indirect_y(nnes *console, uint8_t pointer,
                                      uint8_t y,
                                      nnes_indexed_address *resolved);
/* Complete byte accesses for ($zp,X) and ($zp),Y, composing pointer
 * resolution with the addressing-mode dummy reads and final data operation.
 * Use the resolvers above when the effective address will be reused or the
 * indexed result is needed separately. */
nnes_result nnes_rd8_indirect_x(nnes *console, uint8_t pointer, uint8_t x,
                               uint8_t *value);
nnes_result nnes_wr8_indirect_x(nnes *console, uint8_t pointer, uint8_t x,
                               uint8_t value);
nnes_result nnes_rd8_indirect_y(nnes *console, uint8_t pointer, uint8_t y,
                               uint8_t *value);
nnes_result nnes_wr8_indirect_y(nnes *console, uint8_t pointer, uint8_t y,
                               uint8_t value);
/* The NMOS 6502 indirect-JMP page-boundary wrap: $xxFF reads its high byte
 * from $xx00 rather than from the following page. */
nnes_result nnes_rd16_jmp_indirect(nnes *console, uint16_t address,
                                   uint16_t *value);
/* JMP $addr: set PC and dispatch the translated target as a tail action. */
nnes_result nnes_jump_absolute(nnes *console, uint16_t target);
/* JMP ($addr): resolve the NMOS page-wrapped pointer, set PC, and dispatch
 * the translated target as a tail action. */
nnes_result nnes_jump_indirect(nnes *console, uint16_t address);
nnes_result nnes_wr16(nnes *console, uint16_t address, uint16_t value);
nnes_result nnes_wr16_zp(nnes *console, uint8_t address, uint16_t value);

/* The hardware stack is page one. Pushes write at $0100|S then decrement S;
 * pulls increment S then read. Word helpers push high then low and pull low
 * then high. Instruction-shaped stack helpers treat cpu.pc as the current
 * opcode address and emit their dummy PC read at cpu.pc+1. PHA/PHP emit that
 * read before pushing. PLA/PLP emit it and one dummy read at the current stack
 * slot before incrementing S. PHP sets stacked B/U bits; PLP ignores B and
 * keeps U set. Primitive push/pull helpers emit only their stack bus access
 * and are used for saving/restoring X and Y, which have no push/pull opcodes
 * on the 2A03.
 */
nnes_result nnes_push8(nnes *console, uint8_t value);
nnes_result nnes_pull8(nnes *console, uint8_t *value);
nnes_result nnes_push16(nnes *console, uint16_t value);
nnes_result nnes_pull16(nnes *console, uint16_t *value);
nnes_result nnes_pha(nnes *console);
nnes_result nnes_pla(nnes *console);
nnes_result nnes_php(nnes *console);
nnes_result nnes_plp(nnes *console);
/* RTI: dummy read at cpu.pc+1 and current-stack reads, then pulls of P, PCL,
 * and PCH. */
nnes_result nnes_rti(nnes *console);
/* RTS: dummy PC and current-stack reads, pulls PCL/PCH, dummy-reads the
 * pulled address, then sets PC to that address plus one. This helper models
 * RTS stack/control state for computed jumps; dispatch the new PC with
 * nnes_jump_routine when it names a translated entry. Ordinary decompiled
 * subroutine returns use C return. */
nnes_result nnes_rts(nnes *console);
/* For jump tables that push target-1 and use RTS as a computed jump: perform
 * RTS, then tail-dispatch the resulting PC as translated native code. */
nnes_result nnes_jump_rts(nnes *console);

typedef nnes_result (*nnes_ready_fn)(nnes *console, void *ctx, int *ready);
/* Poll immediately and again after each poll-body charge until ready. Bus
 * accesses in the callback count base cycles and profile weights;
 * poll_tenths represents additional native-C instruction work. Callback
 * accesses are omitted from access-count snapshots, matching the reference
 * profile's poll-loop filter. With zero poll charge, each unready callback must
 * still advance time through a bus access or explicit charge. Pending PPU NMI
 * is delivered after a readiness callback returns, without splitting the
 * callback's bus sequence. A poll callback may call
 * nnes_interrupt_checkpoint between machine instructions to expose an earlier
 * safe boundary. waited_tenths is exact elapsed CPU time in tenths
 * of a cycle. This clocks the basic PPU and APU frame-status models, not APU
 * channel state. */
nnes_result nnes_wait_until(nnes *console, nnes_ready_fn ready, void *ctx,
                            uint64_t poll_tenths, uint64_t *waited_tenths);

/* Abandon the current native routine chain, set S to $FF, and dispatch the
 * registered routine at address. Call only from native routine code while
 * nnes_start is running, outside a cartridge callback; it never returns on
 * success. */
nnes_result nnes_cpu_restart(nnes *console, uint16_t address);

/* Dispatch the native C routine registered at an original 16-bit address.
 * nnes_jump_routine should be used as the tail action of its caller. The
 * pushed form writes the supplied bytes in push order before dispatch, for
 * computed jumps whose target expects caller-pushed values. None runs ROM. */
nnes_result nnes_call(nnes *console, uint16_t address);
/* Model a translated JSR/RTS call whose original JSR pushes return_address
 * (the final JSR operand byte) and whose translated target returns at
 * rts_address. It performs the JSR stack dummy read, preserves the guest stack
 * and RTS bus effects around native dispatch, and does not interpret the
 * target's instructions. */
nnes_result nnes_jsr_call(nnes *console, uint16_t address,
                          uint16_t return_address, uint16_t rts_address);
nnes_result nnes_jump_routine(nnes *console, uint16_t address);
nnes_result nnes_jump_routine_pushed(nnes *console, uint16_t address,
                                     const uint8_t *pushed, size_t count);

typedef enum {
    NNES_INT_NMI,
    NNES_INT_IRQ,
    NNES_INT_BRK
} nnes_interrupt_kind;

/* Enter NMI, IRQ, or BRK now using its cartridge vector and a native handler,
 * then perform RTI dummy reads and stack pulls if the handler leaves the
 * interrupt frame on the stack. Handlers which model their own RTI or
 * title-specific RTS are not returned a second time. For
 * BRK, cpu.pc points to the BRK opcode and resumes two bytes later; for IRQ and
 * NMI, cpu.pc is the interrupted continuation address. IRQ obeys P.I. This is
 * an explicit boundary only: no asynchronous scheduler or complete
 * instruction timing. Entry from a cartridge bus callback or during an open
 * RMW pair returns NNES_INVALID_SEQUENCE, preserving operation ordering. */
nnes_result nnes_raise_interrupt(nnes *console, nnes_interrupt_kind kind);

#ifdef __cplusplus
}
#endif
#endif
