/* The raw host masks are kept separate from the controller protocol state. */
#include "io.h"

#include "../console/md_internal.h"
#include "nmd/cart.h"
#include "nmd/hw.h"

#define MD_IO_PAD_SETTLE_MCLK 112u /* Four 68000 NOPs at 7 master clocks per cycle. */
#define MD_IO_TH_TIMEOUT_MCLK 56000u /* Candidate pad reset interval in master clocks. */

typedef struct {
    uint8_t data;
    uint8_t control;
    uint8_t sampled_th;
    uint8_t pending_th;
    uint8_t settle_pending;
    uint8_t th_edges;
    uint8_t sampled_edges;
    uint64_t settle_clock;
    uint64_t timeout_clock;
} md_io_port;

static md_io_port ports[2];
static nmd_port_type pad_types[2];
static uint32_t button_masks[2];
static nmd_model machine_model;
static nmd_region machine_region;
static uint16_t tmss_signature[2];

static int md_io_tmss_present(void)
{
    return machine_model == NMD_MODEL_MD1VA6_TMSS;
}

int md_io_vdp_unlocked(void)
{
    return !md_io_tmss_present() ||
           (tmss_signature[0] == 0x5345u && tmss_signature[1] == 0x4741u);
}

static uint8_t md_io_version(void)
{
    uint8_t value = 0xA0u; /* Export NTSC hardware with no floppy unit. */

    if (machine_region == NMD_PAL)
        value |= 0x40u;
    if (md_io_tmss_present())
        value |= 0x01u;
    return value;
}

static uint8_t md_io_th_level(int port)
{
    const md_io_port *state = &ports[port];

    if (state->control & 0x40u)
        return (uint8_t)((state->data & 0x40u) != 0);
    return 1;
}

static void md_io_set_th(int port, uint8_t old_th)
{
    md_io_port *state = &ports[port];
    uint8_t new_th = md_io_th_level(port);

    if (old_th == new_th)
        return;

    state->pending_th = new_th;
    state->settle_clock = md_clock + MD_IO_PAD_SETTLE_MCLK;
    state->settle_pending = 1;
    if (pad_types[port] != NMD_PORT_PAD6)
        return;

    if (md_clock >= state->timeout_clock) {
        state->th_edges = 0;
        state->sampled_edges = 0;
    }
    if (new_th && state->th_edges < 4u)
        state->th_edges++;
    state->timeout_clock = md_clock + MD_IO_TH_TIMEOUT_MCLK;
}

static void md_io_update_timing(int port)
{
    md_io_port *state = &ports[port];

    if (state->settle_pending && md_clock >= state->settle_clock) {
        state->sampled_th = state->pending_th;
        state->sampled_edges = state->th_edges;
        state->settle_pending = 0;
    }
    if (pad_types[port] == NMD_PORT_PAD6 && md_clock >= state->timeout_clock) {
        state->th_edges = 0;
        state->sampled_edges = 0;
    }
}

static uint8_t md_io_pad_pins(int port, int th)
{
    uint32_t buttons = button_masks[port];
    const md_io_port *state = &ports[port];
    int extended = pad_types[port] == NMD_PORT_PAD6 && th && state->sampled_edges == 3u;
    int recognized = pad_types[port] == NMD_PORT_PAD6 && !th && state->sampled_edges == 2u;
    int after_extra = pad_types[port] == NMD_PORT_PAD6 && !th && state->sampled_edges == 3u;
    uint8_t pins;

    switch (pad_types[port]) {
    case NMD_PORT_PAD3:
    case NMD_PORT_PAD6:
        if (recognized)
            pins = 0x30u;
        else if (after_extra)
            pins = 0x3Fu;
        else
            pins = th ? 0x3Fu : 0x33u;
        break;
    default:
        return 0x3Fu;
    }

    if (th) {
        if (buttons & NMD_BUTTON_C)
            pins &= (uint8_t)~0x20u;
        if (buttons & NMD_BUTTON_B)
            pins &= (uint8_t)~0x10u;
        if (extended) {
            if (buttons & NMD_BUTTON_MODE)
                pins &= (uint8_t)~0x08u;
            if (buttons & NMD_BUTTON_X)
                pins &= (uint8_t)~0x04u;
            if (buttons & NMD_BUTTON_Y)
                pins &= (uint8_t)~0x02u;
            if (buttons & NMD_BUTTON_Z)
                pins &= (uint8_t)~0x01u;
        } else {
            if (buttons & NMD_BUTTON_RIGHT)
                pins &= (uint8_t)~0x08u;
            if (buttons & NMD_BUTTON_LEFT)
                pins &= (uint8_t)~0x04u;
            if (buttons & NMD_BUTTON_DOWN)
                pins &= (uint8_t)~0x02u;
            if (buttons & NMD_BUTTON_UP)
                pins &= (uint8_t)~0x01u;
        }
    } else {
        if (buttons & NMD_BUTTON_START)
            pins &= (uint8_t)~0x20u;
        if (buttons & NMD_BUTTON_A)
            pins &= (uint8_t)~0x10u;
        if (!recognized && !after_extra) {
            if (buttons & NMD_BUTTON_DOWN)
                pins &= (uint8_t)~0x02u;
            if (buttons & NMD_BUTTON_UP)
                pins &= (uint8_t)~0x01u;
        }
    }

    return pins;
}

