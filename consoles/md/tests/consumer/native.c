/*
 * A synthetic game with native execution: its reset routine, main loop,
 * vertical interrupt and Z80 sound program are C over nmd's public calls,
 * entered through nmd_main from the ROM's reset vector. Authored for the
 * consumer check (run.py); it is not recovered from any cartridge.
 *
 * Work RAM, read by the check from a script's dump:
 *   $0000 long  'NMD2' once the reset routine has run
 *   $0004 word  main-loop frames
 *   $0006 word  vertical interrupts taken
 *   $0008 byte  the pad as the last interrupt read it
 *   $0009 byte  set by the interrupt, cleared by main
 *   $000A long  a sum over every frame's pad (main's own state)
 *   $0010 word  commands main sent to the Z80
 *   $0100 ...   the pad at each interrupt, a byte each
 * Z80 RAM: $1F00 command, $1F01 its sequence number, $1F02 the sequence the
 * driver acknowledged, $1F03 commands played, $1F04 driver interrupts.
 *
 * NMD_NATIVE_PLANT plants an error the check must catch: skip-reset (the
 * cart asks for no native execution), lose-state (main restarts from the
 * reset routine every frame), bad-return (the interrupt masks its own
 * return's status register).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <nmd/nmd.h>

_Static_assert(NMD_VERSION_MAJOR == 1 && NMD_VERSION_MINOR >= 1,
               "native execution needs nativemd 1.1");

#define ENTRY 0x000200u
#define VINT 0x000300u
#define RAM 0xFF0000u
#define PAD1_DATA 0xA10003u
#define PAD1_CONTROL 0xA10009u
#define Z80_RAM 0xA00000u
#define Z80_BUSREQ 0xA11100u
#define Z80_RESET 0xA11200u

static uint8_t rom[0x400];
static int plant_lose_state;
static int plant_bad_return;

/* ---- 68000 ---- */

static int z80_bus_granted(void *ctx)
{
    (void)ctx;
    return (rd16(Z80_BUSREQ) & 0x0100u) == 0;
}

static void z80_command(uint8_t command)
{
    uint16_t sent = (uint16_t)(rd16(RAM + 0x10u) + 1u);
    wr16(Z80_BUSREQ, 0x0100);
    (void)nmd_wait_until(z80_bus_granted, NULL);
    wr8(Z80_RAM + 0x1F00u, command);
    wr8(Z80_RAM + 0x1F01u, (uint8_t)sent);
    wr16(Z80_BUSREQ, 0x0000);
    wr16(RAM + 0x10u, sent);
}

static uint8_t read_pad(void)
{
    uint8_t high, low;
    int i;
    wr8(PAD1_DATA, 0x40);
    for (i = 0; i < 4; i++) /* TH settles while the 68000 runs on */
        (void)rd8(PAD1_DATA);
    high = rd8(PAD1_DATA);
    wr8(PAD1_DATA, 0x00);
    for (i = 0; i < 4; i++)
        (void)rd8(PAD1_DATA);
    low = rd8(PAD1_DATA);
    return (uint8_t)((~high & 0x3Fu) | ((~low & 0x30u) << 2));
}

static void vint(void)
{
    uint16_t count = (uint16_t)(rd16(RAM + 6u) + 1u);
    uint8_t pad = read_pad();
    wr16(RAM + 6u, count);
    wr8(RAM + 8u, pad);
    wr8(RAM + 9u, 1);
    wr8(RAM + 0x100u + (count & 0xFFu), pad);
    if (plant_bad_return)
        wr16(nmd_cpu.a[7], (uint16_t)(rd16(nmd_cpu.a[7]) | 0x0700u));
}

static int vint_seen(void *ctx)
{
    (void)ctx;
    return rd8(RAM + 9u) != 0;
}

