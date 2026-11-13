/*
 * click_macos.m: the mouse's buttons as the macOS host reports them
 * (HOST_EV_MOUSE's buttons), in a window: AppKit's own button events posted
 * to the host's window, all four queued before any is handled, so each
 * press is handled after its button is already up again (as a trackpad's
 * tap, or any click let go before a console's frame). The buttons must come
 * from each event itself, not from the buttons held when it is handled: a
 * left press, its release, a right press and its release must read
 * 1, 0, 2, 0, at the places posted.
 *
 * After the upstream check of the same name (Jeffrey Wilbur), which hears
 * these events through a console's pointer hook; held here at the host's
 * events, which the kit has without that hook.
 *
 *   kit_click_macos   opens a window, runs the checks, exits 0 when every
 *                     one holds
 */
#import <AppKit/AppKit.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "nk/host.h"

static int heard[16], n, fails;
static float heard_x[16], heard_y[16];

static void post(NSWindow *w, NSEventType type, double x, double y)
{
    NSView *v = [w contentView];
    NSPoint p = [v convertPoint:NSMakePoint(x, [v bounds].size.height - y) toView:nil];
    NSEvent *e = [NSEvent mouseEventWithType:type location:p modifierFlags:0 timestamp:0
                                windowNumber:[w windowNumber] context:nil eventNumber:0 clickCount:1 pressure:1];
    [NSApp postEvent:e atStart:NO];
}

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

/* the host's events for up to ms; only those at the places posted are
   kept: the real pointer's moves are not */
static void drain(uint32_t ms)
{
    uint64_t end = host_ticks_ns() + (uint64_t)ms * 1000000u;
    host_event e;
    while (host_ticks_ns() < end) {
        if (!host_poll_event(&e, 10) || e.type != HOST_EV_MOUSE)
            continue;
        if (!((fabsf(e.x - 40) < 0.01f && fabsf(e.y - 30) < 0.01f) ||
              (fabsf(e.x - 60) < 0.01f && fabsf(e.y - 50) < 0.01f)))
            continue;
        if (n < 16) {
            heard_x[n] = e.x;
            heard_y[n] = e.y;
            heard[n++] = e.buttons;
        }
    }
}

int main(int argc, char **argv)
{
    NSWindow *w;
    int i;

    host_set_app_name("kit_click_macos", "click");
    host_init(argc, argv);
    if (!host_window_open(256, 224, "click")) {
        printf("FAIL no window\n");
        return 1;
    }
    drain(500);                         /* the window up, its first events past */
    n = 0;
    w = [NSApp mainWindow] ? [NSApp mainWindow] : [[NSApp windows] firstObject];
    if (!w) {
        printf("FAIL no window to post to\n");
        return 1;
    }
    post(w, NSEventTypeLeftMouseDown, 40, 30);
    post(w, NSEventTypeLeftMouseUp, 40, 30);
    post(w, NSEventTypeRightMouseDown, 60, 50);
    post(w, NSEventTypeRightMouseUp, 60, 50);
    drain(500);
    for (i = 0; i < n; i++)
        printf("     heard (%.1f, %.1f) buttons %d\n", heard_x[i], heard_y[i], heard[i]);
    check(n == 4, "four events heard");
    check(n == 4 && heard[0] == 1 && heard[1] == 0 && heard[2] == 2 && heard[3] == 0,
          "left press 1, release 0, right press 2, release 0");
    check(n == 4 && fabsf(heard_x[0] - 40) < 0.01f && fabsf(heard_y[0] - 30) < 0.01f &&
              fabsf(heard_x[2] - 60) < 0.01f && fabsf(heard_y[2] - 50) < 0.01f,
          "at the places posted, from the view's top left");
    printf("%d check(s) failed\n", fails);
    host_shutdown();
    return fails ? 1 : 0;
}
