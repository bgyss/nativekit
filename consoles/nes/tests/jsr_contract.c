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
    nnes_bus_access accesses[8];
    size_t count;
} trace_state;

static nnes_result read_rom(void *user, uint16_t address, uint8_t *value)
{
    (void)user;
    if (!value) return NNES_INVALID_ARGUMENT;
    (void)address;
    *value = 0xea;
    return NNES_OK;
}

static void trace_bus(void *user, const nnes_bus_access *access)
{
    trace_state *trace = user;
    if (trace->count < sizeof(trace->accesses) / sizeof(trace->accesses[0]))
        trace->accesses[trace->count++] = *access;
}

static nnes_result translated_target(nnes *console, void *user)
{
    (void)user;
    nnes_cpu(console)->a = 0x42;
    return NNES_OK;
}

int main(void)
{
    trace_state trace = {0};
    const nnes_routine routines[] = {{0x8000, translated_target, 0}};
    nnes_cartridge cartridge = {0};
    cartridge.user = &trace;
    cartridge.read = read_rom;
    cartridge.routines = routines;
    cartridge.routine_count = 1;
    cartridge.bus_trace_user = &trace;
    cartridge.bus_trace = trace_bus;
    nnes *console = NULL;
    OK(nnes_create(&cartridge, &console));
    nnes_cpu(console)->s = 0xff;

    uint64_t cycles_before = nnes_bus_cycles(console);
    OK(nnes_jsr_call(console, 0x8000, 0x8123, 0x8001));
    CHECK(nnes_bus_cycles(console) - cycles_before == 8);
    CHECK(nnes_cpu(console)->pc == 0x8124);
    CHECK(nnes_cpu(console)->s == 0xff);
    CHECK(nnes_cpu(console)->a == 0x42);
    CHECK(trace.count == 8);
    CHECK(trace.accesses[0].operation == NNES_BUS_DUMMY_READ &&
          trace.accesses[0].address == 0x01ff);
    CHECK(trace.accesses[1].operation == NNES_BUS_WRITE &&
          trace.accesses[1].address == 0x01ff && trace.accesses[1].value == 0x81);
    CHECK(trace.accesses[2].operation == NNES_BUS_WRITE &&
          trace.accesses[2].address == 0x01fe && trace.accesses[2].value == 0x23);
    CHECK(trace.accesses[3].operation == NNES_BUS_DUMMY_READ &&
          trace.accesses[3].address == 0x8002);
    CHECK(trace.accesses[4].operation == NNES_BUS_DUMMY_READ &&
          trace.accesses[4].address == 0x01fd);
    CHECK(trace.accesses[5].operation == NNES_BUS_READ &&
          trace.accesses[5].address == 0x01fe && trace.accesses[5].value == 0x23);
    CHECK(trace.accesses[6].operation == NNES_BUS_READ &&
          trace.accesses[6].address == 0x01ff && trace.accesses[6].value == 0x81);
    CHECK(trace.accesses[7].operation == NNES_BUS_DUMMY_READ &&
          trace.accesses[7].address == 0x8123);

    nnes_destroy(console);
    puts("NES native JSR/RTS stack contract passed");
    return 0;
}
