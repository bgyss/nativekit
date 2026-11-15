/*
 * nmd_decomp.h: what a game's recovered C uses beyond the nmd bus calls
 * while its routines are still shaped like the original's: flag results of
 * 68000 and Z80 operations, and the guest-stack side of call and return.
 *
 * Header-only and written over nmd/machine.h and nmd/z80.h alone, so the
 * same source builds into the checker's hybrid library and into a native
 * cart. The helpers are transitional verification tools (D04): each is held
 * against the reference core's own execution by ops_check.py, and only the
 * operations listed here are claimed.
 */
#ifndef NMD_DECOMP_H
#define NMD_DECOMP_H

#include <stdint.h>

#include "nmd/machine.h"
#include "nmd/z80.h"

/* ---- 68000 condition codes (the low byte of nmd_cpu.sr) ---- */

enum { NMD_C = 1, NMD_V = 2, NMD_Z = 4, NMD_N = 8, NMD_X = 16 };

static inline void nmd_flags(unsigned mask, unsigned flags)
{
    nmd_cpu.sr = (uint16_t)((nmd_cpu.sr & ~mask) | (flags & mask));
}

#define NMD_M68K_OPS(bits, type, wide)                                                       \
    /* MOVE, AND, OR, EOR, NOT, TST, CLR, EXT, SWAP, MOVEQ: N Z from the result; V C clear */\
    static inline type nmd_tst##bits(type value)                                             \
    {                                                                                        \
        nmd_flags(NMD_N | NMD_Z | NMD_V | NMD_C,                                             \
                  (value >> (bits - 1) ? NMD_N : 0) | (value ? 0 : NMD_Z));                  \
        return value;                                                                        \
    }                                                                                        \
    /* ADD, ADDI, ADDQ to a data destination */                                              \
    static inline type nmd_add##bits(type destination, type source)                          \
    {                                                                                        \
        wide sum = (wide)destination + source;                                               \
        type result = (type)sum;                                                             \
        unsigned carry = (unsigned)(sum >> bits) & 1;                                        \
        unsigned overflow = (unsigned)(((destination ^ result) & (source ^ result)) >> (bits - 1)) & 1; \
        nmd_flags(NMD_X | NMD_N | NMD_Z | NMD_V | NMD_C,                                     \
                  (carry ? NMD_X | NMD_C : 0) | (overflow ? NMD_V : 0) |                     \
                  (result >> (bits - 1) ? NMD_N : 0) | (result ? 0 : NMD_Z));                \
        return result;                                                                       \
    }                                                                                        \
    static inline type nmd_sub_flags##bits(type destination, type source, unsigned mask)     \
    {                                                                                        \
        type result = (type)(destination - source);                                          \
        unsigned borrow = source > destination;                                              \
        unsigned overflow = (unsigned)(((destination ^ source) & (destination ^ result)) >> (bits - 1)) & 1; \
        nmd_flags(mask, (borrow ? NMD_X | NMD_C : 0) | (overflow ? NMD_V : 0) |              \
                  (result >> (bits - 1) ? NMD_N : 0) | (result ? 0 : NMD_Z));                \
        return result;                                                                       \
    }                                                                                        \
    /* SUB, SUBI, SUBQ to a data destination */                                              \
    static inline type nmd_sub##bits(type destination, type source)                          \
    {                                                                                        \
        return nmd_sub_flags##bits(destination, source, NMD_X | NMD_N | NMD_Z | NMD_V | NMD_C); \
    }                                                                                        \
    /* CMP, CMPI, CMPA (long): X is not changed */                                           \
    static inline void nmd_cmp##bits(type destination, type source)                          \
    {                                                                                        \
        (void)nmd_sub_flags##bits(destination, source, NMD_N | NMD_Z | NMD_V | NMD_C);       \
    }                                                                                        \
    /* NEG */                                                                                \
    static inline type nmd_neg##bits(type value)                                             \
    {                                                                                        \
        return nmd_sub##bits(0, value);                                                      \
    }                                                                                        \
    /* LSL and ASL by 1..8 or a register count (0..63); ASL's V is set when the sign changed at any step */ \
    static inline type nmd_shl##bits(type value, unsigned count, int arithmetic)             \
    {                                                                                        \
        unsigned flags = nmd_cpu.sr & NMD_X, changed = 0;                                    \
        count &= 63;                                                                         \
        for (unsigned step = 0; step < count; step++) {                                      \
            unsigned out = (unsigned)(value >> (bits - 1)) & 1;                              \
            value = (type)(value << 1);                                                      \
            changed |= out ^ ((unsigned)(value >> (bits - 1)) & 1);                          \
            flags = out ? NMD_X | NMD_C : 0;                                                 \
        }                                                                                    \
        if (!count)                                                                          \
            flags &= NMD_X;                                                                  \
        nmd_flags(NMD_X | NMD_N | NMD_Z | NMD_V | NMD_C, flags |                             \
                  (arithmetic && changed ? NMD_V : 0) |                                      \
                  (value >> (bits - 1) ? NMD_N : 0) | (value ? 0 : NMD_Z));                  \
        return value;                                                                        \
    }                                                                                        \
    /* LSR and ASR */                                                                        \
    static inline type nmd_shr##bits(type value, unsigned count, int arithmetic)             \
    {                                                                                        \
        unsigned flags = nmd_cpu.sr & NMD_X;                                                 \
        type sign = arithmetic ? (type)(value & ((type)1 << (bits - 1))) : 0;                \
        count &= 63;                                                                         \
        for (unsigned step = 0; step < count; step++) {                                      \
            flags = (value & 1) ? NMD_X | NMD_C : 0;                                         \
            value = (type)((value >> 1) | sign);                                             \
        }                                                                                    \
        if (!count)                                                                          \
            flags &= NMD_X;                                                                  \
        nmd_flags(NMD_X | NMD_N | NMD_Z | NMD_V | NMD_C, flags |                             \
                  (value >> (bits - 1) ? NMD_N : 0) | (value ? 0 : NMD_Z));                  \
        return value;                                                                        \
    }                                                                                        \
    /* ROL and ROR by a register count: C is the last bit rotated out (clear for 0), X kept */ \
    static inline type nmd_rol##bits(type value, unsigned count, int right)                  \
    {                                                                                        \
        unsigned carry = 0;                                                                  \
        count &= 63;                                                                         \
        for (unsigned step = 0; step < count; step++) {                                      \
            if (right) {                                                                     \
                carry = value & 1;                                                           \
                value = (type)((value >> 1) | ((type)carry << (bits - 1)));                  \
            } else {                                                                         \
                carry = (unsigned)(value >> (bits - 1)) & 1;                                 \
                value = (type)((type)(value << 1) | carry);                                  \
            }                                                                                \
        }                                                                                    \
        nmd_flags(NMD_N | NMD_Z | NMD_V | NMD_C, (carry ? NMD_C : 0) |                       \
                  (value >> (bits - 1) ? NMD_N : 0) | (value ? 0 : NMD_Z));                  \
        return value;                                                                        \
    }

