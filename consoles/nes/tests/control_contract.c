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
    uint8_t memory[65536];
    uint16_t reads[128];
    uint16_t writes[128];
    uint8_t write_values[128];
    size_t read_count;
    size_t write_count;
    nnes *console;
    nnes_result nested_interrupt_result;
    int probe_nested_interrupt;
    unsigned handler_count;
    uint16_t handler_pc;
    uint8_t handler_s;
    uint8_t handler_p;
    unsigned restart_entry_count;
    unsigned restart_target_count;
    uint8_t restart_target_s;
    uint16_t restart_target_pc;
    uint32_t switchable_code_id;
    uint32_t fixed_code_id;
    unsigned bank1_calls;
    unsigned bank2_calls;
    unsigned fixed_calls;
    int fail_code_identity;
} cart_state;

static nnes_result cart_read(void *user, uint16_t address, uint8_t *value)
{
    cart_state *state = user;
    if (state->read_count < sizeof(state->reads) / sizeof(state->reads[0]))
        state->reads[state->read_count++] = address;
    if (state->probe_nested_interrupt) {
        state->probe_nested_interrupt = 0;
        state->nested_interrupt_result =
            nnes_raise_interrupt(state->console, NNES_INT_NMI);
    }
    *value = state->memory[address];
    return NNES_OK;
}

static nnes_result cart_write(void *user, uint16_t address, uint8_t value)
{
    cart_state *state = user;
    if (state->write_count < sizeof(state->writes) / sizeof(state->writes[0])) {
        size_t i = state->write_count++;
        state->writes[i] = address;
        state->write_values[i] = value;
    }
    state->memory[address] = value;
    return NNES_OK;
}

static void clear_trace(cart_state *state)
{
    state->read_count = 0;
    state->write_count = 0;
}

static nnes_result interrupt_handler(nnes *console, void *user)
{
    cart_state *state = user;
    const nnes_cpu_state *cpu = nnes_cpu(console);
    ++state->handler_count;
    state->handler_pc = cpu->pc;
    state->handler_s = cpu->s;
    state->handler_p = cpu->p;
    return NNES_OK;
}

static nnes_result interrupt_rti_handler(nnes *console, void *user)
{
    cart_state *state = user;
    ++state->handler_count;
    return nnes_rti(console);
}

static nnes_result restart_entry(nnes *console, void *user)
{
    cart_state *state = user;
    ++state->restart_entry_count;
    nnes_cpu(console)->s = 0x82;
    return nnes_cpu_restart(console, 0x8100);
}

static nnes_result restart_target(nnes *console, void *user)
{
    cart_state *state = user;
    const nnes_cpu_state *cpu = nnes_cpu(console);
    ++state->restart_target_count;
    state->restart_target_s = cpu->s;
    state->restart_target_pc = cpu->pc;
    return NNES_OK;
}

static nnes_result resolve_code_identity(void *user, uint16_t address,
                                         uint32_t *code_id)
{
    cart_state *state = user;
    if (state->fail_code_identity) return NNES_UNSUPPORTED;
    if (address >= 0xc000) *code_id = state->fixed_code_id;
    else if (address >= 0x8000) *code_id = state->switchable_code_id;
    else *code_id = 0;
    return NNES_OK;
}

static nnes_result bank1_entry(nnes *console, void *user)
{
    (void)console;
    ++((cart_state *)user)->bank1_calls;
    return NNES_OK;
}

static nnes_result bank2_entry(nnes *console, void *user)
{
    (void)console;
    ++((cart_state *)user)->bank2_calls;
    return NNES_OK;
}

static nnes_result banked_restart_entry(nnes *console, void *user)
{
    cart_state *state = user;
    ++state->restart_entry_count;
    state->switchable_code_id = 2;
    return nnes_cpu_restart(console, 0x8123);
}

