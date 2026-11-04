/* The raw host masks are kept separate from the controller protocol state. */
#include "io.h"

#include "nmd/cart.h"
#include "nmd/hw.h"

typedef struct {
    uint8_t data;
    uint8_t control;
} md_io_port;

static md_io_port ports[2];
static nmd_port_type pad_types[2];
static uint32_t button_masks[2];

static uint8_t md_io_pad_pins(int port, int th)
{
    uint32_t buttons = button_masks[port];
    uint8_t pins = th ? 0x3Fu : 0x33u;

    switch (pad_types[port]) {
    case NMD_PORT_PAD3:
    case NMD_PORT_PAD6:
        break; /* Both pad types share these ordinary phases. */
    default:
        return 0x3Fu;
    }

    if (th) {
        if (buttons & NMD_BUTTON_C)
            pins &= (uint8_t)~0x20u;
        if (buttons & NMD_BUTTON_B)
            pins &= (uint8_t)~0x10u;
        if (buttons & NMD_BUTTON_RIGHT)
            pins &= (uint8_t)~0x08u;
        if (buttons & NMD_BUTTON_LEFT)
            pins &= (uint8_t)~0x04u;
    } else {
        if (buttons & NMD_BUTTON_START)
            pins &= (uint8_t)~0x20u;
        if (buttons & NMD_BUTTON_A)
            pins &= (uint8_t)~0x10u;
    }
    if (buttons & NMD_BUTTON_DOWN)
        pins &= (uint8_t)~0x02u;
    if (buttons & NMD_BUTTON_UP)
        pins &= (uint8_t)~0x01u;

    return pins;
}

static uint8_t md_io_data_read(int port)
{
    const md_io_port *state = &ports[port];
    uint8_t th = (state->control & 0x40u) ? (state->data & 0x40u) : 0x40u;
    uint8_t pad = md_io_pad_pins(port, th != 0);
    uint8_t value = (uint8_t)((state->data & state->control & 0x3Fu) |
                              (pad & (uint8_t)~state->control & 0x3Fu));

    if (state->control & 0x40u)
        value |= state->data & 0x40u;
    else
        value |= 0x40u; /* TH has a weak pull-up while configured as input. */
    value |= state->data & 0x80u;
    return value;
}

void md_io_reset(const nmd_cart *cart)
{
    int port;

    for (port = 0; port < 2; port++) {
        ports[port].data = 0;
        ports[port].control = 0;
        button_masks[port] = 0;
        if (cart && cart->ports[port] == NMD_PORT_PAD3)
            pad_types[port] = NMD_PORT_PAD3;
        else
            pad_types[port] = NMD_PORT_PAD6;
    }
}

void md_io_input(const uint32_t *buttons)
{
    if (!buttons) {
        button_masks[0] = 0;
        button_masks[1] = 0;
        return;
    }

    button_masks[0] = buttons[0];
    button_masks[1] = buttons[1];
}

uint32_t md_io_button_mask(int port)
{
    if (port < 0 || port >= 2)
        return 0;
    return button_masks[port];
}

uint8_t md_io_read8(uint32_t addr)
{
    uint32_t reg = addr & 0x1Fu;

    if (!(addr & 1u))
        return 0xFFu; /* The I/O devices are connected to the low byte lane. */

    switch (reg) {
    case 0x01u:
        return 0; /* md1va3 version; the TMSS model is handled in Task 4. */
    case 0x03u:
        return md_io_data_read(0);
    case 0x05u:
        return md_io_data_read(1);
    case 0x09u:
        return ports[0].control;
    case 0x0Bu:
        return ports[1].control;
    default:
        return 0xFFu;
    }
}

uint16_t md_io_read16(uint32_t addr)
{
    return md_io_read8((addr & ~1u) + 1u);
}

void md_io_write8(uint32_t addr, uint8_t value)
{
    uint32_t reg = addr & 0x1Fu;

    if (!(addr & 1u))
        return; /* The high byte lane is not connected to these registers. */

    switch (reg) {
    case 0x03u:
        ports[0].data = value;
        break;
    case 0x05u:
        ports[1].data = value;
        break;
    case 0x09u:
        ports[0].control = value;
        break;
    case 0x0Bu:
        ports[1].control = value;
        break;
    default:
        break;
    }
}

void md_io_write16(uint32_t addr, uint16_t value)
{
    md_io_write8((addr & ~1u) + 1u, (uint8_t)value);
}
