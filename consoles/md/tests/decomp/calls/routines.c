/*
 * routines.c: authored C for the routines of the per-call control cartridge
 * (calls_fixture.py). Each routine is written over the nmd bus calls and
 * nmd_decomp.h as a game's recovered C is. The PLANT_ macros are the planted
 * errors calls_check.py builds one at a time. Nothing here comes from a game.
 */
#include <stddef.h>

#include "nmd/cart.h"
#include "nmd_decomp.h"

#define D nmd_cpu.d
#define A nmd_cpu.a

/* $400 sum: d0.w words at (a0) summed into d0.w; a0 advanced; d1.w = $FFFF. */
static void sum(void)
{
    uint16_t count = nmd_sub16((uint16_t)D[0], 1);      /* subq.w #1,d0 */

    nmd_set16(&D[0], count);
    nmd_set16(&D[1], nmd_tst16(count));                 /* move.w d0,d1 */
    D[0] = nmd_tst32(0);                                /* moveq #0,d0 */
    do {
#ifdef PLANT_LOAD
        uint16_t word = rd16(A[0] + 2);
#else
        uint16_t word = rd16(A[0]);
#endif
        A[0] += 2;
#ifdef PLANT_FLAGS
        nmd_set16(&D[0], (uint16_t)(D[0] + word));      /* the sum without its flags */
#else
        nmd_set16(&D[0], nmd_add16((uint16_t)D[0], word));
#endif
        nmd_set16(&D[1], (uint16_t)(D[1] - 1));         /* dbf d1 */
    } while ((uint16_t)D[1] != 0xFFFF);
    nmd_rts();
}

/* $420 poke: set VDP register 15 to d0's low byte, then read the status. */
static void poke(void)
{
    nmd_set16(&D[0], nmd_tst16((uint16_t)(D[0] | 0x8F00)));     /* or.w #$8F00,d0 */
#ifdef PLANT_ORDER
    nmd_set16(&D[1], nmd_tst16(rd16(0xC00004)));    /* the status before the register */
    wr16(0xC00004, nmd_tst16((uint16_t)D[0]));
#else
#ifdef PLANT_STORE
    wr16(0xC00004, (uint16_t)(D[0] ^ 1));
#else
    wr16(0xC00004, nmd_tst16((uint16_t)D[0]));
#endif
    nmd_set16(&D[1], nmd_tst16(rd16(0xC00004)));
#endif
    nmd_set16(&D[1], nmd_tst16((uint16_t)(D[1] & 0x0200)));
    nmd_rts();
}

/* $480.. the four handlers: each stores its own word at $FF0020. */
#define HANDLER(name, value)                    \
    static void name(void)                      \
    {                                           \
        wr16(0xFF0020, nmd_tst16(value));       \
        nmd_rts();                              \
    }
HANDLER(handler0, 0x1111)
HANDLER(handler1, 0x2222)
HANDLER(handler2, 0x3333)
HANDLER(handler3, 0x4444)

/* $440 dispatch: call handler d0.w (0..3) through the table at $460. */
static void dispatch(void)
{
    static void (*const handlers[4])(void) = {handler0, handler1, handler2, handler3};
    uint16_t index;

    nmd_set16(&D[0], nmd_add16((uint16_t)D[0], (uint16_t)D[0]));
    nmd_set16(&D[0], nmd_add16((uint16_t)D[0], (uint16_t)D[0]));
    index = (uint16_t)D[0];
    A[1] = 0x460;
    A[1] = rd32(A[1] + index);
#ifdef PLANT_TARGET
    NMD_JSR(0x44E, handlers[(index / 4 + 1) & 3]);
#else
    NMD_JSR(0x44E, handlers[index / 4]);
#endif
    nmd_rts();
}

/* $4C0 outer: sum two words, then three more, store the total at $FF0030. */
static void outer(void)
{
    D[0] = nmd_tst32(2);
    NMD_JSR(0x4C6, sum);
    nmd_set16(&D[2], nmd_tst16((uint16_t)D[0]));
    D[0] = nmd_tst32(3);
    NMD_JSR(0x4CE, sum);
    nmd_set16(&D[0], nmd_add16((uint16_t)D[0], (uint16_t)D[2]));
#ifdef PLANT_MEMORY
    wr16(0xFF0032, nmd_tst16((uint16_t)D[0]));
#else
    wr16(0xFF0030, nmd_tst16((uint16_t)D[0]));
#endif
#ifdef PLANT_RETURN
    A[7] += 4;      /* leaves without reading the return address */
#else
    nmd_rts();
#endif
}

/* $4E0 send: take the Z80 bus, store d0's low byte at Z80 $1F00, release. */
static void send(void)
{
    wr16(0xA11100, nmd_tst16(0x0100));
    do {
        nmd_btst(rd8(0xA11100), 0);
    } while (!(nmd_cpu.sr & NMD_Z));
    wr8(0xA01F00, nmd_tst8((uint8_t)D[0]));
    wr16(0xA11100, nmd_tst16(0));
    nmd_rts();
}

