/* The native SNES front end's stalled-game watchdog. */
#ifndef NSHVC_WATCHDOG_H
#define NSHVC_WATCHDOG_H
void snes_watchdog_start(int headless);
void snes_watchdog_frame(void);
#endif
