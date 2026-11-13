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

static nmd_cart pad6_cart(void)
{
    nmd_cart cart = { 0 };

    cart.region = NMD_NTSC;
    cart.ports[0] = NMD_PORT_PAD6;
    cart.ports[1] = NMD_PORT_PAD6;
    return cart;
}

static nmd_cart tmss_cart(nmd_region region)
{
    nmd_cart cart = pad6_cart();

    cart.region = region;
    cart.model = NMD_MODEL_MD1VA6_TMSS;
    return cart;
}

static void wait_for_pad_settle(void)
{
    md_advance_to(md_clock + 84u);
}

static void pad6_precharge_th_high(void)
{
    nmd_bus_write8(0xA10003u, 0x40u);
    nmd_bus_write8(0xA10009u, 0x40u);
    wait_for_pad_settle();
}

static void pad6_write_th(uint8_t high)
{
    nmd_bus_write8(0xA10003u, high ? 0x40u : 0x00u);
    wait_for_pad_settle();
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
    wait_for_pad_settle();
    assert(nmd_bus_read8(0xA10003u) == 0x4Au);

    nmd_bus_write8(0xA10003u, 0x00u);
    wait_for_pad_settle();
    assert(nmd_bus_read8(0xA10003u) == 0x02u);
}

static void test_io_3button_ctrl_driven_output(void)
{
    nmd_cart cart = pad3_cart();
    uint64_t before;

    nmd_power(&cart);
    nmd_bus_write8(0xA10009u, 0x43u);
    nmd_bus_write8(0xA10003u, 0x01u);
    wait_for_pad_settle();
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

static void test_io_pad_data_settles_after_th_change(void)
{
    nmd_cart cart = pad6_cart();
    const uint32_t buttons[2] = { NMD_BUTTON_C, 0 };

    nmd_power(&cart);
    md_io_input(buttons);
    pad6_precharge_th_high();
    assert((nmd_bus_read8(0xA10003u) & 0x20u) == 0);

    nmd_bus_write8(0xA10003u, 0x00u);
    assert((md_io_read8(0xA10003u) & 0x20u) == 0);
    md_advance_to(md_clock + 83u);
    assert((md_io_read8(0xA10003u) & 0x20u) == 0);
    md_advance_to(md_clock + 1u);
    assert((md_io_read8(0xA10003u) & 0x20u) != 0);

    pad6_write_th(1);
    pad6_write_th(0);
    nmd_bus_write8(0xA10003u, 0x40u);
    assert((md_io_read8(0xA10003u) & 0x3Fu) == 0x33u);
    md_advance_to(md_clock + 83u);
    assert((md_io_read8(0xA10003u) & 0x3Fu) == 0x33u);
    md_advance_to(md_clock + 1u);
    assert(md_io_read8(0xA10003u) == 0x5Fu);
}

static void test_io_6button_extended_phase(void)
{
    nmd_cart cart = pad6_cart();
    const uint32_t buttons[2] = {
        NMD_BUTTON_MODE | NMD_BUTTON_X | NMD_BUTTON_Y | NMD_BUTTON_Z,
        NMD_BUTTON_A | NMD_BUTTON_B,
    };

    cart.ports[1] = NMD_PORT_PAD3;
    nmd_power(&cart);
    md_io_input(buttons);
    pad6_precharge_th_high();
    assert(nmd_bus_read8(0xA10003u) == 0x7Fu);
    assert(nmd_bus_read8(0xA10005u) == 0x6Fu);

    pad6_write_th(0);
    assert(nmd_bus_read8(0xA10003u) == 0x33u);
    pad6_write_th(1);
    assert(nmd_bus_read8(0xA10003u) == 0x7Fu);
    pad6_write_th(0);
    assert(nmd_bus_read8(0xA10003u) == 0x33u);
    pad6_write_th(1);
    assert(nmd_bus_read8(0xA10003u) == 0x7Fu);
    pad6_write_th(0);
    assert(nmd_bus_read8(0xA10003u) == 0x30u);
    pad6_write_th(1);
    assert(nmd_bus_read8(0xA10003u) == 0x70u);
    pad6_write_th(0);
    assert(nmd_bus_read8(0xA10003u) == 0x3Fu);
}

static uint8_t read_pad6_extended_button(uint32_t button)
{
    nmd_cart cart = pad6_cart();
    const uint32_t buttons[2] = { button, 0 };

    nmd_power(&cart);
    md_io_input(buttons);
    pad6_precharge_th_high();
    pad6_write_th(0);
    pad6_write_th(1);
    pad6_write_th(0);
    pad6_write_th(1);
    pad6_write_th(0);
    pad6_write_th(1);
    return nmd_bus_read8(0xA10003u);
}

static uint8_t read_pad3_low_button(uint32_t button)
{
    nmd_cart cart = pad3_cart();
    const uint32_t buttons[2] = { button, 0 };

    nmd_power(&cart);
    md_io_input(buttons);
    nmd_bus_write8(0xA10009u, 0x40u);
    nmd_bus_write8(0xA10003u, 0x00u);
    wait_for_pad_settle();
    return nmd_bus_read8(0xA10003u);
}

static uint8_t read_pad3_high_button(uint32_t button)
{
    nmd_cart cart = pad3_cart();
    const uint32_t buttons[2] = { button, 0 };

    nmd_power(&cart);
    md_io_input(buttons);
    nmd_bus_write8(0xA10009u, 0x40u);
    nmd_bus_write8(0xA10003u, 0x40u);
    wait_for_pad_settle();
    return nmd_bus_read8(0xA10003u);
}

static void test_io_button_bits_have_distinct_pad_lines(void)
{
    assert(read_pad3_high_button(NMD_BUTTON_UP) == 0x7Eu);
    assert(read_pad3_high_button(NMD_BUTTON_DOWN) == 0x7Du);
    assert(read_pad3_high_button(NMD_BUTTON_LEFT) == 0x7Bu);
    assert(read_pad3_high_button(NMD_BUTTON_RIGHT) == 0x77u);
    assert(read_pad3_high_button(NMD_BUTTON_B) == 0x6Fu);
    assert(read_pad3_high_button(NMD_BUTTON_C) == 0x5Fu);
    assert(read_pad6_extended_button(NMD_BUTTON_MODE) == 0x77u);
    assert(read_pad6_extended_button(NMD_BUTTON_X) == 0x7Bu);
    assert(read_pad6_extended_button(NMD_BUTTON_Y) == 0x7Du);
    assert(read_pad6_extended_button(NMD_BUTTON_Z) == 0x7Eu);
    assert(read_pad3_low_button(NMD_BUTTON_A) == 0x23u);
    assert(read_pad3_low_button(NMD_BUTTON_START) == 0x13u);
}

static void test_io_6button_timeout_resets(void)
{
    nmd_cart cart = pad6_cart();
    const uint32_t buttons[2] = { NMD_BUTTON_X, NMD_BUTTON_Y };
    uint64_t edge_clock;
    uint64_t timeout_clock;

    nmd_power(&cart);
    md_io_input(buttons);
    pad6_precharge_th_high();
    nmd_bus_write8(0xA10005u, 0x40u);
    nmd_bus_write8(0xA1000Bu, 0x40u);
    wait_for_pad_settle();
    pad6_write_th(0);
    pad6_write_th(1);
    pad6_write_th(0);
    pad6_write_th(1);
    pad6_write_th(0);
    edge_clock = md_clock;
    pad6_write_th(1);
    timeout_clock = edge_clock + 56000u;
    assert((nmd_bus_read8(0xA10003u) & 0x04u) == 0);
    assert((nmd_bus_read8(0xA10005u) & 0x02u) != 0);

    md_advance_to(timeout_clock - 300u);
    nmd_bus_write8(0xA10005u, 0x00u);
    wait_for_pad_settle();
    md_advance_to(timeout_clock);
    assert((nmd_bus_read8(0xA10003u) & 0x04u) != 0);
}

static void test_io_buttons_sampled_per_frame(void)
{
    nmd_cart cart = pad6_cart();
    const uint32_t first_frame[2] = { NMD_BUTTON_MODE | NMD_BUTTON_X, NMD_BUTTON_UP };
    const uint32_t next_frame[2] = { NMD_BUTTON_MODE | NMD_BUTTON_Y | NMD_BUTTON_Z,
                                     NMD_BUTTON_LEFT };

    nmd_power(&cart);
    nmd_console.input(1, first_frame);
    pad6_precharge_th_high();
    pad6_write_th(0);
    pad6_write_th(1);
    pad6_write_th(0);
    pad6_write_th(1);
    pad6_write_th(0);
    pad6_write_th(1);
    assert(nmd_bus_read8(0xA10003u) == 0x73u);
    assert(md_io_button_mask(0) == first_frame[0]);

    nmd_console.input(1, next_frame);
    assert(md_io_button_mask(0) == next_frame[0]);
    assert(md_io_button_mask(1) == next_frame[1]);
    assert(nmd_bus_read8(0xA10003u) == 0x74u);
}

static void test_io_version_register_model_bits(void)
{
    nmd_cart cart = pad3_cart();

    nmd_power(&cart);
    assert(nmd_bus_read8(0xA10001u) == 0xA0u);
    nmd_bus_write8(0xA10001u, 0xFFu); /* The version register is read-only. */
    assert(nmd_bus_read8(0xA10001u) == 0xA0u);

    cart.model = NMD_MODEL_MD1VA6_TMSS;
    nmd_power(&cart);
    assert(nmd_bus_read8(0xA10001u) == 0xA1u);

    cart.region = NMD_PAL;
    nmd_power(&cart);
    assert(nmd_bus_read8(0xA10001u) == 0xE1u);
}

static void test_io_tmss_absent_is_open(void)
{
    nmd_cart cart = pad3_cart();

    nmd_power(&cart);
    nmd_bus_write16(0xA14000u, 0x5345u);
    nmd_bus_write16(0xA14002u, 0x4741u);
    assert(nmd_bus_read16(0xA14000u) == 0xFFFFu);
    nmd_bus_write16(0xC00004u, 0x8104u);
}

static void test_io_tmss_lock_unlock_sequence(void)
{
    nmd_cart cart = tmss_cart(NMD_NTSC);

    nmd_power(&cart);
    nmd_bus_write8(0xA14000u, 0x53u);
    assert(nmd_bus_read16(0xA14000u) == 0x5300u);
    nmd_bus_write8(0xA14001u, 0x45u);
    assert(nmd_bus_read16(0xA14000u) == 0x5345u);
    nmd_bus_write8(0xA14002u, 0x47u);
    assert(nmd_bus_read16(0xA14002u) == 0x4700u);
    nmd_bus_write8(0xA14003u, 0x41u);
    assert(nmd_bus_read16(0xA14002u) == 0x4741u);
    nmd_bus_write16(0xC00004u, 0x8104u);

    nmd_power(&cart);
    wr32(0xA14000u, 0x53454741u);
    nmd_bus_write16(0xC00004u, 0x8104u);
}

static void test_io_tmss_vdp_locked(int partial_signature)
{
    nmd_cart cart = tmss_cart(NMD_NTSC);

    nmd_power(&cart);
    if (partial_signature)
        nmd_bus_write16(0xA14000u, 0x5345u);
    nmd_bus_write16(0xC00004u, 0x8104u);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "tmss-vdp-locked") == 0) {
        test_io_tmss_vdp_locked(0);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "tmss-vdp-partial") == 0) {
        test_io_tmss_vdp_locked(1);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "unsupported-io-read") == 0) {
        nmd_cart cart = pad3_cart();
        nmd_power(&cart);
        (void)nmd_bus_read8(0xA10011u);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "unsupported-io-write") == 0) {
        nmd_cart cart = pad3_cart();
        nmd_power(&cart);
        nmd_bus_write8(0xA10011u, 0x5Au);
        return 0;
    }
    if (argc != 1)
        return 2;

    test_nmd_cart_zero_defaults();
    test_nmd_console_button_map();
    test_nmd_console_input_forwards_players();
    test_io_3button_phase_reads();
    test_io_3button_ctrl_driven_output();
    test_io_port_state_independent();
    test_io_pad_data_settles_after_th_change();
    test_io_6button_extended_phase();
    test_io_button_bits_have_distinct_pad_lines();
    test_io_6button_timeout_resets();
    test_io_buttons_sampled_per_frame();
    test_io_version_register_model_bits();
    test_io_tmss_absent_is_open();
    test_io_tmss_lock_unlock_sequence();
    return 0;
}
