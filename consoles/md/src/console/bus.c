/*
 * bus.c: the 68000-visible bus. Stage 2 adds the memory map and clocked
 * device accesses; the console fatal is needed by the kit from its first
 * link.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "nk/fatal.h"

static _Noreturn void md_vfatal(const char *fmt, va_list ap)
{
    fprintf(stderr, "nativemd: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(70);
}

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    md_vfatal(fmt, ap);
}