static nnes_result fixed_bank_entry(nnes *console, void *user)
{
    (void)console;
    ++((cart_state *)user)->fixed_calls;
    return NNES_OK;
}

static nnes_cartridge make_cart(cart_state *state,
                                const nnes_routine *routines,
                                size_t routine_count)
{
    nnes_cartridge cart;
    memset(&cart, 0, sizeof(cart));
    cart.user = state;
    cart.read = cart_read;
    cart.write = cart_write;
    cart.routines = routines;
    cart.routine_count = routine_count;
    return cart;
}

static void set_interrupt_vectors(cart_state *state)
{
    state->memory[0xfffa] = 0x00;
    state->memory[0xfffb] = 0x80;
    state->memory[0xfffc] = 0x00;
    state->memory[0xfffd] = 0x80;
    state->memory[0xfffe] = 0x00;
    state->memory[0xffff] = 0x82;
}

static uint8_t read_at(nnes *console, uint16_t address)
{
    uint8_t value = 0;
    OK(nnes_read(console, address, &value));
    return value;
}

static void test_instruction_stack_and_rti(void)
{
    cart_state state = {0};
    nnes_routine routines[] = {{0x8000, interrupt_handler}};
    nnes_cartridge cart = make_cart(&state, routines, 1);
    nnes *console = NULL;
    OK(nnes_create(&cart, &console));
    state.console = console;

    nnes_cpu_state *cpu = nnes_cpu(console);
    cpu->pc = 0x8000;
    cpu->s = 0xff;
    cpu->p = NNES_P_U;
    cpu->a = 0x80;
    uint64_t before = nnes_bus_cycles(console);
    OK(nnes_pha(console));
    CHECK(nnes_bus_cycles(console) - before == 2);
    CHECK(cpu->s == 0xfe);
    CHECK(read_at(console, 0x01ff) == 0x80);

    cpu->pc = 0x8000;
    cpu->p = NNES_P_U | NNES_P_C | NNES_P_V;
    before = nnes_bus_cycles(console);
    OK(nnes_pla(console));
    CHECK(nnes_bus_cycles(console) - before == 3);
    CHECK(cpu->a == 0x80 && cpu->s == 0xff);
    CHECK((cpu->p & (NNES_P_N | NNES_P_Z | NNES_P_C | NNES_P_V)) ==
          (NNES_P_N | NNES_P_C | NNES_P_V));

    cpu->pc = 0x8000;
    cpu->p = NNES_P_U | NNES_P_N | NNES_P_C;
    before = nnes_bus_cycles(console);
    OK(nnes_php(console));
    CHECK(nnes_bus_cycles(console) - before == 2);
    CHECK(cpu->s == 0xfe);
    CHECK(read_at(console, 0x01ff) ==
          (NNES_P_U | NNES_P_B | NNES_P_N | NNES_P_C));

    cpu->pc = 0x8000;
    cpu->p = 0;
    before = nnes_bus_cycles(console);
    OK(nnes_plp(console));
    CHECK(nnes_bus_cycles(console) - before == 3);
    CHECK(cpu->s == 0xff);
    CHECK(cpu->p == (NNES_P_U | NNES_P_N | NNES_P_C));

    OK(nnes_write(console, 0x01fd, NNES_P_B | NNES_P_V | NNES_P_C));
    OK(nnes_write(console, 0x01fe, 0x34));
    OK(nnes_write(console, 0x01ff, 0x12));
    cpu->pc = 0x9000;
    cpu->s = 0xfc;
    cpu->p = NNES_P_I;
    before = nnes_bus_cycles(console);
    OK(nnes_rti(console));
    CHECK(nnes_bus_cycles(console) - before == 5);
    CHECK(cpu->s == 0xff && cpu->pc == 0x1234);
    CHECK(cpu->p == (NNES_P_U | NNES_P_V | NNES_P_C));
    nnes_destroy(console);
}