static void entry(void)
{
    uint16_t frames = 0; /* main's own state, kept across every frame's yield */
    uint32_t sum = 0;
    uint8_t last_pad = 0xFF;

    wr32(RAM, 0x4E4D4432u);
    wr8(PAD1_CONTROL, 0x40);

    /* the sound CPU: hold its bus, pulse its reset, let it run */
    wr16(Z80_BUSREQ, 0x0100);
    wr16(Z80_RESET, 0x0100);
    (void)nmd_wait_until(z80_bus_granted, NULL);
    wr8(Z80_RAM + 0x1F00u, 0);
    wr8(Z80_RAM + 0x1F01u, 0);
    wr16(Z80_RESET, 0x0000);
    wr16(Z80_RESET, 0x0100);
    wr16(Z80_BUSREQ, 0x0000);

    /* the VDP: mode 5, display and vertical interrupt on, backdrop colour 0 */
    wr16(NMD_VDP_CONTROL, 0x8004);
    wr16(NMD_VDP_CONTROL, 0x8164);
    wr16(NMD_VDP_CONTROL, 0x8700);
    wr16(NMD_VDP_CONTROL, 0x8F02);
    nmd_set_sr(0x2000);

    for (;;) {
        uint8_t pad;
        (void)nmd_wait_until(vint_seen, NULL);
        wr8(RAM + 9u, 0);
        pad = rd8(RAM + 8u);
        frames++;
        sum = sum * 31u + pad;
        wr16(RAM + 4u, frames);
        wr32(RAM + 0xAu, sum);
        if (pad != last_pad) {
            last_pad = pad;
            wr32(NMD_VDP_CONTROL, 0xC0000000u); /* CRAM 0: the backdrop follows the pad */
            wr16(NMD_VDP_DATA, (uint16_t)(0x0222u + ((pad & 0x07u) << 1) +
                                          ((pad & 0x38u) << 2) + ((pad & 0xC0u) << 3)));
            z80_command(pad);
        }
        if (plant_lose_state)
            nmd_restart(ENTRY);
    }
}

/* ---- Z80 ---- */

static int command_arrived(void *ctx)
{
    return zrd(0x1F01) != *(const uint8_t *)ctx;
}

static void z80_entry(void)
{
    uint8_t sequence = 0;
    uint8_t played = 0;

    nmd_z80.sp = 0x1FF0;
    nmd_z80.im = 1;
    zwr(0x1F02, 0);
    zwr(0x1F03, 0);
    zwr(0x1F04, 0);
    zwr(0x7F11, 0x9F); /* the PSG's four channels off */
    zwr(0x7F11, 0xBF);
    zwr(0x7F11, 0xDF);
    zwr(0x7F11, 0xFF);
    nmd_z80.iff1 = nmd_z80.iff2 = 1; /* EI */

    for (;;) {
        uint8_t command;
        uint16_t period;
        (void)nmd_z80_wait_until(command_arrived, &sequence);
        sequence = zrd(0x1F01);
        command = zrd(0x1F00);
        period = (uint16_t)(0x0080u + command);
        zwr(0x7F11, (uint8_t)(0x80u | (period & 0x0Fu)));
        zwr(0x7F11, (uint8_t)((period >> 4) & 0x3Fu));
        zwr(0x7F11, (uint8_t)(command ? 0x90u : 0x9Fu)); /* a pad's tone, or silence */
        zwr(0x1F02, sequence);
        zwr(0x1F03, ++played);
    }
}

static void z80_vint(void)
{
    zwr(0x1F04, (uint8_t)(zrd(0x1F04) + 1u));
    nmd_z80.iff1 = nmd_z80.iff2 = 1; /* EI before the return */
}

static const nmd_routine routines[] = {
    { ENTRY, entry, "entry" },
    { VINT, vint, "vint" },
};

static const nmd_z80_routine z80_routines[] = {
    { 0x0000, z80_entry, "z80 entry" },
    { 0x0038, z80_vint, "z80 vint" },
};

static void put32(uint32_t at, uint32_t value)
{
    rom[at + 0] = (uint8_t)(value >> 24);
    rom[at + 1] = (uint8_t)(value >> 16);
    rom[at + 2] = (uint8_t)(value >> 8);
    rom[at + 3] = (uint8_t)value;
}

int main(int argc, char **argv)
{
    const char *plant = getenv("NMD_NATIVE_PLANT");
    nmd_cart cart = {
        .title = "nmd native",
        .region = NMD_NTSC,
        .rom = rom,
        .rom_size = sizeof rom,
        .routines = routines,
        .routine_count = sizeof routines / sizeof routines[0],
        .z80_routines = z80_routines,
        .z80_routine_count = sizeof z80_routines / sizeof z80_routines[0],
        .model = NMD_MODEL_MD1VA3,
        .ports = { NMD_PORT_PAD3, NMD_PORT_PAD3 },
        .execution = NMD_EXECUTION_NATIVE,
    };

    put32(0x00, 0x00FFFE00u); /* initial supervisor stack */
    put32(0x04, ENTRY);       /* reset */
    put32(0x78, VINT);        /* level 6: vertical interrupt */
    if (plant && !strcmp(plant, "skip-reset"))
        cart.execution = NMD_EXECUTION_HOST;
    plant_lose_state = plant && !strcmp(plant, "lose-state");
    plant_bad_return = plant && !strcmp(plant, "bad-return");
    return nmd_main(&cart, argc, argv);
}
