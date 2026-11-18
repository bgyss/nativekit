/*
 * session.h: a play recorded for an exact replay (session.c), and a log of
 * every frame a run makes (app.c's --record DIR and --frame-log FILE).
 *
 * --frame-log FILE (any run) writes a line for each frame the machine ran:
 *
 *   # nk frame log 1
 *   # console NAME
 *   # audio_hz HZ
 *   # regions .ext .ext ...      (the console's memories, nk_console.regions)
 *   F N PICTURE STATE [SAMPLES]
 *
 * N counts the frames run from power-on (1 is the first). PICTURE is the
 * 64-bit FNV-1a of the console's picture (its width, height and pixels),
 * STATE that of every memory in the regions' order, both in hex. SAMPLES
 * (with --sound) is the number of stereo samples written to the sound file
 * once frame N's sound is in it. A frame the machine did not run (a pause)
 * has no line.
 *
 * --record DIR (a windowed run) writes in DIR, which it makes if needed:
 *
 *   input.nks    the players' buttons each frame as a headless script
 *                (script.h: wait and hold lines), so --headless --script
 *                DIR/input.nks plays the same frames again
 *   frames.log   the frame log of the play (as above, without SAMPLES)
 *   marks.log    the recorder's marks: "mark N SECONDS" and
 *                "range A B SECONDS" (frames N, A to B; seconds from the
 *                start of the play)
 *
 * While recording the recorder takes these keys before the game:
 *
 *   F1   the frame counter shown or hidden (shown at first)
 *   F5   mark the frame shown
 *   F6   a range starts at the frame shown; F7 ends it there
 *   F9   pause or go on (the recorder's own pause: no frame runs)
 *   F10  while paused, run one frame
 *
 * The first frame's buttons reach no controller read in a windowed run
 * (the machine runs it before the first input) and none in a headless one
 * (a script's first frame is taken before the machine runs), so the two
 * agree from power-on.
 */
#ifndef NK_SESSION_H
#define NK_SESSION_H

#include "nk.h"

#ifdef __cplusplus
extern "C" {
#endif

/* before the machine powers on: record (windowed) into dir, and/or the
   frame log in log (either NULL) */
void nk_session_open(const nk_console *c, const char *dir, const char *log, int headless);
/* 1 while the recorder's pause holds the machine (no frame is due) */
int  nk_session_holding(void);
/* around each frame the machine runs; samples: the sound file's stereo
   samples so far (after the frame), -1 when there is no sound file */
void nk_session_run_begin(void);
void nk_session_run_end(long long samples);
/* a key for the recorder: 1 when it took it */
int  nk_session_key(int vk, int down);
/* the recorder's line over the picture ("" none) */
const char *nk_session_text(void);
/* frames the machine has run */
long nk_session_frames(void);

#ifdef __cplusplus
}
#endif
#endif
