/* save_check: the battery save. Loaded from DIR/NAME.srm when there is
   one; written only when the save area changed, through NAME.srm.new
   renamed over NAME.srm; a change outside the save area writes nothing;
   a save area past the RAM is fatal. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nk/fatal.h"
#include "nk/save.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "stub: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(70);
}

static uint8_t ram[64];
static size_t from = 16, size = 16;

static int get(nk_battery *b)
{
    b->ram = ram;
    b->bytes = sizeof ram;
    b->from = from;
    b->size = size;
    b->name = "kit test";
    return 1;
}

static long file_size(const char *p)
{
    struct stat st;
    return stat(p, &st) ? -1 : (long)st.st_size;
}

int main(int argc, char **argv)
{
    char dir[] = "/tmp/nk_save_XXXXXX", path[256];
    FILE *f;
    uint8_t back[64];
    if (!mkdtemp(dir))
        return 1;
    snprintf(path, sizeof path, "%s/kit test.srm", dir);
    if (argc > 1 && !strcmp(argv[1], "--past")) {
        from = 60;
        nk_save_load(get, dir);
        return 0;
    }
    memset(ram, 0xAA, sizeof ram);
    f = fopen(path, "wb");
    for (int i = 0; i < 64; i++)
        fputc(i, f);
    fclose(f);
    nk_save_load(get, dir);
    if (ram[5] != 5 || ram[63] != 63)
        return fprintf(stderr, "save_check: not loaded\n"), 1;
    remove(path);
    ram[2] = 0x55;                      /* outside the save area */
    nk_save_write();
    if (file_size(path) != -1)
        return fprintf(stderr, "save_check: written for a change outside the area\n"), 1;
    ram[20] = 0x99;                     /* inside it */
    nk_save_write();
    if (file_size(path) != 64)
        return fprintf(stderr, "save_check: not written (%ld)\n", file_size(path)), 1;
    snprintf(path + strlen(path), sizeof path - strlen(path), ".new");
    if (file_size(path) != -1)
        return fprintf(stderr, "save_check: the .new file left\n"), 1;
    path[strlen(path) - 4] = 0;
    f = fopen(path, "rb");
    if (fread(back, 1, 64, f) != 64 || back[20] != 0x99 || back[2] != 0x55)
        return fprintf(stderr, "save_check: wrong bytes written\n"), 1;
    fclose(f);
    printf("save_check: ok\n");
    return 0;
}
