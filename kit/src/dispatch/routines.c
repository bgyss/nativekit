/*
 * routines.c: the routine table's lookup (routines.h): a binary search.
 */
#include <string.h>

#include "routines.h"

const void *nk_routine_find(const void *base, int count, size_t stride, uint32_t addr)
{
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const char *e = (const char *)base + (size_t)mid * stride;
        uint32_t at;
        memcpy(&at, e, sizeof at);
        if (at == addr)
            return e;
        if (at < addr)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}
