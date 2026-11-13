/* Optional additions for existing applications that supply their own host.
 * A custom host may override these weak defaults with its own definitions. */
#include "host.h"

__attribute__((weak)) void host_text_input(int on) { (void)on; }
__attribute__((weak)) int host_keyboard_attached(void) { return 0; }
__attribute__((weak)) int host_app_active(void) { return 1; }
__attribute__((weak)) void host_every(uint32_t ms, void (*fn)(void))
{
    (void)ms;
    (void)fn;
}
