/* Public I/O configuration and the host-to-pad input seam. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <nmd/nmd.h>

#include "io.h"
#include "md_console.h"

static void test_nmd_cart_zero_defaults(void)
{
    nmd_cart cart = { 0 };

    assert(cart.model == NMD_MODEL_MD1VA3);
    assert(cart.ports[0] == NMD_PORT_PAD6);
    assert(cart.ports[1] == NMD_PORT_PAD6);
    assert(cart.cost_profile == NULL);
}

static void test_nmd_console_button_map(void)
{
    static const struct {
        const char *name;
        uint32_t bit;
        uint32_t expected_bit;
    } expected[] = {
        { "UP", NMD_BUTTON_UP, 1u << 0 },
        { "DOWN", NMD_BUTTON_DOWN, 1u << 1 },
        { "LEFT", NMD_BUTTON_LEFT, 1u << 2 },
        { "RIGHT", NMD_BUTTON_RIGHT, 1u << 3 },
        { "B", NMD_BUTTON_B, 1u << 4 },
        { "C", NMD_BUTTON_C, 1u << 5 },
        { "A", NMD_BUTTON_A, 1u << 6 },
        { "START", NMD_BUTTON_START, 1u << 7 },
        { "X", NMD_BUTTON_X, 1u << 8 },
        { "Y", NMD_BUTTON_Y, 1u << 9 },
        { "Z", NMD_BUTTON_Z, 1u << 10 },
        { "MODE", NMD_BUTTON_MODE, 1u << 11 },
    };
    unsigned i;

    assert(nmd_console.players == 2);
    assert(nmd_console.button_count == (int)(sizeof expected / sizeof expected[0]));
    for (i = 0; i < sizeof expected / sizeof expected[0]; i++) {
        assert(expected[i].bit == expected[i].expected_bit);
        assert(strcmp(nmd_console.buttons[i].name, expected[i].name) == 0);
        assert(nmd_console.buttons[i].bit == expected[i].bit);
    }
}

static void test_nmd_console_input_forwards_players(void)
{
    const uint32_t buttons[2] = {
        NMD_BUTTON_A | NMD_BUTTON_START | NMD_BUTTON_LEFT,
        NMD_BUTTON_X | NMD_BUTTON_MODE | NMD_BUTTON_DOWN,
    };

    nmd_console.input(1, buttons);
    assert(md_io_button_mask(0) == buttons[0]);
    assert(md_io_button_mask(1) == buttons[1]);

    nmd_console.input(0, NULL);
    assert(md_io_button_mask(0) == 0);
    assert(md_io_button_mask(1) == 0);
}

int main(void)
{
    test_nmd_cart_zero_defaults();
    test_nmd_console_button_map();
    test_nmd_console_input_forwards_players();
    return 0;
}
