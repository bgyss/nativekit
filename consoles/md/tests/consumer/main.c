/* A game as games use nmd: its own CMake project links the native console. */
#include <stdint.h>

#include <nmd/cart.h>
#include <nmd/hw.h>
#include <nmd/machine.h>
#include <nmd/nmd.h>
#include <nmd/version.h>
#include <nmd/z80.h>

_Static_assert(NMD_VERSION_MAJOR == 1 && NMD_VERSION_MINOR == 1 && NMD_VERSION_PATCH == 0,
               "nativemd consumer contract version");

static const uint8_t rom[0x400] = { 0 };

int main(int argc, char **argv)
{
    const nmd_cart cart = {
        .title = "nmd consumer",
        .region = NMD_NTSC,
        .rom = rom,
        .rom_size = sizeof rom,
        .model = NMD_MODEL_MD1VA3,
        .ports = { NMD_PORT_PAD6, NMD_PORT_PAD6 },
    };
    return nmd_main(&cart, argc, argv);
}
