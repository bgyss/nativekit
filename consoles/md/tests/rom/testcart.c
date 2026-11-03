/* Execute the binary step table built from tests/rom/cases.py on nmd. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nmd/nmd.h>

#include "md_internal.h"

#define STEPS_MAGIC "NMDX"
#define STEPS_HEADER_SIZE 8u
#define STEPS_RECORD_SIZE 12u

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

static void trace_bus(uint64_t clock, uint8_t operation, uint8_t width,
                      uint32_t addr, uint32_t value)
{
    printf("%" PRIu64 "\t%u\t%u\t%08X\t%08X\n", clock, operation, width,
           (unsigned)addr, (unsigned)value);
}

static int execute_steps(const uint8_t *steps, size_t size)
{
    uint16_t version, count;
    unsigned i;
    if (size < STEPS_HEADER_SIZE || memcmp(steps, STEPS_MAGIC, 4)) {
        fprintf(stderr, "testcart: invalid step table header\n");
        return 2;
    }
    version = read_be16(steps + 4);
    count = read_be16(steps + 6);
    if (version != 1 || size != STEPS_HEADER_SIZE + (size_t)count * STEPS_RECORD_SIZE) {
        fprintf(stderr, "testcart: unsupported step table version or size\n");
        return 2;
    }

    for (i = 0; i < count; i++) {
        const uint8_t *record = steps + STEPS_HEADER_SIZE + (size_t)i * STEPS_RECORD_SIZE;
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
    const char *rom_path = NULL, *steps_path = NULL;
    uint8_t *rom, *steps;
    size_t rom_size, steps_size;
    nmd_cart cart;
    int i, result;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc)
            rom_path = argv[++i];
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc)
            steps_path = argv[++i];
        else {
            fprintf(stderr, "testcart: usage: testcart --rom ROM --steps TABLE\n");
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
    if (!rom_size || rom_size > 0x400000u) {
        fprintf(stderr, "testcart: ROM size must be between 1 byte and 4 MiB\n");
        free(rom);
        free(steps);
        return 2;
    }

    memset(&cart, 0, sizeof cart);
    cart.title = "nativemd testcart";
    cart.region = NMD_NTSC;
    cart.rom = rom;
    cart.rom_size = (uint32_t)rom_size;
    nmd_power(&cart);
    md_bus_trace_hook = trace_bus;
    result = execute_steps(steps, steps_size);
    free(steps);
    free(rom);
    return result;
}
