/* consumer: the smallest game that links nshvc from outside the tree */
#include "nshvc/nshvc.h"
#include "nshvc/machine.h"

static void abi_reset(void)
{
    for (;;)
        cpu_wai(0x008000);
}

static uint8_t rom[0x8000];
static const nshvc_routine routines[] = { { 0x008000, abi_reset, "Reset", 0 } };

NSHVC_MAIN(argc, argv)
{
    static nshvc_cart cart;
    rom[0x7FFC] = 0x00;
    rom[0x7FFD] = 0x80;
    cart.title = "Consumer";
    cart.map = NSHVC_LOROM;
    cart.rom = rom;
    cart.rom_size = sizeof rom;
    cart.routines = routines;
    cart.routine_count = 1;
    return nshvc_main(&cart, argc, argv);
}
