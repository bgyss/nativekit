#include "nnes/machine.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

#define OK(call) CHECK((call) == NNES_OK)

typedef struct {
    uint8_t chr[0x2000];
    nnes_mirroring mirroring;
} ppu_cartridge;

typedef struct {
    unsigned reads;
    unsigned bad_events;
    uint16_t first_address;
    uint16_t last_address;
} dma_trace;

typedef struct {
    nnes_bus_access accesses[520];
    size_t count;
    uint8_t overflow;
} cpu_bus_trace;

typedef struct {
    unsigned polls;
    unsigned handler_calls;
} nmi_test_state;

static void record_cpu_bus(void *user, const nnes_bus_access *access)
{
    cpu_bus_trace *trace = user;
    if (trace->count == sizeof(trace->accesses) / sizeof(trace->accesses[0])) {
        trace->overflow = 1;
        return;
    }
    trace->accesses[trace->count++] = *access;
}

static nnes_result ppu_read(void *user, uint16_t address, uint8_t *value)
{
    ppu_cartridge *cart = user;
    if (address >= 0x2000 || !value) return NNES_INVALID_ARGUMENT;
    *value = cart->chr[address];
    return NNES_OK;
}

static nnes_result ppu_write(void *user, uint16_t address, uint8_t value)
{
    ppu_cartridge *cart = user;
    if (address >= 0x2000) return NNES_INVALID_ARGUMENT;
    cart->chr[address] = value;
    return NNES_OK;
}

static nnes_result ppu_get_mirroring(void *user, nnes_mirroring *mirroring)
{
    ppu_cartridge *cart = user;
    if (!mirroring) return NNES_INVALID_ARGUMENT;
    *mirroring = cart->mirroring;
    return NNES_OK;
}

static nnes_result dma_bus_access(void *user, nnes_bus_access *access)
{
    dma_trace *trace = user;
    uint16_t expected = (uint16_t)(0x8000 + trace->reads);
    if (access->operation != NNES_BUS_DMA_READ || access->address != expected ||
        trace->reads >= 256) {
        ++trace->bad_events;
        return NNES_UNSUPPORTED;
    }
    if (trace->reads == 0) trace->first_address = access->address;
    trace->last_address = access->address;
    access->value = (uint8_t)trace->reads;
    ++trace->reads;
    return NNES_OK;
}

static nnes_result nmi_vector_read(void *user, uint16_t address,
                                   uint8_t *value)
{
    (void)user;
    if (!value) return NNES_INVALID_ARGUMENT;
    if (address == 0xfffa) *value = 0x00;
    else if (address == 0xfffb) *value = 0x80;
    else if (address >= 0x4020) *value = 0xea;
    else return NNES_UNSUPPORTED;
    return NNES_OK;
}

static nnes_result nmi_handler(nnes *console, void *user)
{
    nmi_test_state *state = user;
    ++state->handler_calls;
    return nnes_write(console, 0x0200, 0x42);
}

static nnes_result nmi_wait_ready(nnes *console, void *user, int *ready)
{
    nmi_test_state *state = user;
    uint8_t flag = 0;
    nnes_result result = nnes_read(console, 0x0200, &flag);
    if (result != NNES_OK) return result;
    ++state->polls;
    *ready = flag == 0x42 || state->polls >= 2;
    return NNES_OK;
}

static void ppu_address(nnes *console, uint16_t address)
{
    OK(nnes_write(console, 0x2006, (uint8_t)(address >> 8)));
    OK(nnes_write(console, 0x2006, (uint8_t)address));
}

static uint8_t read_ppudata(nnes *console)
{
    uint8_t value = 0;
    OK(nnes_read(console, 0x2007, &value));
    return value;
}

static void test_register_mirroring_and_vblank(void)
{
    ppu_cartridge cart = {.mirroring = NNES_MIRROR_HORIZONTAL};
    nnes_cartridge descriptor = {0};
    descriptor.user = &cart;
    descriptor.ppu_read = ppu_read;
    descriptor.ppu_write = ppu_write;
    descriptor.ppu_mirroring = ppu_get_mirroring;
    nnes *console = NULL;
    OK(nnes_create(&descriptor, &console));

    uint8_t value = 0;
    OK(nnes_write(console, 0x2000, 0x04));
    OK(nnes_read(console, 0x2008, &value));
    CHECK(value == 0x04);
    OK(nnes_write(console, 0x2001, 0x1e));
    OK(nnes_read(console, 0x2009, &value));
    CHECK(value == 0x1e);

    /* NTSC vblank starts at scanline 241, dot 1. */
    OK(nnes_charge_cpu_cycles(console, 27394));
    OK(nnes_read(console, 0x2002, &value));
    CHECK((value & 0x80) != 0);
    /* PPUSTATUS is mirrored and reading it clears vblank and the $2005/$2006 latch. */
    OK(nnes_read(console, 0x200a, &value));
    CHECK((value & 0x80) == 0);

    nnes_destroy(console);
}

