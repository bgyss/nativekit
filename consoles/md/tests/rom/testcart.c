/* Execute the binary step table built from tests/rom/cases.py on nmd. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nmd/nmd.h>

#include "md_internal.h"
#include "render.h"

#define STEPS_MAGIC "NMDX"
#define STEPS_RECORD_SIZE 12u

enum {
    STEPS_VERSION = 1,
    STEPS_Z80_VERSION = 2,
    Z80_FIXTURE_BANK_WINDOW = 1
};

enum {
    STEP_READ = 1,
    STEP_WRITE = 2,
    STEP_TAS = 3,
    STEP_POLL = 4
};

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *data;
    if (!file) {
        fprintf(stderr, "testcart: cannot read %s\n", path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET)) {
        fprintf(stderr, "testcart: cannot size %s\n", path);
        fclose(file);
        return NULL;
    }
    data = (uint8_t *)malloc((size_t)length ? (size_t)length : 1);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "testcart: cannot load %s\n", path);
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static uint32_t read_be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

static int parse_steps_header(const uint8_t *steps, size_t size, uint16_t *count,
                              uint16_t *fixture_id, uint16_t *z80_entry,
                              size_t *header_size)
{
    uint16_t version;

    if (size < 8u || memcmp(steps, STEPS_MAGIC, 4))
        return 0;
    version = read_be16(steps + 4);
    *count = read_be16(steps + 6);
    *fixture_id = 0;
    *z80_entry = 0;
    *header_size = 8u;
    if (version == STEPS_Z80_VERSION) {
        if (size < 12u)
            return 0;
        *fixture_id = read_be16(steps + 8);
        *z80_entry = read_be16(steps + 10);
        *header_size = 12u;
    } else if (version != STEPS_VERSION) {
        return 0;
    }
    return size == *header_size + (size_t)*count * STEPS_RECORD_SIZE;
}

static int z80_never_ready(void *ctx)
{
    (void)ctx;
    return 0;
}

static void z80_bank_window_fixture(void)
{
    unsigned bit;
    uint8_t value;

    for (bit = 0; bit < 9; bit++)
        zwr(0x6000, (uint8_t)((0x007Fu >> bit) & 1u));
    value = zrd(0xFFFF);
    zwr(0x0010, value);
    for (;;)
        (void)nmd_z80_wait_until(z80_never_ready, NULL);
}

static const nmd_z80_routine z80_bank_window_routines[] = {
    { 0x0000, z80_bank_window_fixture, "z80_bank_window_fixture" },
};

static int set_z80_fixture(nmd_cart *cart, uint16_t fixture_id, uint16_t entry)
{
    if (!fixture_id)
        return entry == 0;
    if (fixture_id != Z80_FIXTURE_BANK_WINDOW || entry != 0)
        return 0;
    cart->z80_routines = z80_bank_window_routines;
    cart->z80_routine_count = sizeof z80_bank_window_routines /
                              sizeof z80_bank_window_routines[0];
    return 1;
}

static void trace_bus(uint64_t clock, uint8_t operation, uint8_t width,
                      uint32_t addr, uint32_t value)
{
    printf("%" PRIu64 "\t%u\t%u\t%08X\t%08X\n", clock, operation, width,
           (unsigned)addr, (unsigned)value);
}

static void trace_z80(uint64_t clock, uint8_t operation, uint8_t width,
                      uint32_t addr, uint32_t value)
{
    printf("Z80\t%" PRIu64 "\t%u\t%u\t%08X\t%08X\n", clock, operation, width,
           (unsigned)addr, (unsigned)value);
}

static void trace_vdp(uint64_t clock, uint8_t operation, uint8_t width,
                      uint32_t port, uint32_t value)
{
    printf("VDP\t%" PRIu64 "\t%u\t%u\t%08X\t%08X\n", clock, operation, width,
           (unsigned)port, (unsigned)value);
}

static int write_picture(const char *path)
{
    FILE *file;
    uint8_t *rgb;
    const uint32_t *pixels;
    int width, height;
    size_t count, i;
    int valid = 1;

    pixels = md_vdp_picture(&width, &height);
    count = (size_t)width * (size_t)height;
    rgb = (uint8_t *)malloc(count * 3u);
    if (!rgb)
        return 0;
    for (i = 0; i < count; i++) {
        rgb[i * 3u] = (uint8_t)(pixels[i] >> 16);
        rgb[i * 3u + 1u] = (uint8_t)(pixels[i] >> 8);
        rgb[i * 3u + 2u] = (uint8_t)pixels[i];
    }

    file = fopen(path, "wb");
    if (!file) {
        free(rgb);
        return 0;
    }
    if (fprintf(file, "P6\n%d %d\n255\n", width, height) < 0)
        valid = 0;
    if (fwrite(rgb, 3u, count, file) != count)
        valid = 0;
    if (fclose(file))
        valid = 0;
    free(rgb);
    return valid;
}

static int execute_steps(const uint8_t *steps, size_t size)
{
    uint16_t version, count;
    uint16_t fixture_id, z80_entry;
    size_t header_size;
    unsigned i;
    if (!parse_steps_header(steps, size, &count, &fixture_id, &z80_entry, &header_size)) {
        fprintf(stderr, "testcart: invalid step table header\n");
        return 2;
    }
    version = read_be16(steps + 4);
    if (version != STEPS_VERSION && version != STEPS_Z80_VERSION) {
        fprintf(stderr, "testcart: unsupported step table version or size\n");
        return 2;
    }
    (void)fixture_id;
    (void)z80_entry;

    for (i = 0; i < count; i++) {
        const uint8_t *record = steps + header_size + (size_t)i * STEPS_RECORD_SIZE;
        uint8_t operation = record[0];
        uint8_t width = record[1];
        uint16_t compare_mask = read_be16(record + 2);
        uint32_t addr = read_be32(record + 4);
        uint32_t value = read_be32(record + 8);
        if ((width != 1 && width != 2 && width != 4) ||
            (operation == STEP_TAS && width != 1) ||
            (operation == STEP_POLL && (width != 2 || !compare_mask)) ||
            (compare_mask && operation != STEP_READ && operation != STEP_POLL)) {
            fprintf(stderr, "testcart: invalid step %u\n", i);
            return 2;
        }
        if (operation == STEP_READ) {
            if (width == 1)
                (void)rd8(addr);
            else if (width == 2)
                (void)rd16(addr);
            else
                (void)rd32(addr);
        } else if (operation == STEP_WRITE) {
            if (width == 1)
                wr8(addr, (uint8_t)value);
            else if (width == 2)
                wr16(addr, (uint16_t)value);
            else
                wr32(addr, value);
        } else if (operation == STEP_TAS) {
            (void)nmd_tas(addr);
        } else if (operation == STEP_POLL) {
            while ((rd16(addr) & compare_mask) != (uint16_t)value) {
            }
        } else {
            fprintf(stderr, "testcart: unknown operation %u at step %u\n", operation, i);
            return 2;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *steps_path = NULL, *picture_path = NULL;
    const char *region = "U";
    unsigned frames = 2;
    uint8_t *rom, *steps;
    size_t rom_size, steps_size;
    uint16_t step_count, fixture_id, z80_entry;
    size_t step_header_size;
    nmd_cart cart;
    int i, result;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc)
            rom_path = argv[++i];
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc)
            steps_path = argv[++i];
        else if (!strcmp(argv[i], "--picture-out") && i + 1 < argc)
            picture_path = argv[++i];
        else if (!strcmp(argv[i], "--region") && i + 1 < argc)
            region = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = (unsigned)strtoul(argv[++i], NULL, 10);
        else {
            fprintf(stderr, "testcart: usage: testcart --rom ROM --steps TABLE [--picture-out PPM]\n");
            return 2;
        }
    }
    if (!rom_path || !steps_path) {
        fprintf(stderr, "testcart: usage: testcart --rom ROM --steps TABLE\n");
        return 2;
    }

    rom = read_file(rom_path, &rom_size);
    steps = read_file(steps_path, &steps_size);
    if (!rom || !steps) {
        free(rom);
        free(steps);
        return 2;
    }
    if (!parse_steps_header(steps, steps_size, &step_count, &fixture_id, &z80_entry,
                            &step_header_size)) {
        fprintf(stderr, "testcart: invalid step table header\n");
        free(rom);
        free(steps);
        return 2;
    }
    (void)step_count;
    (void)step_header_size;
    if (!rom_size || rom_size > 0x400000u) {
        fprintf(stderr, "testcart: ROM size must be between 1 byte and 4 MiB\n");
        free(rom);
        free(steps);
        return 2;
    }

    memset(&cart, 0, sizeof cart);
    cart.title = "nativemd testcart";
    if (!strcmp(region, "E"))
        cart.region = NMD_PAL;
    else if (!strcmp(region, "J") || !strcmp(region, "U"))
        cart.region = NMD_NTSC;
    else {
        fprintf(stderr, "testcart: unsupported region %s\n", region);
        free(rom);
        free(steps);
        return 2;
    }
    cart.rom = rom;
    cart.rom_size = (uint32_t)rom_size;
    if (!set_z80_fixture(&cart, fixture_id, z80_entry)) {
        fprintf(stderr, "testcart: unknown Z80 fixture %u at entry $%04X\n",
                (unsigned)fixture_id, z80_entry);
        free(rom);
        free(steps);
        return 2;
    }
    nmd_power(&cart);
    md_bus_trace_hook = trace_bus;
    md_z80_bus_trace_hook = trace_z80;
    md_vdp_trace_hook = trace_vdp;
    result = execute_steps(steps, steps_size);
    if (!result && picture_path) {
        unsigned frame;
        if (!frames || frames > 4) {
            fprintf(stderr, "testcart: frame count must be in 1-4\n");
            result = 2;
        }
        for (frame = 0; !result && frame < frames; frame++)
            nmd_run_frame();
    }
    if (!result && picture_path) {
        if (!write_picture(picture_path)) {
            fprintf(stderr, "testcart: cannot write picture %s\n", picture_path);
            result = 2;
        }
    }
    free(steps);
    free(rom);
    return result;
}
