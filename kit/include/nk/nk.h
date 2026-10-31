/*
 * nk.h: a native console on nativekit. The console is its machine; the kit
 * is the application around it: main's loop, the window, the frame's pace,
 * the sound out, the players' devices, the headless script and its states,
 * the battery save and the picture's presentation (kit/docs/kit.md). A
 * console fills one nk_console and calls nk_main.
 */
#ifndef NK_NK_H
#define NK_NK_H

#include <stddef.h>
#include <stdint.h>

#include "save.h"

#ifdef __cplusplus
extern "C" {
#endif

/* a named memory: `dump NAME` writes each as DIR/NAME<ext>, in table order */
typedef struct {
    const char *ext;                    /* ".wram" */
    const void *(*data)(size_t *bytes); /* as it stands now; NULL with 0 bytes: an empty file */
    int optional;                       /* 1: no file when data gives NULL */
} nk_region;

/* a button's name in the script ("B", "START") and the console's bit */
typedef struct {
    const char *name;
    uint32_t bit;
} nk_button;

typedef struct nk_console {
    const char *name;                   /* "nativeshvc": the window's title fallback, messages */
    const char *env;                    /* its settings' prefix (env.h) */
    const char *play_ext;               /* ".sfx": the plays in <env>_STATES that save states */

    /* the machine */
    void     (*power)(const void *cart);
    void     (*run_frame)(void);        /* to this frame's end */
    uint64_t (*frame_ns)(void);         /* the frame's length now, ns */
    void     (*app_name)(const void *cart, const char **dir, const char **title); /* the save folder, the title */

    /* the picture */
    void     (*window_size)(int *w, int *h);              /* the window's first size */
    double   (*aspect)(void);                              /* the picture's display aspect now */
    void     (*scaled)(int w, int h);                      /* optional: the picture's size on the target */
    void     (*present)(const int d[4]);                   /* the picture onto target rectangle d */
    /* a shot's pixels: w x h, every xs-th column and ys-th row written */
    const uint32_t *(*shot)(int *w, int *h, int *xs, int *ys);
    /* optional, <env>_RRCHECK: draw this frame onto d and fill want (tw x th)
       with its reference; 0 when the generic one applies (the picture
       presented, and drawn scaled on the CPU) */
    int      (*rrcheck)(const int d[4], uint32_t *want, int tw, int th);
    void     (*rrcheck_why)(int x, int y);                 /* optional: more about a pixel that differs */
    const char *rrcheck_own;            /* the report's words for frames rrcheck drew ("with ...") */
    const uint32_t *(*picture)(int *w, int *h);            /* the console's picture as it stands */
    void     (*headless_scale)(double sx, double sy);      /* optional: <env>_SCALE in a headless run */

    /* the sound */
    double   audio_hz;
    int      (*audio_take)(int16_t *lr, int max);
    int      (*audio_queued)(void);

    /* the controllers */
    const nk_button *buttons;
    int      button_count;
    const char *const *steps;           /* the script steps the console takes itself */
    int      step_count;
    int      (*script_step)(int argc, char w[][1024]);     /* one of them: 1 when taken */
    int      (*key)(int vk, int down);                     /* optional: 1 when the game took the key */
    void     (*touch)(int id, int phase, float x, float y);/* optional: the game's touch hook */
    /* the players' controllers from the frame's input: headless, the
       script's buttons (b: players 0 and 1); else the kit's devices (input.h) */
    void     (*input)(int headless, const uint32_t b[2]);
    void     (*frame)(void);            /* optional: the game's frame hook */

    /* memories */
    const nk_region *regions;
    int      region_count;
    int      (*battery)(nk_battery *b); /* optional: 0 no save */
    void     (*counts_hooks)(void (*rd)(uint32_t addr, uint8_t v), void (*wr)(uint32_t addr, uint8_t v));
    int      (*access_kind)(uint32_t addr);
    int      kind_count;

    void     (*at_exit)(void);          /* optional: its own checks' reports */
} nk_console;

/* power on, open what the arguments ask for and run until the window
   closes (or a headless run's end) */
int nk_main(const nk_console *c, const void *cart, int argc, char **argv);

/* the console nk_main runs (NULL before) */
const nk_console *nk_console_now(void);
/* <env>_SCALE (headless): window pixels a console pixel across and down, 0 when not set */
void nk_shot_scale(double *x, double *y);

#ifdef __cplusplus
}
#endif
#endif
