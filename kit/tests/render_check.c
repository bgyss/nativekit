/* Check the console's headless size and the environment override. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nk/fatal.h"
#include "nk/rr.h"
#include "nk/rr_size.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(70);
}

int main(int argc, char **argv)
{
    int w = 0, h = 0;
    if (argc > 1 && !strcmp(argv[1], "--unset")) {
        rr_headless_size(&w, &h);
        return 0;
    }
    if (argc <= 1 || strcmp(argv[1], "--override-only"))
        rr_set_headless_size(320, 240);
    rr_headless_size(&w, &h);
    if (getenv("RR_SIZE") ? !(w == 1100 && h == 800) : !(w == 320 && h == 240))
        return 1;
    puts("render_check: ok");
    return 0;
}
