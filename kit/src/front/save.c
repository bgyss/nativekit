/*
 * save.c: the battery save (save.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fatal.h"
#include "save.h"

static int (*s_get)(nk_battery *b);
static uint8_t *saved;
static size_t save_from, save_size;
static char save_path[1024];

void nk_save_load(int (*get)(nk_battery *b), const char *dir)
{
    FILE *f;
    nk_battery b;
    s_get = get;
    if (!get || !get(&b) || !b.bytes || !b.ram)
        return;
    save_from = b.size ? b.from : 0;
    save_size = b.size ? b.size : b.bytes;
    if (save_from + save_size > b.bytes)
        nk_fatal("the cartridge's save area ($%X + $%X) is past its RAM ($%X)", (unsigned)save_from,
                 (unsigned)save_size, (unsigned)b.bytes);
    saved = (uint8_t *)malloc(b.bytes);
    if (!saved)
        nk_fatal("no memory for the save");
    memcpy(saved, b.ram, b.bytes);
    snprintf(save_path, sizeof save_path, "%s/%s.srm", dir, b.name);
    f = fopen(save_path, "rb");
    if (!f)
        return;
    if (fread(b.ram, 1, b.bytes, f) == b.bytes)
        memcpy(saved, b.ram, b.bytes);
    fclose(f);
}

/* written (to a new file, then renamed over the old) when the save area
   changed */
void nk_save_write(void)
{
    char tmp[1100];
    FILE *f;
    nk_battery b;
    if (!saved || !s_get(&b))
        return;
    if (!memcmp(saved + save_from, b.ram + save_from, save_size))
        return;
    memcpy(saved, b.ram, b.bytes);
    snprintf(tmp, sizeof tmp, "%s.new", save_path);
    f = fopen(tmp, "wb");
    if (!f)
        return;
    fwrite(saved, 1, b.bytes, f);
    fclose(f);
    remove(save_path);
    rename(tmp, save_path);
}