static void test_vblank_clear_and_nmi_latch(void)
{
    ppu_cartridge cart = {.mirroring = NNES_MIRROR_HORIZONTAL};
    nnes_cartridge descriptor = {0};
    descriptor.user = &cart;
    descriptor.ppu_read = ppu_read;
    descriptor.ppu_write = ppu_write;
    descriptor.ppu_mirroring = ppu_get_mirroring;
    nnes *console = NULL;
    OK(nnes_create(&descriptor, &console));

    OK(nnes_write(console, 0x2000, 0x80));
    OK(nnes_charge_cpu_cycles(console, 27394));
    int pending = 0;
    OK(nnes_ppu_take_nmi(console, &pending));
    CHECK(pending == 1);
    OK(nnes_ppu_take_nmi(console, &pending));
    CHECK(pending == 0);

    /* NTSC vblank ends at the start of the pre-render line. */
    OK(nnes_charge_cpu_cycles(console, 2274));
    uint8_t status = 0;
    OK(nnes_read(console, 0x2002, &status));
    CHECK((status & 0x80) == 0);
    nnes_destroy(console);
}

static void test_profile_work_advances_ppu_after_the_bus_sample(void)
{
    nnes_cost_profile profile = {.version = NNES_COST_PROFILE_VERSION};
    profile.cpu_tenths[0][NNES_KIND_IO] = 60;
    nnes_cartridge cartridge = {0};
    cartridge.cost_profile = &profile;
    nnes *console = NULL;
    OK(nnes_create(&cartridge, &console));
    /* One CPU cycle before vblank. The read's base bus cycle samples status
     * before its six-cycle native-work profile crosses the vblank edge. */
    OK(nnes_charge_cpu_cycles(console, 27392));
    uint64_t before = nnes_cpu_cycles(console);
    uint8_t status = 0;
    OK(nnes_read(console, 0x2002, &status));
    CHECK((status & 0x80) == 0);
    CHECK(nnes_cpu_cycles(console) - before == 7);
    OK(nnes_read(console, 0x2002, &status));
    CHECK((status & 0x80) != 0);
    nnes_destroy(console);
}

static void test_vram_buffer_increment_and_nametable_mirroring(void)
{
    ppu_cartridge cart = {.mirroring = NNES_MIRROR_HORIZONTAL};
    cart.chr[0x0002] = 0xa5;
    cart.chr[0x0022] = 0x5a;
    nnes_cartridge descriptor = {0};
    descriptor.user = &cart;
    descriptor.ppu_read = ppu_read;
    descriptor.ppu_write = ppu_write;
    descriptor.ppu_mirroring = ppu_get_mirroring;
    nnes *console = NULL;
    OK(nnes_create(&descriptor, &console));

    /* PPUDATA reads are buffered for pattern-table addresses. */
    ppu_address(console, 0x0002);
    CHECK(read_ppudata(console) == 0);
    CHECK(read_ppudata(console) == 0xa5);

    /* PPUCTRL bit 2 selects a 32-byte address increment. */
    OK(nnes_write(console, 0x2000, 0x04));
    ppu_address(console, 0x0002);
    OK(nnes_write(console, 0x2007, 0x11));
    OK(nnes_write(console, 0x2007, 0x22));
    CHECK(cart.chr[0x0002] == 0x11 && cart.chr[0x0022] == 0x22);

    /* Horizontal mirroring aliases $2400 to $2000. */
    OK(nnes_write(console, 0x2000, 0x00));
    ppu_address(console, 0x2000);
    OK(nnes_write(console, 0x2007, 0x37));
    ppu_address(console, 0x2400);
    (void)read_ppudata(console);
    CHECK(read_ppudata(console) == 0x37);

    nnes_destroy(console);
}

static void test_ppudata_palette_alias_and_status_latch_reset(void)
{
    ppu_cartridge cart = {.mirroring = NNES_MIRROR_SINGLE_LOWER};
    nnes_cartridge descriptor = {0};
    descriptor.user = &cart;
    descriptor.ppu_read = ppu_read;
    descriptor.ppu_write = ppu_write;
    descriptor.ppu_mirroring = ppu_get_mirroring;
    nnes *console = NULL;
    OK(nnes_create(&descriptor, &console));

    /* A PPUSTATUS read resets the shared PPUADDR write toggle. */
    OK(nnes_write(console, 0x2006, 0x3f));
    uint8_t status = 0;
    OK(nnes_read(console, 0x2002, &status));
    ppu_address(console, 0x3f10);
    OK(nnes_write(console, 0x2007, 0x2d));
    ppu_address(console, 0x3f00);
    CHECK(read_ppudata(console) == 0x2d);

    nnes_destroy(console);
}