static uint8_t md_io_data_read(int port)
{
    md_io_port *state = &ports[port];
    uint8_t pad;
    uint8_t value;

    md_io_update_timing(port);
    pad = md_io_pad_pins(port, state->sampled_th != 0);
    value = (uint8_t)((state->data & state->control & 0x3Fu) |
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

    machine_model = cart && cart->model == NMD_MODEL_MD1VA6_TMSS ?
        NMD_MODEL_MD1VA6_TMSS : NMD_MODEL_MD1VA3;
    machine_region = cart && cart->region == NMD_PAL ? NMD_PAL : NMD_NTSC;
    tmss_signature[0] = 0;
    tmss_signature[1] = 0;

    for (port = 0; port < 2; port++) {
        ports[port].data = 0;
        ports[port].control = 0;
        ports[port].sampled_th = 1;
        ports[port].pending_th = 1;
        ports[port].settle_pending = 0;
        ports[port].th_edges = 0;
        ports[port].sampled_edges = 0;
        ports[port].settle_clock = 0;
        ports[port].timeout_clock = 0;
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

    if (addr >= 0xA14000u && addr <= 0xA14003u) {
        uint16_t word;
        unsigned index;

        if (!md_io_tmss_present())
            return 0xFFu;
        index = (unsigned)((addr - 0xA14000u) >> 1);
        word = tmss_signature[index];
        return (addr & 1u) ? (uint8_t)word : (uint8_t)(word >> 8);
    }

    if (!(addr & 1u))
        return 0xFFu; /* The I/O devices are connected to the low byte lane. */

    switch (reg) {
    case 0x01u:
        return md_io_version();
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
    uint32_t aligned = addr & ~1u;

    if (aligned == 0xA14000u || aligned == 0xA14002u) {
        if (!md_io_tmss_present())
            return 0xFFFFu;
        return tmss_signature[(aligned - 0xA14000u) >> 1];
    }
    return md_io_read8((addr & ~1u) + 1u);
}

void md_io_write8(uint32_t addr, uint8_t value)
{
    uint32_t reg = addr & 0x1Fu;

    if (addr >= 0xA14000u && addr <= 0xA14003u) {
        if (md_io_tmss_present()) {
            unsigned index = (unsigned)((addr - 0xA14000u) >> 1);
            if (addr & 1u)
                tmss_signature[index] = (uint16_t)((tmss_signature[index] & 0xFF00u) | value);
            else
                tmss_signature[index] = (uint16_t)((tmss_signature[index] & 0x00FFu) |
                                                   (uint16_t)value << 8);
        }
        return;
    }

    if (!(addr & 1u))
        return; /* The high byte lane is not connected to these registers. */

    switch (reg) {
    case 0x03u: {
        uint8_t old_th;

        md_io_update_timing(0);
        old_th = md_io_th_level(0);
        ports[0].data = value;
        md_io_set_th(0, old_th);
        break;
    }
    case 0x05u: {
        uint8_t old_th;

        md_io_update_timing(1);
        old_th = md_io_th_level(1);
        ports[1].data = value;
        md_io_set_th(1, old_th);
        break;
    }
    case 0x09u: {
        uint8_t old_th;

        md_io_update_timing(0);
        old_th = md_io_th_level(0);
        ports[0].control = value;
        md_io_set_th(0, old_th);
        break;
    }
    case 0x0Bu: {
        uint8_t old_th;

        md_io_update_timing(1);
        old_th = md_io_th_level(1);
        ports[1].control = value;
        md_io_set_th(1, old_th);
        break;
    }
    default:
        break;
    }
}

void md_io_write16(uint32_t addr, uint16_t value)
{
    uint32_t aligned = addr & ~1u;

    if (aligned == 0xA14000u || aligned == 0xA14002u) {
        if (md_io_tmss_present())
            tmss_signature[(aligned - 0xA14000u) >> 1] = value;
        return;
    }
    md_io_write8(aligned + 1u, (uint8_t)value);
}
