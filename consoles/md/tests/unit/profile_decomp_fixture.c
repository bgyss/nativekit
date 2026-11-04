/* Minimal native consumer used to exercise nmd-profile's --decomp handoff. */
#include <stdint.h>

#include <nmd/nmd.h>

static const uint8_t rom[0x400] = { 0 };

int main(int argc, char **argv)
{
    const nmd_cart cart = {
        .title = "profile count fixture",
        .region = NMD_NTSC,
        .rom = rom,
        .rom_size = sizeof rom,
    };
    return nmd_main(&cart, argc, argv);
}