static void test_interrupt_boundaries(void)
{
    cart_state state = {0};
    nnes_routine routines[] = {{0x8000, interrupt_handler},
                               {0x8200, interrupt_handler}};
    nnes_cartridge cart = make_cart(&state, routines, 2);
    nnes *console = NULL;
    set_interrupt_vectors(&state);
    OK(nnes_create(&cart, &console));
    state.console = console;

    nnes_cpu_state *cpu = nnes_cpu(console);
    cpu->pc = 0x8123;
    cpu->s = 0xff;
    cpu->p = NNES_P_U | NNES_P_N | NNES_P_C;
    cpu->a = 0x12;
    cpu->x = 0x34;
    cpu->y = 0x56;
    state.probe_nested_interrupt = 1;
    clear_trace(&state);
    uint64_t before = nnes_bus_cycles(console);
    OK(nnes_raise_interrupt(console, NNES_INT_NMI));
    CHECK(nnes_bus_cycles(console) - before == 12);
    CHECK(state.nested_interrupt_result == NNES_INVALID_SEQUENCE);
    CHECK(state.handler_pc == 0x8000 && state.handler_s == 0xfc);
    CHECK((state.handler_p & NNES_P_I) != 0);
    CHECK(cpu->pc == 0x8123 && cpu->s == 0xff);
    CHECK(cpu->a == 0x12 && cpu->x == 0x34 && cpu->y == 0x56);
    CHECK(cpu->p == (NNES_P_U | NNES_P_N | NNES_P_C));
    CHECK(read_at(console, 0x01ff) == 0x81);
    CHECK(read_at(console, 0x01fe) == 0x23);
    CHECK(read_at(console, 0x01fd) == (NNES_P_U | NNES_P_N | NNES_P_C));
    CHECK(state.read_count == 5);
    CHECK(state.reads[0] == 0x8123 && state.reads[1] == 0x8123);
    CHECK(state.reads[2] == 0xfffa && state.reads[3] == 0xfffb);
    CHECK(state.reads[4] == 0x8001);

    cpu->pc = 0x8123;
    cpu->s = 0xff;
    cpu->p = NNES_P_U | NNES_P_I;
    clear_trace(&state);
    before = nnes_bus_cycles(console);
    CHECK(nnes_raise_interrupt(console, NNES_INT_IRQ) == NNES_INTERRUPT_MASKED);
    CHECK(nnes_bus_cycles(console) == before && state.read_count == 0);

    cpu->pc = 0x8123;
    cpu->s = 0xff;
    cpu->p = NNES_P_U | NNES_P_V;
    clear_trace(&state);
    OK(nnes_raise_interrupt(console, NNES_INT_IRQ));
    CHECK(state.handler_pc == 0x8200);
    CHECK(read_at(console, 0x01fd) == (NNES_P_U | NNES_P_V));
    CHECK(cpu->pc == 0x8123 && cpu->s == 0xff);

    cpu->pc = 0x8123;
    cpu->s = 0xff;
    cpu->p = NNES_P_U | NNES_P_C;
    clear_trace(&state);
    OK(nnes_raise_interrupt(console, NNES_INT_BRK));
    CHECK(state.handler_pc == 0x8200);
    CHECK(read_at(console, 0x01ff) == 0x81);
    CHECK(read_at(console, 0x01fe) == 0x25);
    CHECK(read_at(console, 0x01fd) == (NNES_P_U | NNES_P_B | NNES_P_C));
    CHECK(cpu->pc == 0x8125 && cpu->s == 0xff);

    uint8_t value = 0;
    OK(nnes_rd8_rmw(console, 0x8000, &value));
    before = nnes_bus_cycles(console);
    CHECK(nnes_raise_interrupt(console, NNES_INT_NMI) == NNES_INVALID_SEQUENCE);
    CHECK(nnes_bus_cycles(console) == before);
    OK(nnes_wr8_rmw(console, 0x8000, (uint8_t)(value + 1)));
    CHECK(nnes_raise_interrupt(console, (nnes_interrupt_kind)99) ==
          NNES_INVALID_ARGUMENT);
    nnes_destroy(console);
}

