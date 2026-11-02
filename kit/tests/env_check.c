/* env_check: settings by the console's prefix */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nk/env.h"
#include "nk/fatal.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(70);
}

int main(void)
{
    nk_env_prefix("KITX");
    setenv("KITX_STATES", "/tmp/plays", 1);
    if (!nk_getenv("STATES") || strcmp(nk_getenv("STATES"), "/tmp/plays"))
        return fprintf(stderr, "env_check: KITX_STATES not read\n"), 1;
    if (nk_getenv("NOT_SET_ANYWHERE"))
        return fprintf(stderr, "env_check: an unset setting read\n"), 1;
    nk_setenv("SCRIPT_PLAN", "M:0-0");
    if (!getenv("KITX_SCRIPT_PLAN") || strcmp(getenv("KITX_SCRIPT_PLAN"), "M:0-0"))
        return fprintf(stderr, "env_check: KITX_SCRIPT_PLAN not set\n"), 1;
    printf("env_check: ok\n");
    return 0;
}
