/* host_win32.c: time and files on Windows, what host_posix.c is on macOS
 * and Linux. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>

#include <stdlib.h>

#include "host.h"

uint64_t host_ticks_ns(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart)
        QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)(c.QuadPart / f.QuadPart) * 1000000000u +
           (uint64_t)(c.QuadPart % f.QuadPart) * 1000000000u / (uint64_t)f.QuadPart;
}

/* Sleep() wakes on the system timer's tick (15.6 ms unless a program asks
   for finer): the first sleep asks for 1 ms */
void host_sleep_ms(uint32_t ms)
{
    static int fine;
    if (!fine) {
        timeBeginPeriod(1);
        fine = 1;
    }
    Sleep(ms);
}

void host_thread_interactive(void) { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL); }

int host_mkdir(const char *path)
{
    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}


/* A separate immutable context lets callers schedule independent callbacks. */
typedef struct HostEvery {
    uint32_t ms;
    void (*fn)(void);
} HostEvery;

static DWORD WINAPI every_thread(LPVOID arg)
{
    const HostEvery *e = (const HostEvery *)arg;
    for (;;) {
        host_sleep_ms(e->ms);
        e->fn();
    }
    return 0;
}

void host_every(uint32_t ms, void (*fn)(void))
{
    HostEvery *e;
    HANDLE t;
    if (!ms || !fn)
        return;
    e = (HostEvery *)malloc(sizeof *e);
    if (!e)
        return;
    e->ms = ms;
    e->fn = fn;
    t = CreateThread(NULL, 0, every_thread, e, 0, NULL);
    if (t)
        CloseHandle(t);
    else
        free(e);
}
