/* The raw host masks are kept separate from the controller protocol state. */
#include "io.h"

static uint32_t button_masks[2];

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
