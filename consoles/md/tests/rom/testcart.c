/* Execute the binary step table built from tests/rom/cases.py on nmd. */
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nmd/nmd.h>
#include <nk/resample.h>

#include "md_internal.h"
#include "render.h"
#include "sound.h"

#define STEPS_MAGIC "NMDX"
#define STEPS_RECORD_SIZE 12u

enum {
    STEPS_VERSION = 1,
    STEPS_Z80_VERSION = 2,
    Z80_FIXTURE_BANK_WINDOW = 1,
    Z80_FIXTURE_PSG_TONE = 2,
    Z80_FIXTURE_PSG_NOISE = 3
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

static uint64_t read_be64(const uint8_t *p)
{
    return (uint64_t)read_be32(p) << 32 | read_be32(p + 4);
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

static void z80_psg_tone_fixture(void)
{
    zout(0x7F, 0x90);
    zout(0x7F, 0x84);
    zout(0x7F, 0x12);
    zwr(0x0010, 0x5A);
    for (;;)
        (void)nmd_z80_wait_until(z80_never_ready, NULL);
}

static void z80_psg_noise_fixture(void)
{
    zout(0x7F, 0xE0);
    zout(0x7F, 0xE4);
    zout(0x7F, 0xF0);
    zwr(0x0010, 0xA5);
    for (;;)
        (void)nmd_z80_wait_until(z80_never_ready, NULL);
}

static const nmd_z80_routine z80_bank_window_routines[] = {
    { 0x0000, z80_bank_window_fixture, "z80_bank_window_fixture" },
};
static const nmd_z80_routine z80_psg_tone_routines[] = {
    { 0x0000, z80_psg_tone_fixture, "z80_psg_tone_fixture" },
};
static const nmd_z80_routine z80_psg_noise_routines[] = {
    { 0x0000, z80_psg_noise_fixture, "z80_psg_noise_fixture" },
};

static int set_z80_fixture(nmd_cart *cart, uint16_t fixture_id, uint16_t entry)
{
    const nmd_z80_routine *routines;
    size_t count;

    if (!fixture_id)
        return entry == 0;
    if (entry != 0)
        return 0;
    switch (fixture_id) {
    case Z80_FIXTURE_BANK_WINDOW:
        routines = z80_bank_window_routines;
        count = sizeof z80_bank_window_routines / sizeof z80_bank_window_routines[0];
        break;
    case Z80_FIXTURE_PSG_TONE:
        routines = z80_psg_tone_routines;
        count = sizeof z80_psg_tone_routines / sizeof z80_psg_tone_routines[0];
        break;
    case Z80_FIXTURE_PSG_NOISE:
        routines = z80_psg_noise_routines;
        count = sizeof z80_psg_noise_routines / sizeof z80_psg_noise_routines[0];
        break;
    default:
        return 0;
    }
    cart->z80_routines = routines;
    cart->z80_routine_count = count;
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

static int write_psg_samples(const char *path, double output_rate, unsigned frames)
{
    Resampler *resampler = NULL;
    int16_t *mono = NULL, *stereo = NULL, *pcm = NULL;
    float *output = NULL;
    FILE *file = NULL;
    uint64_t source_count = md_psg.sample_count;
    int source_frames, pushed, pulled;
    size_t i;
    int valid = 1;

    if (!path || !output_rate || !frames || source_count > INT_MAX || frames > INT_MAX / 2u)
        return 0;
    source_frames = (int)source_count;
    mono = (int16_t *)malloc((size_t)source_frames * sizeof *mono);
    stereo = (int16_t *)malloc((size_t)source_frames * 2u * sizeof *stereo);
    output = (float *)malloc((size_t)frames * 2u * sizeof *output);
    pcm = (int16_t *)malloc((size_t)frames * 2u * sizeof *pcm);
    if (!mono || !stereo || !output || !pcm) {
        fprintf(stderr, "testcart: cannot allocate PSG conversion buffers\n");
        valid = 0;
        goto done;
    }
    if (md_psg_take_samples(mono, source_frames) != source_frames) {
        fprintf(stderr, "testcart: PSG source sample count changed while draining\n");
        valid = 0;
        goto done;
    }
    for (i = 0; i < source_count; i++) {
        stereo[i * 2u] = mono[i];
        stereo[i * 2u + 1u] = mono[i];
    }

    resampler = resample_new(md_psg_source_rate(), output_rate);
    if (!resampler) {
        fprintf(stderr, "testcart: cannot create PSG resampler\n");
        valid = 0;
        goto done;
    }
    pushed = resample_push(resampler, stereo, source_frames);
    if (pushed != source_frames) {
        fprintf(stderr, "testcart: PSG resampler accepted %d of %d source frames\n",
                pushed, source_frames);
        valid = 0;
        goto done;
    }
    pulled = resample_pull(resampler, output, (int)frames);
    if (pulled != (int)frames) {
        fprintf(stderr, "testcart: PSG resampler produced %d of %u output frames\n",
                pulled, frames);
        valid = 0;
        goto done;
    }
    for (i = 0; i < (size_t)frames * 2u; i++) {
        float sample = output[i];
        if (sample >= 1.0f)
            pcm[i] = 0x7FFF;
        else if (sample <= -1.0f)
            pcm[i] = (int16_t)-0x8000;
        else
            pcm[i] = (int16_t)(sample * 0x7FFF);
    }
    file = fopen(path, "wb");
    if (!file) {
        valid = 0;
        goto done;
    }
    if (fwrite(pcm, sizeof *pcm, (size_t)frames * 2u, file) != (size_t)frames * 2u)
        valid = 0;
    if (fclose(file))
        valid = 0;
    file = NULL;

done:
    if (file)
        fclose(file);
    if (resampler)
        resample_free(resampler);
    free(mono);
    free(stereo);
    free(output);
    free(pcm);
    return valid;
}

static int replay_psg_events(const char *path, nmd_region region)
{
    uint8_t *data;
    size_t size;
    uint32_t count;
    uint64_t end_clock, last_clock = 0;
    uint32_t i;

    data = read_file(path, &size);
    if (!data)
        return 0;
    if (size < 18u || memcmp(data, "NPSG", 4) || read_be16(data + 4) != 1) {
        free(data);
        return 0;
    }
    count = read_be32(data + 6);
    end_clock = read_be64(data + 10);
    if (size != 18u + (size_t)count * 9u) {
        free(data);
        return 0;
    }
    md_psg_reset(region);
    for (i = 0; i < count; i++) {
        const uint8_t *record = data + 18u + (size_t)i * 9u;
        uint64_t clock = read_be64(record);
        if (clock < last_clock || clock > end_clock) {
            free(data);
            return 0;
        }
        md_psg_write(record[8], clock);
        last_clock = clock;
    }
    md_psg_advance_to(end_clock);
    free(data);
    return 1;
}

static int replay_ym_events(const char *path, nmd_region region)
{
    uint8_t *data;
    size_t size;
    uint32_t count;
    uint64_t end_clock, last_clock = 0;
    uint32_t i;

    data = read_file(path, &size);
    if (!data)
        return 0;
    if (size < 18u || memcmp(data, "NYM1", 4) || read_be16(data + 4) != 1) {
        free(data);
        return 0;
    }
    count = read_be32(data + 6);
    end_clock = read_be64(data + 10);
    if (size != 18u + (size_t)count * 11u) {
        free(data);
        return 0;
    }
    md_ym_reset(region);
    for (i = 0; i < count; i++) {
        const uint8_t *record = data + 18u + (size_t)i * 11u;
        uint64_t clock = read_be64(record);
        uint8_t bank = record[8];
        uint8_t address = record[9];
        uint8_t value = record[10];
        if (clock < last_clock || clock > end_clock || bank > 1u) {
            free(data);
            return 0;
        }
        md_ym_write((uint8_t)(bank * 2u), address, clock);
        md_ym_write((uint8_t)(bank * 2u + 1u), value, clock);
        last_clock = clock;
    }
    free(data);
    return 1;
}

static int write_ym_samples(const char *path, double output_rate, unsigned frames,
                            uint64_t master_clock)
{
    Resampler *resampler = NULL;
    int16_t *stereo = NULL, *pcm = NULL;
    float *output = NULL;
    FILE *file = NULL;
    uint64_t source_count;
    int source_frames, pushed, pulled;
    size_t i;
    int valid = 1;

    if (!path || !(output_rate > 0.0) || !frames || frames > INT_MAX / 2u)
        return 0;
    md_ym_advance_to(master_clock);
    source_count = md_ym.sample_count;
    if (source_count > INT_MAX)
        return 0;
    source_frames = (int)source_count;
    stereo = (int16_t *)malloc((size_t)source_frames * 2u * sizeof *stereo);
    output = (float *)malloc((size_t)frames * 2u * sizeof *output);
    pcm = (int16_t *)malloc((size_t)frames * 2u * sizeof *pcm);
    if (!stereo || !output || !pcm) {
        fprintf(stderr, "testcart: cannot allocate YM2612 conversion buffers\n");
        valid = 0;
        goto done;
    }
    if (md_ym_take_stereo_samples(stereo, source_frames) != source_frames) {
        fprintf(stderr, "testcart: YM2612 source sample count changed while draining\n");
        valid = 0;
        goto done;
    }

    resampler = resample_new(md_ym_source_rate(), output_rate);
    if (!resampler) {
        fprintf(stderr, "testcart: cannot create YM2612 resampler\n");
        valid = 0;
        goto done;
    }
    pushed = resample_push(resampler, stereo, source_frames);
    if (pushed != source_frames) {
        fprintf(stderr, "testcart: YM2612 resampler accepted %d of %d source frames\n",
                pushed, source_frames);
        valid = 0;
        goto done;
    }
    pulled = resample_pull(resampler, output, (int)frames);
    if (pulled != (int)frames) {
        fprintf(stderr, "testcart: YM2612 resampler produced %d of %u output frames\n",
                pulled, frames);
        valid = 0;
        goto done;
    }
    for (i = 0; i < (size_t)frames * 2u; i++) {
        float sample = output[i];
        if (sample >= 1.0f)
            pcm[i] = 0x7FFF;
        else if (sample <= -1.0f)
            pcm[i] = (int16_t)-0x8000;
        else
            pcm[i] = (int16_t)(sample * 0x7FFF);
    }
    file = fopen(path, "wb");
    if (!file) {
        valid = 0;
        goto done;
    }
    if (fwrite(pcm, sizeof *pcm, (size_t)frames * 2u, file) != (size_t)frames * 2u)
        valid = 0;
    if (fclose(file))
        valid = 0;
    file = NULL;

done:
    if (file)
        fclose(file);
    if (resampler)
        resample_free(resampler);
    free(stereo);
    free(output);
    free(pcm);
    return valid;
}

static int write_ym_source_samples(const char *path, unsigned frames,
                                   uint64_t render_clock)
{
    int16_t *samples;
    FILE *file;
    int valid = 1;

    if (!path || !frames || frames > INT_MAX / 2u)
        return 0;
    md_ym_advance_to(render_clock);
    if (md_ym.sample_count < frames)
        return 0;
    samples = (int16_t *)malloc((size_t)frames * 2u * sizeof *samples);
    if (!samples)
        return 0;
    if (md_ym_take_stereo_samples(samples, (int)frames) != (int)frames) {
        free(samples);
        return 0;
    }
    file = fopen(path, "wb");
    if (!file) {
        free(samples);
        return 0;
    }
    if (fwrite(samples, sizeof *samples, (size_t)frames * 2u, file) !=
        (size_t)frames * 2u)
        valid = 0;
    if (fclose(file))
        valid = 0;
    free(samples);
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
            (operation == STEP_POLL && ((width != 1 && width != 2) || !compare_mask)) ||
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
            if (width == 1) {
                while ((rd8(addr) & compare_mask) != (uint8_t)value) {
                }
            } else {
                while ((rd16(addr) & compare_mask) != (uint16_t)value) {
                }
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
    const char *psg_path = NULL, *psg_event_path = NULL;
    const char *ym_path = NULL, *ym_event_path = NULL;
    const char *ym_source_path = NULL;
    const char *region = "U";
    unsigned frames = 2;
    unsigned psg_frames = 0;
    double psg_rate = 0;
    unsigned ym_frames = 0;
    unsigned ym_source_frames = 0;
    double ym_rate = 0;
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
        else if (!strcmp(argv[i], "--psg-out") && i + 1 < argc)
            psg_path = argv[++i];
        else if (!strcmp(argv[i], "--psg-events") && i + 1 < argc)
            psg_event_path = argv[++i];
        else if (!strcmp(argv[i], "--ym-out") && i + 1 < argc)
            ym_path = argv[++i];
        else if (!strcmp(argv[i], "--ym-events") && i + 1 < argc)
            ym_event_path = argv[++i];
        else if (!strcmp(argv[i], "--ym-source-out") && i + 1 < argc)
            ym_source_path = argv[++i];
        else if (!strcmp(argv[i], "--ym-source-frames") && i + 1 < argc)
            ym_source_frames = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--ym-rate") && i + 1 < argc)
            ym_rate = strtod(argv[++i], NULL);
        else if (!strcmp(argv[i], "--ym-frames") && i + 1 < argc)
            ym_frames = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--psg-rate") && i + 1 < argc)
            psg_rate = strtod(argv[++i], NULL);
        else if (!strcmp(argv[i], "--psg-frames") && i + 1 < argc)
            psg_frames = (unsigned)strtoul(argv[++i], NULL, 10);
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
    if (psg_path && (!(psg_rate > 0.0) || !psg_frames)) {
        fprintf(stderr, "testcart: --psg-out requires --psg-rate and --psg-frames\n");
        return 2;
    }
    if (psg_event_path && !psg_path) {
        fprintf(stderr, "testcart: --psg-events requires --psg-out\n");
        return 2;
    }
    if (ym_path && (!(ym_rate > 0.0) || !ym_frames)) {
        fprintf(stderr, "testcart: --ym-out requires --ym-rate and --ym-frames\n");
        return 2;
    }
    if (ym_event_path && !ym_path && !ym_source_path) {
        fprintf(stderr, "testcart: --ym-events requires YM audio output\n");
        return 2;
    }
    if (ym_source_path && (!ym_source_frames || !ym_event_path)) {
        fprintf(stderr, "testcart: --ym-source-out requires --ym-source-frames and --ym-events\n");
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
    if (!result && psg_event_path && !replay_psg_events(psg_event_path, cart.region)) {
        fprintf(stderr, "testcart: invalid PSG replay event file %s\n", psg_event_path);
        result = 2;
    }
    if (!result && ym_event_path && !replay_ym_events(ym_event_path, cart.region)) {
        fprintf(stderr, "testcart: invalid YM2612 replay event file %s\n", ym_event_path);
        result = 2;
    }
    if (!result && (picture_path || (psg_path && !psg_event_path) ||
                    (ym_path && !ym_event_path) ||
                    (ym_source_path && !ym_event_path))) {
        unsigned frame;
        if (!frames || frames > 4) {
            fprintf(stderr, "testcart: frame count must be in 1-4\n");
            result = 2;
        }
        for (frame = 0; !result && frame < frames; frame++)
            nmd_run_frame();
    }
    if (!result && psg_path) {
        if (!write_psg_samples(psg_path, psg_rate, psg_frames)) {
            fprintf(stderr, "testcart: cannot write PSG samples %s\n", psg_path);
            result = 2;
        }
    }
    if (!result && ym_path) {
        const uint64_t master_clock_hz = cart.region == NMD_PAL ?
            53203395ull : 53693175ull;
        uint64_t end_clock = (uint64_t)((double)ym_frames * master_clock_hz / ym_rate + 0.5);
        uint64_t render_clock = end_clock + 64u * 1008u;
        if (!write_ym_samples(ym_path, ym_rate, ym_frames, render_clock)) {
            fprintf(stderr, "testcart: cannot write YM2612 samples %s\n", ym_path);
            result = 2;
        }
    }
    if (!result && ym_source_path) {
        uint64_t render_clock = ((uint64_t)ym_source_frames + 64u) * 1008u;
        if (!write_ym_source_samples(ym_source_path, ym_source_frames, render_clock)) {
            fprintf(stderr, "testcart: cannot write YM2612 source samples %s\n",
                    ym_source_path);
            result = 2;
        }
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