NMD_M68K_OPS(8, uint8_t, uint32_t)
NMD_M68K_OPS(16, uint16_t, uint32_t)
NMD_M68K_OPS(32, uint32_t, uint64_t)

/* SWAP: the halves exchanged; N Z from the long, V C clear. */
static inline uint32_t nmd_swap(uint32_t value)
{
    return nmd_tst32(value << 16 | value >> 16);
}

/* DIVS.W by a nonzero divisor: the low word the quotient, the high word the
 * remainder (sign of the dividend); N Z from the quotient, V clear. A
 * quotient outside a signed word leaves the register and sets V and N,
 * clears Z. X is kept. A zero divisor traps on the 68000: not here.
 * C is KEPT, as the pinned reference core keeps it; the 68000 manual says
 * DIVS clears C. A known reference difference (check-calls.md). */
static inline void nmd_divs(uint32_t *reg, uint16_t divisor)
{
    int32_t dividend = (int32_t)*reg, by = (int16_t)divisor;
    int64_t quotient = (int64_t)dividend / by;
    int32_t remainder = (int32_t)((int64_t)dividend % by);

    if (quotient < -32768 || quotient > 32767) {
        nmd_flags(NMD_N | NMD_Z | NMD_V, NMD_N | NMD_V);
        return;
    }
    *reg = (uint32_t)(uint16_t)remainder << 16 | (uint16_t)quotient;
    nmd_flags(NMD_N | NMD_Z | NMD_V, (quotient < 0 ? NMD_N : 0) | (quotient == 0 ? NMD_Z : 0));
}