/* $510 spin: count 8,000 times at $FF0050 (long enough for an interrupt). */
static void spin(void)
{
    nmd_set16(&D[1], nmd_tst16(7999));
    do {
#ifdef PLANT_SPIN
        wr16(0xFF0050, nmd_add16(rd16(0xFF0050), 2));
#else
        wr16(0xFF0050, nmd_add16(rd16(0xFF0050), 1));
#endif
        nmd_set16(&D[1], (uint16_t)(D[1] - 1));
    } while ((uint16_t)D[1] != 0xFFFF);
    nmd_rts();
}

/* $530 unused: has C, is never called. */
static void unused(void)
{
    D[7] = nmd_tst32(99);
    nmd_rts();
}

/* $580 pad: make TH an output, raise it, store what pad 1 answers at $FF0070. */
static void pad(void)
{
    wr8(0xA10009, nmd_tst8(0x40));
    wr8(0xA10003, nmd_tst8(0x40));
    nmd_set8(&D[0], nmd_tst8(rd8(0xA10003)));
    wr8(0xFF0070, nmd_tst8((uint8_t)D[0]));
    nmd_rts();
}

/* $5A0 tone: PSG channel 0 at a fixed pitch, full volume. */
static void tone(void)
{
    wr8(0xC00011, nmd_tst8(0x8E));
    wr8(0xC00011, nmd_tst8(0x0F));
#ifdef PLANT_SAMPLE
    wr8(0xC00011, nmd_tst8(0x9F));      /* silent */
#else
    wr8(0xC00011, nmd_tst8(0x90));
#endif
    nmd_rts();
}

/* $5C0 irqon: enable the vertical interrupt in the VDP and in SR. */
static void irqon(void)
{
    wr16(0xC00004, nmd_tst16(0x8164));
#ifdef PLANT_IRQ
    nmd_set_sr(0x2700);                 /* the mask stays up */
#else
    nmd_set_sr(0x2000);
#endif
    nmd_rts();
}

/* $5D0 count: count D5.W down to zero; the loop branches back to the entry. */
static void count(void)
{
    do
        nmd_set16(&D[5], nmd_sub16((uint16_t)D[5], 1));
    while (!(nmd_cpu.sr & NMD_Z));
    nmd_rts();
}

/* $7E2 below: returns at once when D5.W is zero (through the RTS at $7E0, below
 * the entry); else sets D6 to 1. */
static void below(void)
{
    nmd_tst16((uint16_t)D[5]);
    if (!(nmd_cpu.sr & NMD_Z))
        D[6] = nmd_tst32(1);
    nmd_rts();
}

/* $5E0 dma: copy eight words from $FF0100 to VRAM $0000. */
static void dma(void)
{
    static const uint16_t words[9] = {0x8114, 0x8F02,
#ifdef PLANT_DMA
                                      0x9304,       /* half the length */
#else
                                      0x9308,
#endif
                                      0x9400, 0x9580, 0x9680, 0x977F, 0x4000, 0x0080};

    A[5] = 0xC00004;
    for (int index = 0; index < 9; index++)
        wr16(A[5], nmd_tst16(words[index]));
    nmd_rts();
}

/* $550 relay: calls a routine that has no C here (the record stands in). */
#ifdef NMD_HYBRID
static void relay(void)
{
    NMD_JSR_ORIGINAL(0x554, 0x560);
    nmd_set16(&D[3], nmd_add16((uint16_t)D[3], 1));
    nmd_rts();
}
#endif

/* Z80 $0020 fetch: add the byte the 68000 sent to the total at $1F01, show
 * it to the YM address port and read the status. */
static void z_fetch(void)
{
    nmd_z80.a = zrd(0x1F00);
    nmd_z80.h = 0x1F;
    nmd_z80.l = 0x01;
    nmd_z80.a = nmd_z_add8(nmd_z80.a, zrd(0x1F01), 0);
    zwr(0x1F01, nmd_z80.a);
    zwr(0x4000, nmd_z80.a);
    nmd_z80.a = zrd(0x4000);
    nmd_z80.a = nmd_z_and8(nmd_z80.a, 0x80);
    nmd_z80_ret();
}

const nmd_routine nmd_game_routines[] = {
    {0x400, sum, "sum"},
    {0x420, poke, "poke"},
    {0x440, dispatch, "dispatch"},
    {0x480, handler0, "handler0"},
    {0x48A, handler1, "handler1"},
    {0x494, handler2, "handler2"},
    {0x49E, handler3, "handler3"},
    {0x4C0, outer, "outer"},
    {0x4E0, send, "send"},
    {0x510, spin, "spin"},
    {0x530, unused, "unused"},
    {0x580, pad, "pad"},
    {0x5A0, tone, "tone"},
    {0x5C0, irqon, "irqon"},
    {0x5D0, count, "count"},
    {0x5E0, dma, "dma"},
    {0x7E2, below, "below"},
#ifdef NMD_HYBRID
    {0x550, relay, "relay"},
#endif
};
const size_t nmd_game_routine_count = sizeof nmd_game_routines / sizeof nmd_game_routines[0];

const nmd_z80_routine nmd_game_z80_routines[] = {
    {0x0020, z_fetch, "z_fetch"},
};
const size_t nmd_game_z80_routine_count =
    sizeof nmd_game_z80_routines / sizeof nmd_game_z80_routines[0];
