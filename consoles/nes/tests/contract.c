#include "nnes/machine.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define OK(x) CHECK((x) == NNES_OK)

struct cart_state { uint16_t address; uint8_t value; unsigned calls; int vectors; uint16_t fail_address; };
static nnes_result cart_read(void *user, uint16_t address, uint8_t *value) {
    struct cart_state *s = user;
    s->address = address;
    if (s->fail_address == address) return NNES_UNSUPPORTED;
    if (s->vectors && (address == 0xfffc || address == 0xfffd)) {
        *value = address == 0xfffc ? 0x00 : 0x80;
        return NNES_OK;
    }
    *value = s->value;
    return address == 0xffff ? NNES_UNSUPPORTED : NNES_OK;
}
static nnes_result cart_write(void *user, uint16_t address, uint8_t value) {
    struct cart_state *s = user;
    s->address = address; s->value = value;
    return address == 0xffff ? NNES_UNSUPPORTED : NNES_OK;
}
static nnes_result entry(nnes *n, void *user) {
    struct cart_state *s = user;
    ++s->calls;
    return nnes_write(n, 0x17ff, 0x91);
}
static nnes_result failed_entry(nnes *n, void *user) {
    (void)n; (void)user;
    return NNES_UNSUPPORTED;
}
static uint8_t read_at(nnes *n, uint16_t a) {
    uint8_t v = 0;
    OK(nnes_read(n, a, &v));
    return v;
}
int main(void) {
    nnes *a = NULL, *b = NULL;
    uint8_t v;
    struct cart_state state = {0};
    nnes_routine routines[] = {{0x8000, entry}, {0x9000, failed_entry}};
    nnes_cartridge cart = {&state, cart_read, cart_write, routines, 2};
    OK(nnes_create(&cart, &a));
    OK(nnes_create(NULL, &b));
    CHECK(nnes_start(b) == NNES_UNSUPPORTED);
    CHECK(nnes_start(NULL) == NNES_INVALID_ARGUMENT);
    state.value = 0x80; /* vector $8080: deliberately unregistered */
    CHECK(nnes_start(a) == NNES_MISSING_ROUTINE);
    /* The routine table is copied, not borrowed. */
    routines[0].run = NULL;
    nnes_cartridge saved_cart = cart;
    cart.read = NULL; cart.user = NULL; cart.routine_count = 0;
    state.vectors = 1;
    OK(nnes_start(a));
    CHECK(state.calls == 1);
    state.fail_address = 0xfffc;
    CHECK(nnes_start(a) == NNES_UNSUPPORTED);
    state.fail_address = 0xfffd;
    CHECK(nnes_start(a) == NNES_UNSUPPORTED);
    CHECK(state.calls == 1);
    state.fail_address = 0;
    cart = saved_cart;
    OK(nnes_call(a, 0x8000));
    CHECK(state.calls == 2);
    CHECK(read_at(a, 0x7ff) == 0x91);
    CHECK(read_at(a, 0xfff) == 0x91);
    CHECK(read_at(a, 0x1fff) == 0x91);
    CHECK(read_at(b, 0x7ff) == 0);
    OK(nnes_write(a, 0x1800, 0x53));
    CHECK(read_at(a, 0) == 0x53);
    CHECK(nnes_call(a, 0x9000) == NNES_UNSUPPORTED);
    CHECK(nnes_call(a, 0xabcd) == NNES_MISSING_ROUTINE);
    CHECK(nnes_call(b, 0x8000) == NNES_MISSING_ROUTINE);
    /* Cartridge range boundaries and callback failures. */
    OK(nnes_write(a, 0x4020, 0x37)); CHECK(state.address == 0x4020);
    CHECK(read_at(a, 0x8000) == 0x37); CHECK(state.address == 0x8000);
    CHECK(nnes_write(a, 0xffff, 0x68) == NNES_UNSUPPORTED);
    v = 0xaa; CHECK(nnes_read(a, 0xffff, &v) == NNES_UNSUPPORTED); CHECK(v == 0xaa);
    CHECK(nnes_read(b, 0x4020, &v) == NNES_UNSUPPORTED);
    CHECK(nnes_write(b, 0x4020, 0) == NNES_UNSUPPORTED);
    /* PPU CPU registers mirror every eight bytes. Unmapped CHR still fails. */
    OK(nnes_write(a, 0x2000, 0x04));
    CHECK(read_at(a, 0x2008) == 0x04);
    OK(nnes_write(a, 0x2001, 0x1e));
    CHECK(read_at(a, 0x2009) == 0x1e);
    OK(nnes_write(a, 0x2006, 0x00));
    OK(nnes_write(a, 0x2006, 0x00));
    v = 0xaa;
    CHECK(nnes_read(a, 0x2007, &v) == NNES_UNSUPPORTED);
    CHECK(v == 0xaa);
    CHECK(nnes_write(a, 0x2007, 0x55) == NNES_UNSUPPORTED);
    /* High strobe reads current A; low strobe reads a stable eight-bit snapshot. */
    OK(nnes_set_buttons(a, 0, NNES_A | NNES_SELECT | NNES_RIGHT));
    OK(nnes_set_buttons(a, 1, NNES_B | NNES_START));
    OK(nnes_write(a, 0x4016, 1));
    CHECK((read_at(a, 0x4016) & 1) == 1);
    CHECK((read_at(a, 0x4016) & 1) == 1);
    OK(nnes_set_buttons(a, 0, NNES_LEFT));
    CHECK((read_at(a, 0x4016) & 1) == 0);
    OK(nnes_write(a, 0x4016, 0));
    OK(nnes_set_buttons(a, 0, 0xff));
    /* Repeated low writes must not relatch. */
    OK(nnes_write(a, 0x4016, 2));
    for (unsigned i = 0; i < 8; ++i) {
        CHECK((read_at(a, 0x4016) & 1) == ((NNES_LEFT >> i) & 1));
        CHECK((read_at(a, 0x4017) & 1) ==
              (((NNES_B | NNES_START) >> i) & 1));
    }
    for (unsigned i = 0; i < 300; ++i)
        CHECK((read_at(a, 0x4016) & 1) == 1);
    CHECK((read_at(b, 0x4016) & 1) == 0);
    OK(nnes_write(a, 0x4016, 1)); OK(nnes_write(a, 0x4016, 0));
    for (unsigned i = 0; i < 8; ++i)
        CHECK((read_at(a, 0x4016) & 1) == 1);

    /* Controller reads drive their low controller lines while bits 7-5 keep
     * the CPU's open-bus state. Bits 4-1 are low for a standard pad. */
    OK(nnes_set_buttons(a, 0, NNES_A));
    OK(nnes_write(a, 0x4016, 1));
    OK(nnes_write(a, 0x4016, 0));
    OK(nnes_write(a, 0x0000, 0xbf));
    CHECK(read_at(a, 0x4016) == 0xa1);
    CHECK(read_at(a, 0x4016) == 0xa0);

    /* The absolute-load helper seeds the omitted high operand byte ($40),
     * which is the common upper-bus value observed for controller reads. */
    OK(nnes_set_buttons(a, 0, NNES_A));
    OK(nnes_write(a, 0x4016, 1));
    OK(nnes_write(a, 0x4016, 0));
    uint8_t controller_value = 0;
    OK(nnes_rd8_absolute(a, 0x4016, &controller_value));
    CHECK(controller_value == 0x41);
    OK(nnes_rd8_absolute(a, 0x4016, &controller_value));
    CHECK(controller_value == 0x40);
    /* Reject invalid public inputs and ambiguous dispatch tables. */
    CHECK(nnes_set_buttons(a, 2, 0) == NNES_INVALID_ARGUMENT);
    CHECK(nnes_set_buttons(NULL, 0, 0) == NNES_INVALID_ARGUMENT);
    CHECK(nnes_read(NULL, 0, &v) == NNES_INVALID_ARGUMENT);
    CHECK(nnes_read(a, 0, NULL) == NNES_INVALID_ARGUMENT);
    CHECK(nnes_write(NULL, 0, 0) == NNES_INVALID_ARGUMENT);
    CHECK(nnes_call(NULL, 0) == NNES_INVALID_ARGUMENT);
    CHECK(nnes_create(NULL, NULL) == NNES_INVALID_ARGUMENT);
    nnes *bad = a;
    CHECK(nnes_create(&cart, &bad) == NNES_INVALID_ARGUMENT); CHECK(bad == NULL);
    routines[0].run = entry; routines[1].address = 0x8000;
    CHECK(nnes_create(&cart, &bad) == NNES_INVALID_ARGUMENT); CHECK(bad == NULL);
    cart.routines = NULL;
    CHECK(nnes_create(&cart, &bad) == NNES_INVALID_ARGUMENT);
    cart.routine_count = (size_t)-1;
    CHECK(nnes_create(&cart, &bad) == NNES_INVALID_ARGUMENT);
    nnes_destroy(a); nnes_destroy(b); nnes_destroy(NULL);
    puts("NES foundation contract passed");
    return 0;
}
