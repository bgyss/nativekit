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

/* A host key, its positional bit, and the label shown to the player. */
typedef struct { int vk; uint32_t bit; const char *name; } nk_key;

#define NK_PLAYERS 8
#define NK_KINDS_MAX 16

/* End the application at the current frame's end. */
void nk_quit(void);

typedef struct nk_console {
    const char *name;                   /* the console's name: the window title's fallback, messages */
    const char *env;                    /* its settings' prefix (env.h) */
    const char *play_ext;               /* ".abc": the plays in <env>_STATES that save states */

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
    const char *rrcheck_own;            /* optional: the report's words for frames rrcheck drew ("with ...") */
    const uint32_t *(*picture)(int *w, int *h);            /* the console's picture as it stands */
    void     (*headless_scale)(double sx, double sy);      /* optional: <env>_SCALE in a headless run */

    /* the sound */
    double   audio_hz;
    int      (*audio_take)(int16_t *lr, int max);
    int      (*audio_queued)(void);

    /* the controllers */
    int players;
    const nk_key *keys;
    int key_count;
    const nk_button *buttons;
    int      button_count;
    const char *const *steps;           /* the script steps the console takes itself */
    int      step_count;
    int      (*script_step)(int argc, char w[][1024]);     /* optional: one of them, 1 when taken */
    int      (*key)(int vk, int down);                     /* optional: 1 when the game took the key */
    void     (*touch)(int id, int phase, float x, float y);/* optional: the game's touch hook */
    /* the players' controllers from the frame's input: headless, the
       script's buttons (b: players 0 and 1); else the kit's devices (input.h) */
    void     (*input)(int headless, const uint32_t *b);
    void     (*frame)(void);            /* optional: the game's frame hook */

    /* memories */
    const nk_region *regions;
    int      region_count;
    int      (*battery)(nk_battery *b); /* optional: 0 no save */
    /* needed only for --counts: the hooks on every access, an access's
       kind (0 to kind_count - 1) and the number of kinds (16 at most) */
    void     (*counts_hooks)(void (*rd)(uint32_t addr, uint8_t v), void (*wr)(uint32_t addr, uint8_t v));
    int      (*access_kind)(uint32_t addr);
    int      kind_count;
    const char *const *kind_names;      /* optional names in access-kind order */

    void     (*at_exit)(void);          /* optional: its own checks' reports */
} nk_console;

/* power on, open what the arguments ask for and run until the window
   closes (or a headless run's end) */
int nk_main(const nk_console *c, const void *cart, int argc, char **argv);

/* the console nk_main runs (NULL before) */
const nk_console *nk_console_now(void);
/* <env>_SCALE (headless): window pixels a console pixel across and down, 0 when not set */
void nk_shot_scale(double *x, double *y);

/* the game's images over the picture: an image's id, -1 when full; placed
   by its centre (cx, cy, the window's points), scale, turn and alpha */
#define NK_OVERLAYS 16
int  nk_overlay_image(const uint32_t *argb, int w, int h);   /* -1 when full */
void nk_overlay_place(int id, float cx, float cy, float scale, float angle_rad, float alpha, int visible);
void nk_window_size(float *w, float *h);   /* points; a headless run's RR_SIZE */
int  nk_headless(void);
long nk_frame_no(void);
/* where the last frame's picture was drawn on the target, and the target's width */
void nk_shown_at(int d[4], int *tw);

#ifdef __cplusplus
}
#endif
#endif