static void test_oam_dma_transfer_and_alignment(void)
{
    for (unsigned alignment = 0; alignment < 2; ++alignment) {
        cpu_bus_trace trace = {0};
        nnes_cartridge cartridge = {0};
        cartridge.bus_trace = record_cpu_bus;
        cartridge.bus_trace_user = &trace;
        nnes *console = NULL;
        OK(nnes_create(&cartridge, &console));
        for (unsigned i = 0; i < 256; ++i)
            OK(nnes_write(console, (uint16_t)(0x0200 + i),
                          (uint8_t)(i ^ 0x5a)));
        OK(nnes_write(console, 0x2003, 0x80));
        if (alignment) OK(nnes_charge_cpu_cycles(console, 1));

        uint64_t bus_before = nnes_bus_cycles(console);
        uint64_t cpu_before = nnes_cpu_cycles(console);
        uint64_t write_cycle = cpu_before + 1;
        trace.count = 0;
        OK(nnes_write(console, 0x4014, 0x02));
        uint64_t dma_cycles = 515 - (write_cycle & 1);
        CHECK(nnes_bus_cycles(console) - bus_before == dma_cycles);
        CHECK(nnes_cpu_cycles(console) - cpu_before == dma_cycles);
        CHECK(!trace.overflow && trace.count == dma_cycles);
        CHECK(trace.accesses[0].operation == NNES_BUS_WRITE);
        CHECK(trace.accesses[0].address == 0x4014 &&
              trace.accesses[0].value == 0x02);
        CHECK(trace.accesses[1].operation == NNES_BUS_DUMMY_READ);
        size_t event = 2;
        if ((write_cycle & 1) == 0) {
            CHECK(trace.accesses[event].operation == NNES_BUS_DUMMY_READ);
            ++event;
        }
        for (unsigned i = 0; i < 256; ++i) {
            CHECK(trace.accesses[event].operation == NNES_BUS_DMA_READ);
            CHECK(trace.accesses[event].address == 0x0200 + i);
            CHECK(trace.accesses[event].value == (uint8_t)(i ^ 0x5a));
            ++event;
            CHECK(trace.accesses[event].operation == NNES_BUS_DMA_WRITE);
            CHECK(trace.accesses[event].address == 0x2004);
            CHECK(trace.accesses[event].value == (uint8_t)(i ^ 0x5a));
            ++event;
        }
        CHECK(event == trace.count);

        OK(nnes_write(console, 0x2003, 0x80));
        for (unsigned i = 0; i < 256; ++i) {
            uint8_t value = 0;
            OK(nnes_write(console, 0x2003, (uint8_t)(0x80 + i)));
            OK(nnes_read(console, 0x2004, &value));
            if (value != (uint8_t)(i ^ 0x5a))
                fprintf(stderr, "OAM DMA mismatch at byte %u: got %02X\n",
                        i, value);
            CHECK(value == (uint8_t)(i ^ 0x5a));
        }
        nnes_destroy(console);
    }
}

static void test_oam_dma_marks_cartridge_gets(void)
{
    dma_trace trace = {0};
    nnes_cartridge cartridge = {0};
    cartridge.user = &trace;
    cartridge.bus_access = dma_bus_access;
    nnes *console = NULL;
    OK(nnes_create(&cartridge, &console));
    OK(nnes_write(console, 0x4014, 0x80));
    CHECK(trace.reads == 256);
    CHECK(trace.bad_events == 0);
    CHECK(trace.first_address == 0x8000);
    CHECK(trace.last_address == 0x80ff);
    nnes_destroy(console);
}

static void test_wait_boundary_schedules_ppu_nmi(void)
{
    nmi_test_state state = {0};
    const nnes_routine routines[] = {{0x8000, nmi_handler, 0}};
    nnes_cartridge cartridge = {0};
    cartridge.user = &state;
    cartridge.read = nmi_vector_read;
    cartridge.routines = routines;
    cartridge.routine_count = 1;
    nnes *console = NULL;
    OK(nnes_create(&cartridge, &console));
    nnes_cpu(console)->pc = 0x9000;
    nnes_cpu(console)->s = 0xff;
    nnes_cpu(console)->p = NNES_P_U | NNES_P_I;
    OK(nnes_write(console, 0x2000, 0x80));
    OK(nnes_charge_cpu_cycles(console, 27394));

    uint64_t waited = 0;
    OK(nnes_wait_until(console, nmi_wait_ready, &state, 0, &waited));
    CHECK(state.polls == 2 && state.handler_calls == 1);
    CHECK(nnes_cpu(console)->pc == 0x9000);
    CHECK(nnes_cpu(console)->s == 0xff);
    CHECK(nnes_cpu(console)->p == (NNES_P_U | NNES_P_I));
    uint8_t flag = 0;
    OK(nnes_read(console, 0x0200, &flag));
    CHECK(flag == 0x42);
    int pending = 1;
    OK(nnes_ppu_take_nmi(console, &pending));
    CHECK(pending == 0);
    nnes_destroy(console);
}

int main(void)
{
    test_register_mirroring_and_vblank();
    test_vblank_clear_and_nmi_latch();
    test_profile_work_advances_ppu_after_the_bus_sample();
    test_vram_buffer_increment_and_nametable_mirroring();
    test_ppudata_palette_alias_and_status_latch_reset();
    test_oam_dma_transfer_and_alignment();
    test_oam_dma_marks_cartridge_gets();
    test_wait_boundary_schedules_ppu_nmi();
    puts("NES PPU register and memory contract passed");
    return 0;
}
