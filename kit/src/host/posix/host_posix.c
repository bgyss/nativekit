/* host_posix.c: time and files for POSIX systems (macOS, Linux, iOS). */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>

#include <stdlib.h>

#include "host.h"

uint64_t host_ticks_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

void host_sleep_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
        ;
}

void host_thread_interactive(void)
{
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
}

int host_mkdir(const char *path) { return mkdir(path, 0755) == 0 || errno == EEXIST; }


/* A separate immutable context lets callers schedule independent callbacks. */
typedef struct HostEvery {
    uint32_t ms;
    void (*fn)(void);
} HostEvery;

static void *every_thread(void *arg)
{
    const HostEvery *e = (const HostEvery *)arg;
    for (;;) {
        host_sleep_ms(e->ms);
        e->fn();
    }
    return NULL;
}

void host_every(uint32_t ms, void (*fn)(void))
{
    HostEvery *e;
    pthread_t t;
    if (!ms || !fn)
        return;
    e = (HostEvery *)malloc(sizeof *e);
    if (!e)
        return;
    e->ms = ms;
    e->fn = fn;
    if (pthread_create(&t, NULL, every_thread, e) == 0)
        pthread_detach(t);
    else
        free(e);
}
