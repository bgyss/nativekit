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
    uint64_t poll_numbers[4];
    unsigned calls;
    int fail_next;
} input_state;

static nnes_result poll_buttons(void *user, uint64_t poll_number,
                                uint8_t buttons[2])
{
    input_state *state = user;
    if (!state || !buttons || state->calls >= 4) return NNES_INVALID_ARGUMENT;
    state->poll_numbers[state->calls++] = poll_number;
    if (state->fail_next) {
        state->fail_next = 0;
        return NNES_UNSUPPORTED;
    }

    switch (poll_number) {
    case 1:
        buttons[0] = NNES_A | NNES_START;
        buttons[1] = NNES_B;
        return NNES_OK;
    case 2:
        buttons[0] = NNES_LEFT;
        buttons[1] = NNES_SELECT | NNES_START;
        return NNES_OK;
    case 3:
        buttons[0] = NNES_A;
        buttons[1] = NNES_RIGHT;
        return NNES_OK;
    default:
        return NNES_INVALID_ARGUMENT;
    }
}

static uint8_t read_bit(nnes *console, unsigned port)
{
    uint8_t value = 0;
    OK(nnes_read(console, (uint16_t)(0x4016 + port), &value));
    return value & 1;
}

int main(void)
{
    nnes *console = NULL;
    input_state state = {0};
    OK(nnes_create(NULL, &console));
    OK(nnes_set_input_poll(console, poll_buttons, &state));

    /* A falling strobe edge samples both host pads before serial reads. */
    OK(nnes_write(console, 0x4016, 1));
    OK(nnes_write(console, 0x4016, 0));
    CHECK(state.calls == 1 && state.poll_numbers[0] == 1);
    CHECK(read_bit(console, 0) == 1); /* A */
    CHECK(read_bit(console, 0) == 0); /* B */
    CHECK(read_bit(console, 0) == 0); /* Select */
    CHECK(read_bit(console, 0) == 1); /* Start */
    CHECK(read_bit(console, 1) == 0); /* A */
    CHECK(read_bit(console, 1) == 1); /* B */

    /* Repeated low writes do not poll or replace the latched first snapshot. */
    OK(nnes_set_buttons(console, 0, NNES_RIGHT));
    OK(nnes_write(console, 0x4016, 0));
    CHECK(state.calls == 1);
    CHECK(read_bit(console, 0) == 0); /* Up from poll 1, not live Right. */

    OK(nnes_write(console, 0x4016, 1));
    OK(nnes_write(console, 0x4016, 0));
    CHECK(state.calls == 2 && state.poll_numbers[1] == 2);
    for (unsigned i = 0; i < 6; ++i)
        CHECK(read_bit(console, 0) == 0);
    CHECK(read_bit(console, 0) == 1); /* Left */
    CHECK(read_bit(console, 0) == 0); /* Right */
    CHECK(read_bit(console, 1) == 0); /* A */
    CHECK(read_bit(console, 1) == 0); /* B */
    CHECK(read_bit(console, 1) == 1); /* Select */
    CHECK(read_bit(console, 1) == 1); /* Start */

    /* Failed sampling preserves the high strobe; retry uses the same index. */
    state.fail_next = 1;
    OK(nnes_write(console, 0x4016, 1));
    CHECK(nnes_write(console, 0x4016, 0) == NNES_UNSUPPORTED);
    CHECK(state.calls == 3 && state.poll_numbers[2] == 3);
    OK(nnes_write(console, 0x4016, 0));
    CHECK(state.calls == 4 && state.poll_numbers[3] == 3);
    CHECK(read_bit(console, 0) == 1); /* A from the successful retry. */
    CHECK(read_bit(console, 1) == 0); /* A from the successful retry. */

    CHECK(nnes_set_input_poll(NULL, poll_buttons, &state) ==
          NNES_INVALID_ARGUMENT);
    uint8_t old = 0;
    OK(nnes_rd8_rmw(console, 0x0200, &old));
    CHECK(nnes_set_input_poll(console, poll_buttons, &state) ==
          NNES_INVALID_SEQUENCE);
    OK(nnes_wr8_rmw(console, 0x0200, (uint8_t)(old ^ 1)));
    nnes_destroy(console);
    puts("NES host input-poll contract passed");
    return 0;
}
