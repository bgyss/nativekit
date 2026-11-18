/* The console entry point is linkable from a game's C. */
#include <nmd/nmd.h>

static int (*volatile entry)(const nmd_cart *, int, char **) = nmd_main;

int main(void)
{
    return entry == NULL;
}
