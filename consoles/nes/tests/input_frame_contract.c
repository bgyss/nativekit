#include "nnes/machine.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

#define OK(call) CHECK((call) == NNES_OK)

typedef struct {
    uint64_t frame_numbers[4];
    unsigned calls;
} input_state;

static void poll_frame(void *user, uint64_t frame_number, uint8_t buttons[2])
{
    input_state *state = user;
    CHECK(state != NULL && buttons != NULL && state->calls < 4);
    state->frame_numbers[state->calls++] = frame_number;
    switch (frame_number) {
    case 1:
        buttons[0] = NNES_A | NNES_START;
        buttons[1] = NNES_B;
        break;
    case 2:
        buttons[0] = NNES_LEFT;
        buttons[1] = NNES_SELECT | NNES_START;
        break;
    case 3:
        buttons[0] = NNES_RIGHT | NNES_B;
        buttons[1] = 0;
        break;
    default:
        buttons[0] = 0;
        buttons[1] = 0;
        break;
    }
}

static uint8_t read_bit(nnes *console, unsigned port)
{
    uint8_t value = 0;
    OK(nnes_read(console, (uint16_t)(0x4016 + port), &value));
    return value & 1;
}

static void latch_pads(nnes *console)
{
    OK(nnes_write(console, 0x4016, 1));
    OK(nnes_write(console, 0x4016, 0));
}

static uint64_t cycles_until_next_input_poll(nnes *console)
{
    const uint64_t frame_dots = 341u * 262u;
    const uint64_t input_poll_dot = 241u * 341u;
    uint64_t dots = (nnes_cpu_cycles(console) * 3) % frame_dots;
    uint64_t until_poll = dots < input_poll_dot
        ? input_poll_dot - dots
        : frame_dots - dots + input_poll_dot;
    return (until_poll + 2) / 3;
}

int main(void)
{
    nnes *console = NULL;
    input_state state = {0};
    OK(nnes_create(NULL, &console));
    OK(nnes_set_input_frame(console, poll_frame, &state));
    CHECK(state.calls == 1 && state.frame_numbers[0] == 1);

    /* The provider supplies frame 1 once; repeated controller strobes retain it. */
    latch_pads(console);
    CHECK(read_bit(console, 0) == 1); /* A */
    CHECK(read_bit(console, 0) == 0); /* B */
    CHECK(read_bit(console, 0) == 0); /* Select */
    CHECK(read_bit(console, 0) == 1); /* Start */
    CHECK(read_bit(console, 1) == 0); /* A */
    CHECK(read_bit(console, 1) == 1); /* B */
    latch_pads(console);
    CHECK(state.calls == 1);

    /* Frame 2 is sampled at the NTSC input poll scanline, before frame wrap. */
    uint64_t to_next_input_poll = cycles_until_next_input_poll(console);
    CHECK(to_next_input_poll > 1);
    OK(nnes_charge_cpu_cycles(console, to_next_input_poll - 1));
    CHECK(state.calls == 1);
    OK(nnes_charge_cpu_cycles(console, 1));
    CHECK(state.calls == 2 && state.frame_numbers[1] == 2);
    latch_pads(console);
    for (unsigned i = 0; i < 6; ++i) CHECK(read_bit(console, 0) == 0);
    CHECK(read_bit(console, 0) == 1); /* Left */
    CHECK(read_bit(console, 0) == 0); /* Right */
    CHECK(read_bit(console, 1) == 0); /* A */
    CHECK(read_bit(console, 1) == 0); /* B */
    CHECK(read_bit(console, 1) == 1); /* Select */
    CHECK(read_bit(console, 1) == 1); /* Start */

    OK(nnes_charge_cpu_cycles(console, cycles_until_next_input_poll(console)));
    CHECK(state.calls == 3 && state.frame_numbers[2] == 3);
    latch_pads(console);
    CHECK(read_bit(console, 0) == 0); /* A */
    CHECK(read_bit(console, 0) == 1); /* B */
    nnes_destroy(console);
    puts("NES host input-frame contract passed");
    return 0;
}
