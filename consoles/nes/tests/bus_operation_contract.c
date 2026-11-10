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
} access_trace;

static nnes_result cartridge_access(void *user, nnes_bus_access *access)
{
    access_trace *trace = user;
    if (trace->count >= sizeof(trace->accesses) / sizeof(trace->accesses[0]))
        return NNES_UNSUPPORTED;
    if (access->operation == NNES_BUS_READ ||
        access->operation == NNES_BUS_DUMMY_READ) {
        access->value = trace->memory[access->address];
    } else {
        trace->memory[access->address] = access->value;
    }
    trace->accesses[trace->count++] = *access;
    return NNES_OK;
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

int main(void)
{
    access_trace trace = {0};
    trace.memory[0x8000] = 0x12;
    trace.memory[0x8001] = 0x34;
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
    CHECK(value == 0x00);
    CHECK(trace.count == 8);
    CHECK(trace.accesses[7].open_bus_before == 0xce);

    nnes_destroy(console);
    puts("NES bus operation metadata contract passed");
    return 0;
}
