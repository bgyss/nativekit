/* Public I/O configuration and the host-to-pad input seam. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <nmd/nmd.h>

#include "io.h"
#include "md_internal.h"
#include "md_console.h"

static nmd_cart pad3_cart(void)
{
    nmd_cart cart = { 0 };

    cart.region = NMD_NTSC;
    cart.ports[0] = NMD_PORT_PAD3;
    cart.ports[1] = NMD_PORT_PAD3;
    return cart;
}

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

static void test_io_3button_phase_reads(void)
{
    nmd_cart cart = pad3_cart();
    const uint32_t buttons[2] = {
        NMD_BUTTON_UP | NMD_BUTTON_LEFT | NMD_BUTTON_B | NMD_BUTTON_C |
            NMD_BUTTON_A | NMD_BUTTON_START,
        0,
    };

    nmd_power(&cart);
    assert(nmd_bus_read8(0xA10003u) == 0x7Fu);
    md_io_input(buttons);

    nmd_bus_write8(0xA10009u, 0x40u);
    nmd_bus_write8(0xA10003u, 0x40u);
    assert(nmd_bus_read8(0xA10003u) == 0x4Au);

    nmd_bus_write8(0xA10003u, 0x00u);
    assert(nmd_bus_read8(0xA10003u) == 0x02u);
}

static void test_io_3button_ctrl_driven_output(void)
{
    nmd_cart cart = pad3_cart();
    uint64_t before;

    nmd_power(&cart);
    nmd_bus_write8(0xA10009u, 0x43u);
    nmd_bus_write8(0xA10003u, 0x01u);
    assert(nmd_bus_read8(0xA10009u) == 0x43u);
    assert(nmd_bus_read8(0xA10003u) == 0x31u);

    before = md_clock;
    nmd_bus_write16(0xA10008u, 0xAA40u);
    assert(md_clock - before == 28u);
    assert(nmd_bus_read8(0xA10009u) == 0x40u);

    before = md_clock;
    nmd_bus_write8(0xA10008u, 0x03u);
    assert(md_clock - before == 28u);
    assert(nmd_bus_read8(0xA10009u) == 0x40u);
}

static void test_io_port_state_independent(void)
{
    nmd_cart cart = pad3_cart();
    const uint32_t buttons[2] = { NMD_BUTTON_A, NMD_BUTTON_B };
    uint64_t before;
    uint16_t port0;

    nmd_power(&cart);
    md_io_input(buttons);
    nmd_bus_write16(0xA10008u, 0x1140u);
    nmd_bus_write16(0xA10002u, 0x2200u);
    nmd_bus_write16(0xA1000Au, 0x3300u);
    nmd_bus_write16(0xA10004u, 0x4440u);

    assert(nmd_bus_read8(0xA10009u) == 0x40u);
    assert(nmd_bus_read8(0xA1000Bu) == 0x00u);
    before = md_clock;
    port0 = nmd_bus_read16(0xA10002u);
    assert(md_clock - before == 28u);
    assert((port0 & 0x00FFu) == 0x23u);
    assert((nmd_bus_read16(0xA10004u) & 0x00FFu) == 0x6Fu);
}

int main(void)
{
    test_nmd_cart_zero_defaults();
    test_nmd_console_button_map();
    test_nmd_console_input_forwards_players();
    test_io_3button_phase_reads();
    test_io_3button_ctrl_driven_output();
    test_io_port_state_independent();
    return 0;
}
