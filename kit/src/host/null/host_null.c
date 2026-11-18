/* host_null.c: a host with no window, no input and no audio device.
 *
 * The game runs exactly as it would on screen; nothing is shown and no key
 * is ever pressed except what a script injects. For tests and for
 * comparisons with the original.
 *
 * PORT_SAVEDIR names the save folder (default build/<dir>/save).
 *
 * PORT_EVENTS=FILE gives a windowed run keys, for checks: lines "N VK DOWN",
 * key VK (a Win32 virtual key code) down (1) or up (0) in the Nth drain of
 * the event queue (the first is 1; a windowed run drains it once a frame).
 * Lines in order of N.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"

static char s_save[1024];
static const char *s_app_dir = "game";

void host_set_app_name(const char *dir, const char *title)
{
    (void)title;
    if (dir)
        s_app_dir = dir;
}

void host_init(int argc, char **argv)
{
    const char *e = getenv("PORT_SAVEDIR");
    (void)argc;
    (void)argv;
    if (e) {
        snprintf(s_save, sizeof s_save, "%s", e);
    } else {
        snprintf(s_save, sizeof s_save, "build/%s", s_app_dir);
        host_mkdir("build");
        host_mkdir(s_save);
        snprintf(s_save, sizeof s_save, "build/%s/save", s_app_dir);
    }
    host_mkdir(s_save);
}

void host_shutdown(void) {}

const char *host_resource_dir(void)
{
    const char *e = getenv("PORT_RESDIR");
    return e ? e : ".";
}

const char *host_save_dir(void) { return s_save; }

int host_window_open(int width, int height, const char *title)
{
    (void)width;
    (void)height;
    (void)title;
    return 1;
}

void host_window_close(void) {}
int host_window_visible(void) { return 0; }
/* no window: a renderer draws offscreen */
void *host_window_handle(void) { return NULL; }
void host_window_pixels(int *w, int *h) { *w = *h = 0; }
void host_window_points(float *w, float *h) { *w = *h = 0; }
#ifdef _WIN32
void *host_win32_instance(void) { return NULL; }
#endif

void host_present(const uint32_t *argb, int w, int h, float aspect)
{
    (void)argb;
    (void)w;
    (void)h;
    (void)aspect;
}

static FILE *s_events;
static long s_drain = 1, s_next_n = -1;
static int s_next_vk, s_next_down;

static void next_event(void)
{
    s_next_n = -1;
    if (s_events && fscanf(s_events, "%ld %d %d", &s_next_n, &s_next_vk, &s_next_down) != 3)
        s_next_n = -1;
}

int host_poll_event(host_event *ev, uint32_t wait_ms)
{
    static int opened;
    if (!opened) {
        const char *e = getenv("PORT_EVENTS");
        opened = 1;
        if (e && !(s_events = fopen(e, "r")))
            fprintf(stderr, "PORT_EVENTS: cannot read %s\n", e);
        next_event();
    }
    if (s_next_n >= 0 && s_next_n <= s_drain) {
        memset(ev, 0, sizeof *ev);
        ev->type = HOST_EV_KEY;
        ev->vk = s_next_vk;
        ev->down = s_next_down;
        next_event();
        return 1;
    }
    s_drain++;
    if (wait_ms && wait_ms != 0xFFFFFFFFu)
        host_sleep_ms(wait_ms);
    return 0;
}

void host_message_box(const char *text, const char *caption)
{
    fprintf(stderr, "%s: %s\n", caption ? caption : "message", text ? text : "");
}

int host_audio_open(int rate, host_audio_fn fn, void *user)
{
    (void)rate;
    (void)fn;
    (void)user;
    return 0;
}

void host_audio_close(void) {}

int host_pad_read(int index, host_pad *p)
{
    (void)index;
    memset(p, 0, sizeof *p);
    return 0;
}

void host_safe_area(float *l, float *t, float *r, float *b) { *l = *t = *r = *b = 0; }

void host_text_input(int on) { (void)on; }
int host_keyboard_attached(void) { return 0; }

int host_app_active(void) { return 1; }
