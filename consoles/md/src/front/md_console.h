/*
 * md_console.h: nativekit's table for nativemd.
 */
#ifndef NMD_MD_CONSOLE_H
#define NMD_MD_CONSOLE_H

#include "nk/nk.h"

#include "nk/host.h"

extern const nk_console nmd_console;

/* A windowed frame's input: the players' controllers (`now`/`on` given: a
 * check's own, else read from the host) and the keyboard, onto the pads. */
void nmd_input_frame(const host_pad *now, const int *on);

#endif
