#include "nnes/machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

#define OK(call) CHECK((call) == NNES_OK)

typedef struct {
    uint16_t reads[32];
    uint16_t writes[32];
    uint8_t write_values[32];
    size_t read_count;
    size_t write_count;
} bus_trace;

static nnes_result cart_read(void *user, uint16_t address, uint8_t *value)
{
    bus_trace *trace = user;
    if (trace->read_count >= sizeof(trace->reads) / sizeof(trace->reads[0]))
        return NNES_UNSUPPORTED;
    trace->reads[trace->read_count++] = address;
    switch (address) {
    case 0x8000: *value = 0x12; break;
    case 0x80ff: *value = 0x34; break;
    case 0x8100: *value = 0x5a; break;
    default: *value = 0xea; break;
    }
    return NNES_OK;
}

static nnes_result cart_write(void *user, uint16_t address, uint8_t value)
{
    bus_trace *trace = user;
    if (trace->write_count >= sizeof(trace->writes) / sizeof(trace->writes[0]))
        return NNES_UNSUPPORTED;
    size_t i = trace->write_count++;
    trace->writes[i] = address;
    trace->write_values[i] = value;
    return NNES_OK;
}

static void clear_trace(bus_trace *trace)
{
    trace->read_count = 0;
    trace->write_count = 0;
}

static void test_alu_and_addresses(void)
{
    nnes_alu8_result result = nnes_adc8(NNES_P_U, 0x7f, 0x01);
    CHECK(result.value == 0x80);
    CHECK((result.p & (NNES_P_N | NNES_P_V | NNES_P_C | NNES_P_Z)) ==
          (NNES_P_N | NNES_P_V));

    result = nnes_sbc8(NNES_P_U | NNES_P_C, 0x00, 0x01);
    CHECK(result.value == 0xff);
    CHECK((result.p & (NNES_P_N | NNES_P_C | NNES_P_Z)) == NNES_P_N);

    /* The 2A03 ignores D for arithmetic while preserving the flag itself. */
    result = nnes_adc8(NNES_P_U | NNES_P_D, 0x50, 0x50);
    CHECK(result.value == 0xa0);
    CHECK((result.p & (NNES_P_N | NNES_P_V | NNES_P_C | NNES_P_D)) ==
          (NNES_P_N | NNES_P_V | NNES_P_D));
    result = nnes_adc8(NNES_P_U, 0xff, 0x01);
    CHECK(result.value == 0);
    CHECK((result.p & (NNES_P_N | NNES_P_V | NNES_P_C | NNES_P_Z)) ==
          (NNES_P_C | NNES_P_Z));

    result = nnes_sbc8(NNES_P_U | NNES_P_C, 0x80, 0x01);
    CHECK(result.value == 0x7f);
    CHECK((result.p & (NNES_P_N | NNES_P_V | NNES_P_C | NNES_P_Z)) ==
          (NNES_P_V | NNES_P_C));
    CHECK((nnes_cmp8(NNES_P_U, 0x00, 0x01) &
           (NNES_P_N | NNES_P_C | NNES_P_Z)) == NNES_P_N);
    CHECK((nnes_cmp8(NNES_P_U, 0x80, 0x80) &
           (NNES_P_N | NNES_P_C | NNES_P_Z)) == (NNES_P_C | NNES_P_Z));
    CHECK((nnes_bit8(NNES_P_U, 0x00, 0xc0) &
           (NNES_P_N | NNES_P_V | NNES_P_Z)) ==
          (NNES_P_N | NNES_P_V | NNES_P_Z));

    result = nnes_lsr8(NNES_P_U | NNES_P_V, 0x03);
    CHECK(result.value == 0x01);
    CHECK((result.p & (NNES_P_C | NNES_P_V | NNES_P_N | NNES_P_Z)) ==
          (NNES_P_C | NNES_P_V));
    result = nnes_ror8(NNES_P_U | NNES_P_C, 0x00);
    CHECK(result.value == 0x80);
    CHECK((result.p & (NNES_P_C | NNES_P_N | NNES_P_Z)) == NNES_P_N);

    result = nnes_anc8(NNES_P_U, 0x80, 0xff);
    CHECK(result.value == 0x80);
    CHECK((result.p & (NNES_P_N | NNES_P_C | NNES_P_Z)) ==
          (NNES_P_N | NNES_P_C));
    nnes_las8_result las = nnes_las8(NNES_P_U, 0xff, 0x80);
    CHECK(las.a == 0x80 && las.x == 0x80 && las.s == 0x80);
    CHECK((las.p & NNES_P_N) != 0 && (las.p & NNES_P_Z) == 0);

    CHECK(nnes_zp_index(0xff, 1) == 0x00);
    nnes_indexed_address address = nnes_add_index16(0x80ff, 1);
    CHECK(address.base == 0x80ff);
    CHECK(address.address == 0x8100);
    CHECK(address.page_crossed == 1);
    CHECK(nnes_page_fixup_dummy_address(address.base, address.address) == 0x8000);
}

