/* Exercise access-kind diagnostics through the real application loop. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nk/fatal.h"
#include "nk/nk.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(70);
}
static void (*read_hook)(uint32_t, uint8_t);
static void (*write_hook)(uint32_t, uint8_t);
static int group_mode;
static unsigned group_frame;
static void hooks(void (*rd)(uint32_t, uint8_t), void (*wr)(uint32_t, uint8_t))
{
    read_hook = rd;
    write_hook = wr;
}
static int group(uint32_t addr) { return (addr & 0xFF000000u) == 0x01000000u; }
static int kind(uint32_t addr) { return group_mode ? group(addr) : 2; }
static void power(const void *cart) { (void)cart; }
static void input(int headless, const uint32_t *buttons) { (void)headless; (void)buttons; }
static void frame(void)
{
    unsigned i;
    if (!group_mode) {
        read_hook(0, 0);
        return;
    }
    if (group_mode == 2) {
        if (group_frame++ == 0)
            read_hook(0, 0);
        else
            write_hook(0, 0);
        return;
    }
    read_hook(0, 0);
    for (i = 0; i < 65; i++)
        read_hook(0x01000000u, 0);
    write_hook(0, 0);
    write_hook(0x01000000u, 0);
}
static void name(const void *cart, const char **dir, const char **title) { (void)cart; *dir = "kind_check"; *title = "kind_check"; }
static void size(int *w, int *h) { *w = *h = 8; }
static const char *const names[] = { "memory", "io" };
int main(int argc, char **argv)
{
    nk_console c = { .name = "kind_check", .env = "KIND", .players = 1, .power = power,
        .run_frame = frame, .app_name = name, .window_size = size, .counts_hooks = hooks,
        .input = input, .access_kind = kind, .count_group = group, .kind_count = 2 };
    char *args[] = { argv[0], "--headless", "--counts", "/dev/null", NULL };
    if (argc > 1 && !strcmp(argv[1], "--groups-across-frames")) {
        group_mode = 2;
        return nk_main(&c, NULL, argc, argv);
    }
    if (argc > 1 && !strcmp(argv[1], "--groups")) {
        group_mode = 1;
        return nk_main(&c, NULL, argc, argv);
    }
    if (argc > 1 && !strcmp(argv[1], "--names"))
        c.kind_names = names;
    return nk_main(&c, NULL, 4, args);
}
