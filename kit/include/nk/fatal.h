/*
 * fatal.h: the kit's fatal error. The console defines nk_fatal: its own
 * name before the message, and its exit status (70 for every console in
 * this repository: the contract's "a fatal error, never a guess").
 */
#ifndef NK_FATAL_H
#define NK_FATAL_H

#ifdef __GNUC__
__attribute__((format(printf, 1, 2), noreturn))
#endif
void nk_fatal(const char *fmt, ...);

#endif