/* MULU.W, MULS.W: the low words multiplied into the whole register; N Z from
 * the long, V C clear. */
static inline void nmd_mulu(uint32_t *reg, uint16_t source)
{
    *reg = nmd_tst32((uint32_t)(uint16_t)*reg * source);
}

static inline void nmd_muls(uint32_t *reg, uint16_t source)
{
    *reg = nmd_tst32((uint32_t)((int32_t)(int16_t)*reg * (int16_t)source));
}

/* DIVU.W by a nonzero divisor, as the pinned core computes it (its shift-and-
 * subtract loop, overflow when the divisor shifted up 16 is below the
 * dividend): N from bit 15 of the quotient, Z, V clear; C KEPT as the core
 * keeps it (the 68000 clears it, as for DIVS). Overflow sets N V, clears Z,
 * and leaves the register. */
static inline void nmd_divu(uint32_t *reg, uint16_t divisor)
{
    uint32_t dividend = *reg, shifted = (uint32_t)divisor << 16;
    uint16_t quotient = 0, bit = 0;

    if (shifted < dividend) {
        nmd_flags(NMD_N | NMD_Z | NMD_V, NMD_N | NMD_V);
        return;
    }
    for (int step = 0; step < 16; step++) {
        uint32_t force = dividend >> 31;

        quotient = (uint16_t)(quotient << 1 | bit);
        dividend <<= 1;
        if (force || dividend >= shifted) {
            dividend -= shifted;
            bit = 1;
        } else {
            bit = 0;
        }
    }
    quotient = (uint16_t)(quotient << 1 | bit);
    *reg = dividend | quotient;
    nmd_flags(NMD_N | NMD_Z | NMD_V, (quotient & 0x8000 ? NMD_N : 0) | (quotient ? 0 : NMD_Z));
}

/* The 68000's condition codes, as Bcc, DBcc and Scc test them (0 T ... 15 LE). */
static inline int nmd_cc(unsigned condition)
{
    unsigned sr = nmd_cpu.sr;
    int c = !!(sr & NMD_C), v = !!(sr & NMD_V), z = !!(sr & NMD_Z), n = !!(sr & NMD_N);

    switch (condition & 15) {
    case 0: return 1;
    case 1: return 0;
    case 2: return !c && !z;            /* HI */
    case 3: return c || z;              /* LS */
    case 4: return !c;                  /* CC */
    case 5: return c;                   /* CS */
    case 6: return !z;                  /* NE */
    case 7: return z;                   /* EQ */
    case 8: return !v;                  /* VC */
    case 9: return v;                   /* VS */
    case 10: return !n;                 /* PL */
    case 11: return n;                  /* MI */
    case 12: return n == v;             /* GE */
    case 13: return n != v;             /* LT */
    case 14: return !z && n == v;       /* GT */
    default: return z || n != v;        /* LE */
    }
}

/* BTST: Z is the tested bit's complement; nothing else changes. */
static inline void nmd_btst(uint32_t value, unsigned bit)
{
    nmd_flags(NMD_Z, (value >> bit) & 1 ? 0 : NMD_Z);
}

