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
    uint8_t memory[65536];
    nnes_bus_access accesses[16];
    size_t count;
    int record_bus;
} access_trace;

static nnes_result cartridge_access(void *user, nnes_bus_access *access)
{
    access_trace *trace = user;
    if (trace->count >= sizeof(trace->accesses) / sizeof(trace->accesses[0]))
        return NNES_UNSUPPORTED;
    if (access->operation == NNES_BUS_READ ||
        access->operation == NNES_BUS_DUMMY_READ ||
        access->operation == NNES_BUS_OPCODE_FETCH ||
        access->operation == NNES_BUS_OPERAND_FETCH) {
        access->value = trace->memory[access->address];
    } else {
        trace->memory[access->address] = access->value;
    }
    trace->accesses[trace->count++] = *access;
    return NNES_OK;
}

static void trace_bus_access(void *user, const nnes_bus_access *access)
{
    access_trace *trace = user;
    if (!trace || !access || !trace->record_bus) return;
    CHECK(trace->count < sizeof(trace->accesses) /
                          sizeof(trace->accesses[0]));
    trace->accesses[trace->count++] = *access;
}

static void check_access(const access_trace *trace, size_t index,
                         nnes_bus_operation operation, uint16_t address,
                         uint8_t value, uint64_t cycle_tenths,
                         uint8_t flags, uint8_t open_bus_before)
{
    const nnes_bus_access *access = &trace->accesses[index];
    CHECK(access->operation == operation);
    CHECK(access->address == address);
    CHECK(access->value == value);
    CHECK(access->cpu_cycle_tenths == cycle_tenths);
    CHECK(access->flags == flags);
    CHECK(access->open_bus_before == open_bus_before);
}

static void test_explicit_profile_cost_override(void)
{
    access_trace trace = {0};
    trace.memory[0x8000] = 0x12;
    nnes_cost_profile profile = {.version = NNES_COST_PROFILE_VERSION};
    profile.cpu_tenths[0][NNES_KIND_ROM] = 463;
    nnes_cartridge cartridge = {0};
    cartridge.user = &trace;
    cartridge.bus_access = cartridge_access;
    cartridge.cost_profile = &profile;
    nnes *console = NULL;
    OK(nnes_create(&cartridge, &console));

    uint8_t value = 0;
    OK(nnes_read_with_profile_cost(console, 0x8000, &value, 110));
    CHECK(value == 0x12);
    CHECK(nnes_cpu_cycle_tenths(console) == 120);
    check_access(&trace, 0, NNES_BUS_READ, 0x8000, 0x12, 120, 0, 0x00);

    OK(nnes_set_cpu_costs(console, 0));
    OK(nnes_read_with_profile_cost(console, 0x8000, &value, 110));
    CHECK(nnes_cpu_cycle_tenths(console) == 130);
    check_access(&trace, 1, NNES_BUS_READ, 0x8000, 0x12, 130, 0, 0x12);

    nnes_destroy(console);
}

static void test_explicit_profile_cost_override_without_base_profile(void)
{
    access_trace trace = {0};
    trace.memory[0x8000] = 0x34;
    nnes_cartridge cartridge = {0};
    cartridge.user = &trace;
    cartridge.bus_access = cartridge_access;
    nnes *console = NULL;
    OK(nnes_create(&cartridge, &console));

    uint8_t value = 0;
    OK(nnes_read_with_profile_cost(console, 0x8000, &value, 110));
    CHECK(value == 0x34);
    CHECK(nnes_cpu_cycle_tenths(console) == 120);
    check_access(&trace, 0, NNES_BUS_READ, 0x8000, 0x34, 120, 0, 0x00);

    nnes_destroy(console);
}