static void test_bus_helpers(void)
{
    bus_trace trace = {0};
    nnes_cost_profile profile = {.version = NNES_COST_PROFILE_VERSION};
    profile.cpu_tenths[0][NNES_KIND_ROM] = 2;
    nnes_cartridge cart = {
        .user = &trace,
        .read = cart_read,
        .write = cart_write,
        .cost_profile = &profile
    };
    nnes *console = NULL;
    OK(nnes_create(&cart, &console));

    uint8_t value = 0;
    uint64_t before = nnes_bus_cycles(console);
    OK(nnes_rd8_absolute_indexed(console, 0x80ff, 1, &value));
    CHECK(value == 0x5a);
    CHECK(nnes_bus_cycles(console) - before == 2);
    CHECK(trace.read_count == 2);
    CHECK(trace.reads[0] == 0x8000);
    CHECK(trace.reads[1] == 0x8100);

    /* Discarding a normal read retains profile counts; a dummy read does not. */
    nnes_access_counts counts;
    OK(nnes_reset_access_counts(console));
    clear_trace(&trace);
    before = nnes_bus_cycles(console);
    OK(nnes_rd8_discard(console, 0x8100));
    OK(nnes_rd8_dummy(console, 0x8100));
    CHECK(nnes_bus_cycles(console) - before == 2);
    CHECK(trace.read_count == 2);
    OK(nnes_get_access_counts(console, &counts));
    CHECK(counts.by_access[0][NNES_KIND_ROM] == 1);

    /* ($zp,X) wraps the pointer high byte within zero page. */
    OK(nnes_wr16_zp(console, 0xff, 0x8100));
    clear_trace(&trace);
    before = nnes_bus_cycles(console);
    OK(nnes_rd8_indirect_x(console, 0xff, 0, &value));
    CHECK(value == 0x5a);
    CHECK(nnes_bus_cycles(console) - before == 4);
    CHECK(trace.read_count == 1 && trace.reads[0] == 0x8100);

    /* A page-crossing ($zp),Y RMW preserves its address through the pair. */
    OK(nnes_wr16_zp(console, 0x20, 0x80ff));
    clear_trace(&trace);
    nnes_indexed_address resolved;
    uint8_t original = 0;
    before = nnes_bus_cycles(console);
    OK(nnes_rd8_indirect_y_rmw(console, 0x20, 1, &resolved, &original));
    CHECK(resolved.base == 0x80ff && resolved.address == 0x8100);
    CHECK(resolved.page_crossed == 1 && original == 0x5a);
    CHECK(nnes_bus_cycles(console) - before == 4);
    CHECK(trace.read_count == 2);
    CHECK(trace.reads[0] == 0x8000 && trace.reads[1] == 0x8100);
    before = nnes_bus_cycles(console);
    CHECK(nnes_rd8_discard(console, 0x8100) == NNES_INVALID_SEQUENCE);
    CHECK(nnes_bus_cycles(console) == before);
    OK(nnes_wr8_rmw(console, resolved.address, 0x99));
    CHECK(nnes_bus_cycles(console) - before == 2);
    CHECK(trace.write_count == 2);
    CHECK(trace.writes[0] == 0x8100 && trace.write_values[0] == 0x5a);
    CHECK(trace.writes[1] == 0x8100 && trace.write_values[1] == 0x99);

    /* JMP ($80FF) reads its high byte at $8000 on the NMOS 6502. */
    clear_trace(&trace);
    uint16_t target = 0;
    before = nnes_bus_cycles(console);
    OK(nnes_rd16_jmp_indirect(console, 0x80ff, &target));
    CHECK(target == 0x1234);
    CHECK(nnes_bus_cycles(console) - before == 2);
    CHECK(trace.read_count == 2);
    CHECK(trace.reads[0] == 0x80ff && trace.reads[1] == 0x8000);

    /* Word stack operations preserve 6502 high-then-low push order. */
    nnes_cpu_state *cpu = nnes_cpu(console);
    cpu->s = 0xff;
    before = nnes_bus_cycles(console);
    OK(nnes_push16(console, 0x1234));
    CHECK(cpu->s == 0xfd);
    CHECK(nnes_read(console, 0x01ff, &value) == NNES_OK && value == 0x12);
    CHECK(nnes_read(console, 0x01fe, &value) == NNES_OK && value == 0x34);
    uint16_t word = 0;
    OK(nnes_pull16(console, &word));
    CHECK(word == 0x1234 && cpu->s == 0xff);
    CHECK(nnes_bus_cycles(console) - before == 6);

    nnes_destroy(console);
}

int main(void)
{
    test_alu_and_addresses();
    test_bus_helpers();
    puts("NES machine helper contract passed");
    return 0;
}
