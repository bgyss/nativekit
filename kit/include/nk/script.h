/*
 * script.h: a headless run's script (script.c): steps of buttons held for
 * frames, shots and dumps, from power on.
 *
 *   wait N               N frames, no buttons
 *   hold B[+B...] [N]    hold buttons for N frames (default 1)
 *   press B[+B...] [N]   hold for N frames (default 2), then 2 released
 *   shot NAME            the finished frame as DIR/NAME.ppm (the console's
 *                        picture: nk_console.shot)
 *   dump NAME            each of the console's memories (nk_console.regions)
 *                        as DIR/NAME<ext>, in its table's order
 *   repeat N ... end     the lines between, N times (nests)
 *   save NAME            the machine as it is, for a load
 *   load PATH            the machine as state NAME (PATH's file name less
 *                        .state) left it: saved by this run, else replayed
 *                        from power-on, by the script's own steps to its
 *                        save NAME or by the play in <prefix>_STATES
 *                        (*<play_ext>) that has one
 *   the console's own steps (nk_console.steps), its line's words to
 *                        nk_console.script_step
 * save and load fork, or, without fork, go on in a new copy of the program
 * that replays to the machine wanted (script.c).
 *
 * Buttons: the console's (nk_console.buttons), player 1; 2. before one for
 * player 2 (2.START). '#' starts a comment.
 */
#ifndef NK_SCRIPT_H
#define NK_SCRIPT_H

#include <stdint.h>

#include "nk.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the console the script runs on: before nk_script_load */
void nk_script_console(const nk_console *c);
void nk_script_load(const char *path, const char *out_dir);
/* the program's arguments, for a run that goes on in a new copy of the
   program (save, load and calls without keep, without fork) */
void nk_script_args(int argc, char **argv);
/* replaying steps to rebuild a machine (nothing they make is written) */
int nk_script_replaying(void);
/* the steps loaded (0: no script) */
int nk_script_steps(void);
/* the shots and dumps due now, at the end of a frame */
void nk_script_takes(void);
/* the next frame's buttons from the script; 0 when it has ended */
int nk_script_frame(uint32_t *b0, uint32_t *b1);

#ifdef __cplusplus
}
#endif
#endif
