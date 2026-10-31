/*
 * env.c: the console's settings by its prefix (env.h).
 */
#include <stdio.h>
#include <stdlib.h>

#include "env.h"
#include "fatal.h"

static const char *s_prefix = "NK";

void nk_env_prefix(const char *prefix) { s_prefix = prefix; }

static const char *full(const char *name)
{
    static char k[4][128];
    static int i;
    i = (i + 1) & 3;
    snprintf(k[i], sizeof k[i], "%s_%s", s_prefix, name);
    return k[i];
}

const char *nk_getenv(const char *name) { return getenv(full(name)); }

void nk_setenv(const char *name, const char *value)
{
    const char *k = full(name);
#ifdef _WIN32
    if (_putenv_s(k, value))
#else
    if (setenv(k, value, 1))
#endif
        nk_fatal("cannot set %s", k);
}