static void test_interrupt_handler_can_return_with_rti(void)
{
    cart_state state = {0};
    nnes_routine routines[] = {{0x8000, interrupt_rti_handler}};
    nnes_cartridge cart = make_cart(&state, routines, 1);
    nnes *console = NULL;
    set_interrupt_vectors(&state);
    OK(nnes_create(&cart, &console));

    nnes_cpu_state *cpu = nnes_cpu(console);
    cpu->pc = 0x8123;
    cpu->s = 0xff;
    cpu->p = NNES_P_U | NNES_P_C;
    uint64_t before = nnes_bus_cycles(console);
    OK(nnes_raise_interrupt(console, NNES_INT_NMI));
    CHECK(nnes_bus_cycles(console) - before == 12);
    CHECK(state.handler_count == 1);
    CHECK(cpu->pc == 0x8123 && cpu->s == 0xff);
    CHECK(cpu->p == (NNES_P_U | NNES_P_C));
    nnes_destroy(console);
}

static void test_pending_nmi_interrupt_checkpoint(void)
{
    cart_state state = {0};
    nnes_routine routines[] = {{0x8000, interrupt_handler}};
    nnes_cartridge cart = make_cart(&state, routines, 1);
    nnes *console = NULL;
    set_interrupt_vectors(&state);
    OK(nnes_create(&cart, &console));

    nnes_cpu_state *cpu = nnes_cpu(console);
    cpu->pc = 0xff7a;
    cpu->s = 0xff;
    cpu->p = NNES_P_U | NNES_P_I | NNES_P_C;
    cpu->a = 0x12;
    cpu->x = 0x34;
    cpu->y = 0x56;
    OK(nnes_write(console, 0x2000, 0x80));
    OK(nnes_charge_cpu_cycles(console, 27394));

    int delivered = 0;
    uint64_t before = nnes_bus_cycles(console);
    OK(nnes_interrupt_checkpoint(console, &delivered));
    CHECK(delivered == 1);
    CHECK(nnes_bus_cycles(console) - before == 12);
    CHECK(state.handler_count == 1);
    CHECK(state.handler_pc == 0x8000 && state.handler_s == 0xfc);
    CHECK(cpu->pc == 0xff7a && cpu->s == 0xff);
    CHECK(cpu->a == 0x12 && cpu->x == 0x34 && cpu->y == 0x56);
    CHECK(cpu->p == (NNES_P_U | NNES_P_I | NNES_P_C));
    CHECK(read_at(console, 0x01ff) == 0xff);
    CHECK(read_at(console, 0x01fe) == 0x7a);

    delivered = 1;
    before = nnes_bus_cycles(console);
    OK(nnes_interrupt_checkpoint(console, &delivered));
    CHECK(delivered == 0 && state.handler_count == 1);
    CHECK(nnes_bus_cycles(console) == before);

    uint8_t original = 0;
    OK(nnes_rd8_rmw(console, 0x0100, &original));
    before = nnes_bus_cycles(console);
    delivered = 1;
    CHECK(nnes_interrupt_checkpoint(console, &delivered) ==
          NNES_INVALID_SEQUENCE);
    CHECK(delivered == 0 && nnes_bus_cycles(console) == before);
    OK(nnes_wr8_rmw(console, 0x0100, (uint8_t)(original + 1)));
    nnes_destroy(console);
}

