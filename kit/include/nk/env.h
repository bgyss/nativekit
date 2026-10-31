/*
 * env.h: the console's settings, each named PREFIX_NAME (a console with
 * prefix ABC: ABC_STATES...), so a console's settings keep their names.
 */
#ifndef NK_ENV_H
#define NK_ENV_H

#ifdef __cplusplus
extern "C" {
#endif

void nk_env_prefix(const char *prefix);           /* the console's settings' prefix */
const char *nk_getenv(const char *name);           /* getenv("<prefix>_<name>") */
void nk_setenv(const char *name, const char *value); /* setenv("<prefix>_<name>"); fatal when it cannot */

#ifdef __cplusplus
}
#endif
#endif