static void test_explicit_write_profile_cost_override(void)
{
    access_trace trace = {0};
    nnes_cost_profile profile = {.version = NNES_COST_PROFILE_VERSION};
    profile.cpu_tenths[1][NNES_KIND_WRAM] = 33;
    nnes_cartridge cartridge = {0};
    cartridge.user = &trace;
    cartridge.bus_access = cartridge_access;
    trace.record_bus = 1;
    cartridge.bus_trace_user = &trace;
    cartridge.bus_trace = trace_bus_access;
    cartridge.cost_profile = &profile;
    nnes *console = NULL;
    OK(nnes_create(&cartridge, &console));

    OK(nnes_write_with_profile_cost(console, 0x0001, 0x42, 20));
    CHECK(nnes_cpu_cycle_tenths(console) == 30);
    check_access(&trace, 0, NNES_BUS_WRITE, 0x0001, 0x42, 30, 0, 0x00);

    OK(nnes_set_cpu_costs(console, 0));
    trace.record_bus = 0;
    uint8_t readback = 0;
    OK(nnes_read(console, 0x0001, &readback));
    CHECK(readback == 0x42);
    trace.record_bus = 1;
    OK(nnes_write_with_profile_cost(console, 0x0001, 0x43, 20));
    CHECK(nnes_cpu_cycle_tenths(console) == 50);
    check_access(&trace, 1, NNES_BUS_WRITE, 0x0001, 0x43, 50, 0, 0x42);

    nnes_destroy(console);

    access_trace no_profile_trace = {0};
    no_profile_trace.record_bus = 1;
    nnes_cartridge no_profile_cartridge = {0};
    no_profile_cartridge.user = &no_profile_trace;
    no_profile_cartridge.bus_trace_user = &no_profile_trace;
    no_profile_cartridge.bus_trace = trace_bus_access;
    OK(nnes_create(&no_profile_cartridge, &console));
    OK(nnes_write_with_profile_cost(console, 0x0002, 0x44, 20));
    CHECK(nnes_cpu_cycle_tenths(console) == 30);
    check_access(&no_profile_trace, 0, NNES_BUS_WRITE, 0x0002, 0x44,
                 30, 0, 0x00);
    nnes_destroy(console);
}

int main(void)
{
    access_trace trace = {0};
    trace.memory[0x8000] = 0x12;
    trace.memory[0x8001] = 0x34;
    trace.memory[0x8002] = 0x40;
    trace.memory[0xa000] = 0x56;
    nnes_cartridge cartridge = {0};
    cartridge.user = &trace;
    cartridge.bus_access = cartridge_access;
    nnes *console = NULL;
    OK(nnes_create(&cartridge, &console));

    uint8_t value = 0;
    OK(nnes_read(console, 0x8000, &value));
    CHECK(value == 0x12);
    OK(nnes_rd8_dummy(console, 0x8001));
    OK(nnes_write(console, 0x9000, 0xaa));
    OK(nnes_wr8_dummy(console, 0x9001, 0xbb));
    OK(nnes_rd8_rmw(console, 0xa000, &value));
    CHECK(value == 0x56);
    OK(nnes_wr8_rmw(console, 0xa000, 0x57));

    CHECK(trace.count == 7);
    check_access(&trace, 0, NNES_BUS_READ, 0x8000, 0x12, 10, 0, 0x00);
    check_access(&trace, 1, NNES_BUS_DUMMY_READ, 0x8001, 0x34, 20, 0, 0x12);
    check_access(&trace, 2, NNES_BUS_WRITE, 0x9000, 0xaa, 30, 0, 0x34);
    check_access(&trace, 3, NNES_BUS_DUMMY_WRITE, 0x9001, 0xbb, 40, 0, 0xaa);
    check_access(&trace, 4, NNES_BUS_READ, 0xa000, 0x56, 50,
                 NNES_BUS_ACCESS_RMW_PAIR, 0xbb);
    check_access(&trace, 5, NNES_BUS_DUMMY_WRITE, 0xa000, 0x56, 60,
                 NNES_BUS_ACCESS_RMW_PAIR, 0x56);
    check_access(&trace, 6, NNES_BUS_WRITE, 0xa000, 0x57, 70,
                 NNES_BUS_ACCESS_RMW_PAIR, 0x56);

    OK(nnes_write(console, 0x0000, 0xce));
    OK(nnes_read(console, 0x0000, &value));
    CHECK(value == 0xce);
    OK(nnes_read(console, 0x8002, &value));
    CHECK(value == 0x40);
    CHECK(trace.count == 8);
    CHECK(trace.accesses[7].open_bus_before == 0xce);

    OK(nnes_rd8_operand(console, 0x8002, &value));
    CHECK(value == 0x40);
    CHECK(trace.count == 9);
    check_access(&trace, 8, NNES_BUS_OPERAND_FETCH, 0x8002, 0x40, 110,
                 0, 0x40);

    nnes_destroy(console);
    test_explicit_profile_cost_override();
    test_explicit_profile_cost_override_without_base_profile();
    test_explicit_write_profile_cost_override();
    puts("NES bus operation metadata contract passed");
    return 0;
}
