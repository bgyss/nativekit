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
    unsigned count;
    uint16_t addresses[6];
    uint8_t values[6];
} apu_write_trace;

typedef struct {
    unsigned count;
    nnes_bus_access accesses[2];
} indexed_apu_trace;

static void record_apu_write(void *user, const nnes_bus_access *access)
{
    apu_write_trace *trace = user;
    if (access->operation != NNES_BUS_WRITE ||
        (access->address < 0x4000 || access->address > 0x4017) ||
        access->address == 0x4014 || access->address == 0x4016)
        return;
    if (trace->count < 6) {
        trace->addresses[trace->count] = access->address;
        trace->values[trace->count] = access->value;
    }
    ++trace->count;
}

static void record_indexed_apu_access(void *user,
                                      const nnes_bus_access *access)
{
    indexed_apu_trace *trace = user;
    if (access->address != 0x4008) return;
    if (trace->count < 2) trace->accesses[trace->count] = *access;
    ++trace->count;
}

static nnes *create_console(void)
{
    nnes *console = NULL;
    OK(nnes_create(NULL, &console));
    return console;
}

static void test_frame_irq_read_clear_and_open_bus(void)
{
    nnes *console = create_console();
    uint8_t value = 0;

    /* The 4-step NTSC frame IRQ is active well before this sampled point. */
    OK(nnes_write(console, 0x0000, 0x20));
    OK(nnes_charge_cpu_cycles(console, 29840));
    OK(nnes_read(console, 0x4015, &value));
    CHECK(value == 0x60); /* prior open-bus bit 5 plus frame IRQ bit 6 */

    OK(nnes_read(console, 0x4015, &value));
    CHECK(value == 0x20); /* read cleared frame IRQ and did not replace open bus */

    nnes_destroy(console);
}

static void test_frame_irq_inhibit(void)
{
    nnes *console = create_console();
    uint8_t value = 0;

    OK(nnes_write(console, 0x4017, 0x40));
    OK(nnes_charge_cpu_cycles(console, 60000));
    OK(nnes_read(console, 0x4015, &value));
    CHECK((value & 0x40) == 0);

    nnes_destroy(console);
}

static void test_sound_driver_register_writes(void)
{
    apu_write_trace trace = {0};
    nnes_cartridge cartridge = {0};
    cartridge.bus_trace_user = &trace;
    cartridge.bus_trace = record_apu_write;
    nnes *console = NULL;
    OK(nnes_create(&cartridge, &console));

    const uint16_t addresses[] = {
        0x4010, 0x4017, 0x4015, 0x4015, 0x4001, 0x4005
    };
    const uint8_t values[] = {0x00, 0x00, 0x00, 0x0f, 0x08, 0x08};
    for (unsigned index = 0; index < 6; ++index)
        OK(nnes_write(console, addresses[index], values[index]));

    CHECK(trace.count == 6);
    for (unsigned index = 0; index < 6; ++index) {
        CHECK(trace.addresses[index] == addresses[index]);
        CHECK(trace.values[index] == values[index]);
    }
    uint8_t status = 0xff;
    OK(nnes_read(console, 0x4015, &status));
    CHECK(status == 0x00);
    nnes_destroy(console);
}

static void test_write_only_register_reads_use_cpu_open_bus(void)
{
    nnes *console = create_console();
    uint8_t value = 0;

    OK(nnes_write(console, 0x4008, 0x66));
    OK(nnes_write(console, 0x0000, 0x5a));
    OK(nnes_read(console, 0x4008, &value));
    CHECK(value == 0x5a);

    nnes_cpu(console)->pc = 0x9000;
    OK(nnes_wr8_absolute_indexed(console, 0x4000, 0x08, 0x60));
    nnes_destroy(console);
}

static void test_absolute_indexed_apu_store_seeds_operand_open_bus(void)
{
    indexed_apu_trace trace = {0};
    nnes_cartridge cartridge = {0};
    cartridge.bus_trace_user = &trace;
    cartridge.bus_trace = record_indexed_apu_access;
    nnes *console = NULL;
    OK(nnes_create(&cartridge, &console));

    OK(nnes_write(console, 0x0000, 0x5a));
    OK(nnes_wr8_absolute_indexed(console, 0x4000, 0x08, 0x60));

    CHECK(trace.count == 2);
    CHECK(trace.accesses[0].operation == NNES_BUS_DUMMY_READ);
    CHECK(trace.accesses[0].address == 0x4008);
    CHECK(trace.accesses[0].value == 0x40);
    CHECK(trace.accesses[1].operation == NNES_BUS_WRITE);
    CHECK(trace.accesses[1].address == 0x4008);
    CHECK(trace.accesses[1].value == 0x60);
    nnes_destroy(console);
}

int main(void)
{
    test_frame_irq_read_clear_and_open_bus();
    test_frame_irq_inhibit();
    test_sound_driver_register_writes();
    test_write_only_register_reads_use_cpu_open_bus();
    test_absolute_indexed_apu_store_seeds_operand_open_bus();
    puts("NES APU frame-status contract passed");
    return 0;
}
