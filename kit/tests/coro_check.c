/* coro_check: switching back and forth, each coroutine on its own stack,
   deep recursion on one. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "nk/coro.h"
#include "nk/fatal.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(70);
}

static Coro *self, *a, *b;
static int trace[16], nt;

static int deep(int n) { return n == 0 ? 0 : 1 + deep(n - 1); }

static void fa(void *arg)
{
    (void)arg;
    for (;;) {
        trace[nt++] = 1;
        coro_switch(a, b);
    }
}

static void fb(void *arg)
{
    (void)arg;
    for (;;) {
        char local[256];
        local[0] = (char)deep(10000);
        trace[nt++] = 2 + (local[0] != (char)10000);
        coro_switch(b, self);
    }
}

int main(void)
{
    int i;
    self = coro_self();
    a = coro_new(fa, NULL, 1 << 20);
    b = coro_new(fb, NULL, 1 << 20);
    for (i = 0; i < 4; i++) {
        coro_switch(self, a);
        if (coro_current() != self) {
            fprintf(stderr, "coro_check: not back on the program's stack\n");
            return 1;
        }
    }
    for (i = 0; i < 8; i++)
        if (trace[i] != (i % 2 ? 2 : 1)) {
            fprintf(stderr, "coro_check: trace[%d] = %d\n", i, trace[i]);
            return 1;
        }
    printf("coro_check: ok\n");
    return 0;
}