/* Register parts. A byte or word operation leaves the rest of a data register. */
static inline void nmd_set8(uint32_t *reg, uint8_t value)
{
    *reg = (*reg & 0xFFFFFF00u) | value;
}

static inline void nmd_set16(uint32_t *reg, uint16_t value)
{
    *reg = (*reg & 0xFFFF0000u) | value;
}

/* DBF/DBRA on a data register's low word: decrement, and whether to loop. */
static inline int nmd_dbf(uint32_t *reg)
{
    uint16_t count = (uint16_t)(*reg - 1);

    nmd_set16(reg, count);
    return count != 0xFFFF;
}

/* ---- 68000 call and return over the guest stack ---- */

static inline void nmd_push32(uint32_t value)
{
    nmd_cpu.a[7] -= 4;
    wr32(nmd_cpu.a[7], value);
}

/* BSR/JSR to a routine that has C: push the return address, run it. */
#define NMD_JSR(return_address, routine) (nmd_push32(return_address), (routine)())

/* RTS: the routine's last statement. */
static inline void nmd_rts(void)
{
    nmd_cpu.pc = rd32(nmd_cpu.a[7]) & 0xFFFFFFu;
    nmd_cpu.a[7] += 4;
}

/* ---- Z80 flags (nmd_z80.f, packed S Z Y H X P/V N C) ---- */

enum { NMD_ZC = 0x01, NMD_ZN = 0x02, NMD_ZPV = 0x04, NMD_ZX = 0x08,
       NMD_ZH = 0x10, NMD_ZY = 0x20, NMD_ZZ = 0x40, NMD_ZS = 0x80 };

static inline uint8_t nmd_z_szxy(uint8_t value)
{
    return (uint8_t)((value & (NMD_ZS | NMD_ZY | NMD_ZX)) | (value ? 0 : NMD_ZZ));
}

static inline uint8_t nmd_z_parity(uint8_t value)
{
    value ^= value >> 4;
    value ^= value >> 2;
    value ^= value >> 1;
    return (value & 1) ? 0 : NMD_ZPV;
}

/* ADD A,x and ADC A,x (carry is the incoming C for ADC, 0 for ADD) */
static inline uint8_t nmd_z_add8(uint8_t a, uint8_t value, unsigned carry)
{
    unsigned sum = a + value + carry;
    uint8_t result = (uint8_t)sum;

    nmd_z80.f = (uint8_t)(nmd_z_szxy(result) | (sum > 0xFF ? NMD_ZC : 0) |
                          ((a ^ value ^ result) & NMD_ZH) |
                          (((a ^ result) & (value ^ result) & 0x80) ? NMD_ZPV : 0));
    return result;
}

/* SUB x, SBC A,x (carry is the incoming C for SBC, 0 for SUB) */
static inline uint8_t nmd_z_sub8(uint8_t a, uint8_t value, unsigned carry)
{
    unsigned difference = a - value - carry;
    uint8_t result = (uint8_t)difference;

    nmd_z80.f = (uint8_t)(nmd_z_szxy(result) | NMD_ZN | (difference > 0xFF ? NMD_ZC : 0) |
                          ((a ^ value ^ result) & NMD_ZH) |
                          (((a ^ value) & (a ^ result) & 0x80) ? NMD_ZPV : 0));
    return result;
}

/* CP x: SUB's flags, but bits 5 and 3 come from the operand */
static inline void nmd_z_cp8(uint8_t a, uint8_t value)
{
    (void)nmd_z_sub8(a, value, 0);
    nmd_z80.f = (uint8_t)((nmd_z80.f & ~(NMD_ZY | NMD_ZX)) | (value & (NMD_ZY | NMD_ZX)));
}

