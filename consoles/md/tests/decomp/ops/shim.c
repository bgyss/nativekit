/*
 * shim.c: nmd_decomp.h's flag helpers behind two plain functions, so
 * ops_check.py can call each with the operands the reference core had. The
 * bus calls are never reached. PLANT_ macros are the check's planted errors.
 */
#include "nmd_decomp.h"

nmd_cpu_state nmd_cpu;
nmd_z80_state nmd_z80;

uint8_t rd8(uint32_t addr) { (void)addr; return 0; }
uint16_t rd16(uint32_t addr) { (void)addr; return 0; }
uint32_t rd32(uint32_t addr) { (void)addr; return 0; }
void wr8(uint32_t addr, uint8_t value) { (void)addr; (void)value; }
void wr16(uint32_t addr, uint16_t value) { (void)addr; (void)value; }
void wr32(uint32_t addr, uint32_t value) { (void)addr; (void)value; }
uint8_t zrd(uint16_t addr) { (void)addr; return 0; }
void zwr(uint16_t addr, uint8_t value) { (void)addr; (void)value; }

enum { OP_ADD, OP_SUB, OP_CMP, OP_TST, OP_NEG, OP_LSL, OP_ASL, OP_LSR, OP_ASR, OP_ROL, OP_ROR,
       OP_SWAP, OP_BTST, OP_DIVS, OP_MULU, OP_MULS, OP_DIVU, OP_ADDX, OP_SUBX, OP_ABCD };

#define SIZED(bits, type)                                                           \
    static uint32_t m68k##bits(int op, uint32_t destination, uint32_t source)       \
    {                                                                               \
        type d = (type)destination, s = (type)source;                               \
        switch (op) {                                                               \
        case OP_ADD: return nmd_add##bits(d, s);                                    \
        case OP_SUB: return nmd_sub##bits(d, s);                                    \
        case OP_CMP: nmd_cmp##bits(d, s); return d;                                 \
        case OP_TST: return nmd_tst##bits(d);                                       \
        case OP_NEG: return nmd_neg##bits(d);                                       \
        case OP_LSL: return nmd_shl##bits(d, source, 0);                            \
        case OP_ASL: return nmd_shl##bits(d, source, 1);                            \
        case OP_LSR: return nmd_shr##bits(d, source, 0);                            \
        case OP_ROL: return nmd_rol##bits(d, source, 0);                            \
        case OP_ROR: return nmd_rol##bits(d, source, 1);                            \
        case OP_ADDX: return nmd_addx##bits(d, s);                                  \
        case OP_SUBX: return nmd_subx##bits(d, s);                                  \
        default: return nmd_shr##bits(d, source, 1);                                \
        }                                                                           \
    }
SIZED(8, uint8_t)
SIZED(16, uint16_t)
SIZED(32, uint32_t)

/* The destination register after the operation; *sr in and out. */
uint32_t ops_m68k(int op, int bits, uint32_t destination, uint32_t source, uint16_t *sr)
{
    uint32_t result, mask = bits == 32 ? 0xFFFFFFFFu : (1u << bits) - 1;

    nmd_cpu.sr = *sr;
    if (op == OP_BTST) {
        nmd_btst(destination, source & 31);
        result = destination;
    } else if (op == OP_SWAP) {
        result = nmd_swap(destination);
    } else if (op == OP_ABCD) {
        result = (destination & ~0xFFu) | nmd_abcd((uint8_t)destination, (uint8_t)source);
    } else if (op == OP_DIVS || op == OP_DIVU || op == OP_MULU || op == OP_MULS) {
        result = destination;
        if (op == OP_DIVS)
            nmd_divs(&result, (uint16_t)source);
        else if (op == OP_DIVU)
            nmd_divu(&result, (uint16_t)source);
        else if (op == OP_MULU)
            nmd_mulu(&result, (uint16_t)source);
        else
            nmd_muls(&result, (uint16_t)source);
    } else {
        result = bits == 8 ? m68k8(op, destination, source)
               : bits == 16 ? m68k16(op, destination, source) : m68k32(op, destination, source);
        result = (destination & ~mask) | (result & mask);
    }
#ifdef PLANT_M68K
    if (op == OP_SUB)
        nmd_cpu.sr ^= NMD_V;
#endif
    *sr = nmd_cpu.sr;
    return result;
}

enum { Z_ADD, Z_ADC, Z_SUB, Z_SBC, Z_CP, Z_AND, Z_OR, Z_XOR, Z_INC, Z_DEC };

/* A after the operation; *f in and out. */
uint8_t ops_z80(int op, uint8_t a, uint8_t b, uint8_t *f)
{
    unsigned carry = *f & NMD_ZC;

    nmd_z80.f = *f;
    switch (op) {
    case Z_ADD: a = nmd_z_add8(a, b, 0); break;
    case Z_ADC: a = nmd_z_add8(a, b, carry); break;
    case Z_SUB: a = nmd_z_sub8(a, b, 0); break;
    case Z_SBC: a = nmd_z_sub8(a, b, carry); break;
    case Z_CP: nmd_z_cp8(a, b); break;
    case Z_AND: a = nmd_z_and8(a, b); break;
    case Z_OR: a = nmd_z_or8(a | b); break;
    case Z_XOR: a = nmd_z_or8(a ^ b); break;
    case Z_INC: a = nmd_z_inc8(a); break;
    default: a = nmd_z_dec8(a); break;
    }
#ifdef PLANT_Z80
    if (op == Z_CP)
        nmd_z80.f ^= NMD_ZH;
#endif
    *f = nmd_z80.f;
    return a;
}
