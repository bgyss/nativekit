/* routines_check: the lookup over sorted tables of several shapes, absent
   addresses before, between and past the entries. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "nk/fatal.h"
#include "nk/routines.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(70);
}

typedef struct { uint32_t addr; void (*fn)(void); const char *name; int x; } Wide;
typedef struct { uint32_t addr; } Narrow;

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "routines_check: %s\n", #c); fails++; } } while (0)

int main(void)
{
    static const Wide w[] = { { 0x008000, 0, "a", 0 }, { 0x008010, 0, "b", 0 }, { 0xC00000, 0, "c", 0 } };
    static const Narrow n[] = { { 1 }, { 2 }, { 3 }, { 5 }, { 8 } };
    CHECK(nk_routine_find(w, 3, sizeof w[0], 0x008010) == &w[1]);
    CHECK(nk_routine_find(w, 3, sizeof w[0], 0x008000) == &w[0]);
    CHECK(nk_routine_find(w, 3, sizeof w[0], 0xC00000) == &w[2]);
    CHECK(nk_routine_find(w, 3, sizeof w[0], 0x007FFF) == NULL);
    CHECK(nk_routine_find(w, 3, sizeof w[0], 0x008001) == NULL);
    CHECK(nk_routine_find(w, 3, sizeof w[0], 0xFFFFFF) == NULL);
    CHECK(nk_routine_find(n, 5, sizeof n[0], 5) == &n[3]);
    CHECK(nk_routine_find(n, 5, sizeof n[0], 4) == NULL);
    CHECK(nk_routine_find(n, 0, sizeof n[0], 1) == NULL);
    CHECK(nk_routine_find(NULL, 0, sizeof n[0], 1) == NULL);
    if (fails)
        return 1;
    printf("routines_check: ok\n");
    return 0;
}
