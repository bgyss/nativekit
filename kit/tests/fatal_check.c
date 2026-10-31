/* fatal_check: a console's nk_fatal is what the kit's own errors reach:
   a coroutine that returns is fatal, with the console's message and its
   status. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "nk/coro.h"
#include "nk/fatal.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "stub: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(70);
}

static void returns(void *arg) { (void)arg; }

int main(void)
{
    Coro *self = coro_self(), *c = coro_new(returns, NULL, 64 * 1024);
    coro_switch(self, c);
    fprintf(stderr, "fatal_check: the coroutine's return was not fatal\n");
    return 1;
}