static void test_restart_boundary(void)
{
    cart_state state = {0};
    nnes_routine routines[] = {{0x8000, restart_entry},
                               {0x8100, restart_target}};
    nnes_cartridge cart = make_cart(&state, routines, 2);
    nnes *console = NULL;
    set_interrupt_vectors(&state);
    state.memory[0xfffc] = 0x00;
    state.memory[0xfffd] = 0x80;
    OK(nnes_create(&cart, &console));
    state.console = console;
    CHECK(nnes_cpu_restart(console, 0x8100) == NNES_INVALID_SEQUENCE);
    OK(nnes_start(console));
    CHECK(state.restart_entry_count == 1);
    CHECK(state.restart_target_count == 1);
    CHECK(state.restart_target_s == 0xff);
    CHECK(state.restart_target_pc == 0x8100);
    CHECK(nnes_cpu(console)->pc == 0x8100);
    CHECK(nnes_cpu_restart(console, 0x8100) == NNES_INVALID_SEQUENCE);
    nnes_destroy(console);
}

static void test_banked_routine_identity(void)
{
    cart_state state = {0};
    state.switchable_code_id = 1;
    state.fixed_code_id = 3;
    nnes_routine routines[] = {
        {.address = 0x8123, .run = bank1_entry, .code_identity = 1},
        {.address = 0x8123, .run = bank2_entry, .code_identity = 2},
        {.address = 0xc123, .run = fixed_bank_entry, .code_identity = 3}
    };
    nnes_cartridge cart = make_cart(&state, routines, 3);
    cart.code_identity = resolve_code_identity;
    nnes *console = NULL;
    OK(nnes_create(&cart, &console));
    state.console = console;

    OK(nnes_call(console, 0x8123));
    CHECK(state.bank1_calls == 1 && state.bank2_calls == 0);
    state.switchable_code_id = 2;
    OK(nnes_call(console, 0x8123));
    CHECK(state.bank1_calls == 1 && state.bank2_calls == 1);
    OK(nnes_call(console, 0xc123));
    CHECK(state.fixed_calls == 1);

    state.switchable_code_id = 4;
    CHECK(nnes_call(console, 0x8123) == NNES_MISSING_ROUTINE);
    state.fail_code_identity = 1;
    CHECK(nnes_call(console, 0x8123) == NNES_UNSUPPORTED);
    CHECK(state.bank1_calls == 1 && state.bank2_calls == 1);

    state.fail_code_identity = 0;
    nnes_routine duplicate[] = {
        {.address = 0x8123, .run = bank1_entry, .code_identity = 1},
        {.address = 0x8123, .run = bank2_entry, .code_identity = 1}
    };
    cart.routines = duplicate;
    cart.routine_count = 2;
    nnes *bad = NULL;
    CHECK(nnes_create(&cart, &bad) == NNES_INVALID_ARGUMENT);
    CHECK(bad == NULL);

    nnes_routine reset_routines[] = {
        {.address = 0x8000, .run = banked_restart_entry, .code_identity = 1},
        {.address = 0x8123, .run = bank1_entry, .code_identity = 1},
        {.address = 0x8123, .run = bank2_entry, .code_identity = 2},
        {.address = 0xc123, .run = fixed_bank_entry, .code_identity = 3}
    };
    state.switchable_code_id = 1;
    state.fixed_code_id = 3;
    state.memory[0xfffc] = 0x00;
    state.memory[0xfffd] = 0x80;
    cart.routines = reset_routines;
    cart.routine_count = sizeof(reset_routines) / sizeof(reset_routines[0]);
    nnes_destroy(console);
    console = NULL;
    OK(nnes_create(&cart, &console));
    state.console = console;
    OK(nnes_start(console));
    CHECK(state.restart_entry_count == 1);
    CHECK(state.bank2_calls == 2);
    CHECK(nnes_cpu(console)->pc == 0x8123);
    CHECK(nnes_cpu(console)->s == 0xff);
    nnes_destroy(console);
}

int main(void)
{
    test_instruction_stack_and_rti();
    test_interrupt_boundaries();
    test_interrupt_handler_can_return_with_rti();
    test_pending_nmi_interrupt_checkpoint();
    test_restart_boundary();
    test_banked_routine_identity();
    puts("NES control-flow and stack contract passed");
    return 0;
}