static inline uint8_t nmd_z_and8(uint8_t a, uint8_t value)
{
    uint8_t result = a & value;

    nmd_z80.f = (uint8_t)(nmd_z_szxy(result) | NMD_ZH | nmd_z_parity(result));
    return result;
}

/* OR x and XOR x, given the result */
static inline uint8_t nmd_z_or8(uint8_t result)
{
    nmd_z80.f = (uint8_t)(nmd_z_szxy(result) | nmd_z_parity(result));
    return result;
}

/* INC r and DEC r: C is not changed */
static inline uint8_t nmd_z_inc8(uint8_t value)
{
    uint8_t result = (uint8_t)(value + 1);

    nmd_z80.f = (uint8_t)((nmd_z80.f & NMD_ZC) | nmd_z_szxy(result) |
                          ((value & 0x0F) == 0x0F ? NMD_ZH : 0) | (value == 0x7F ? NMD_ZPV : 0));
    return result;
}

static inline uint8_t nmd_z_dec8(uint8_t value)
{
    uint8_t result = (uint8_t)(value - 1);

    nmd_z80.f = (uint8_t)((nmd_z80.f & NMD_ZC) | nmd_z_szxy(result) | NMD_ZN |
                          ((value & 0x0F) == 0x00 ? NMD_ZH : 0) | (value == 0x80 ? NMD_ZPV : 0));
    return result;
}

/* ---- Z80 call and return over the guest stack ---- */

static inline void nmd_z80_push16(uint16_t value)
{
    nmd_z80.sp = (uint16_t)(nmd_z80.sp - 1);
    zwr(nmd_z80.sp, (uint8_t)(value >> 8));
    nmd_z80.sp = (uint16_t)(nmd_z80.sp - 1);
    zwr(nmd_z80.sp, (uint8_t)value);
}

#define NMD_Z80_CALL(return_address, routine) (nmd_z80_push16(return_address), (routine)())

static inline void nmd_z80_ret(void)
{
    uint8_t low = zrd(nmd_z80.sp);

    nmd_z80.pc = (uint16_t)(low | zrd((uint16_t)(nmd_z80.sp + 1)) << 8);
    nmd_z80.sp = (uint16_t)(nmd_z80.sp + 2);
}

/* ---- instruction boundaries (lockstep) ---- */

/* A draft marks each original instruction it starts (NMD_ZAT on the Z80). In
 * the lockstep's live hybrid (NMD_LIVE_AT) that lets the C pause exactly where
 * the original was interrupted, or (Z80) where its run slice ended;
 * everywhere else it is nothing. */
#ifdef NMD_LIVE_AT
void nmd_live_at(uint32_t address);
void nmd_live_zat(uint16_t address);
#define NMD_AT(address) nmd_live_at(address)
#define NMD_ZAT(address) nmd_live_zat(address)
#else
#define NMD_AT(address) ((void)0)
#define NMD_ZAT(address) ((void)0)
#endif

/* An exception the original takes inside a call (TRAP #n at `address`). In the
 * lockstep's live hybrid the C stops there, the machine takes the exception
 * and runs its handler as the original, and the C goes on with the registers
 * the handler left: NMD_EXCEPTION_TAKEN is then 1. Everywhere else it is 0
 * and the C must stand in for the handler itself. */
#ifdef NMD_LIVE_AT
int nmd_live_exception(uint32_t address);
#define NMD_EXCEPTION_TAKEN(address) nmd_live_exception(address)
#else
#define NMD_EXCEPTION_TAKEN(address) ((void)(address), 0)
#endif

/* ---- a routine that has no C yet ---- */

#ifdef NMD_HYBRID
/* The checker's record stands in for the original routine; counted, and a
 * call that needed it is never IDENTICAL. Not available in a native build:
 * there every routine reached has C. */
void nmd_check_call_fallback(int cpu, uint32_t address);
#define NMD_JSR_ORIGINAL(return_address, address) \
    (nmd_push32(return_address), nmd_check_call_fallback(1, (address)))
#endif

#endif
