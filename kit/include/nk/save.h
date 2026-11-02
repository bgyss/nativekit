/*
 * save.h: the cartridge's battery RAM kept as NAME.srm in the host's save
 * folder, written when its save area changes. A headless run never calls
 * these.
 */
#ifndef NK_SAVE_H
#define NK_SAVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *ram;          /* the battery-backed RAM as it stands now */
    size_t bytes;          /* its size */
    size_t from, size;     /* the save area within it (size 0: all of it) */
    const char *name;      /* the save's file name, less ".srm"; never NULL (the
                              console supplies its fallback) */
} nk_battery;

/* the save loaded from dir/NAME.srm into the RAM; get() fills b (0: no save) */
void nk_save_load(int (*get)(nk_battery *b), const char *dir);
/* written when the save area changed (a new file, renamed over the old) */
void nk_save_write(void);

#ifdef __cplusplus
}
#endif
#endif
