/* Native execution: the reset routine on the 68000 coroutine, frame resume,
 * guest-stack interrupt returns on both CPUs, restart and re-power. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nmd/nmd.h>

#include "md_internal.h"

#define RESET_SSP 0x00FFFE00u
#define RESET_PC 0x000200u
#define ALT_PC 0x000210u
#define VINT_PC 0x000300u
#define HINT_PC 0x000310u
#define COUNTER 0xFF0000u
#define VINT_COUNTER 0xFF0010u

extern int md_m68k_plant; /* dispatch.c, test builds: an error planted in the run */

static uint8_t rom[0x400];

static void (*main_body)(void);
static void (*alt_body)(void);
static void (*vint_body)(void);
static void (*hint_body)(void);
static void (*z80_main_body)(void);
static void (*z80_vint_body)(void);

static unsigned reset_calls, alt_calls, vint_calls, hint_calls;
static unsigned main_resumes;
static uint32_t seen_a7, seen_pc, seen_d0;
static uint16_t seen_sr;
static uint32_t handler_a7, frame_pc;
static uint16_t handler_sr, frame_sr;
static int in_handler;
static char order[16];
static unsigned order_count;

static unsigned z80_starts, z80_vints, z80_loops;
static uint16_t z80_handler_sp, z80_frame_pc, z80_after_sp;
static uint8_t z80_handler_iff1, z80_after_a2, z80_after_iff1;
static int z80_ei;

static void reset_routine(void) { reset_calls++; main_body(); }
static void alt_routine(void) { alt_calls++; alt_body(); }
static void vint_routine(void) { vint_calls++; if (vint_body) vint_body(); }
static void hint_routine(void) { hint_calls++; if (hint_body) hint_body(); }
static void z80_main_routine(void) { z80_starts++; z80_main_body(); }
static void z80_vint_routine(void) { z80_vints++; if (z80_vint_body) z80_vint_body(); }

static const nmd_routine routines[] = {
    { RESET_PC, reset_routine, "reset" },
    { ALT_PC, alt_routine, "alt" },
    { VINT_PC, vint_routine, "vint" },
    { HINT_PC, hint_routine, "hint" },
};

static const nmd_z80_routine z80_routines[] = {
    { 0x0000, z80_main_routine, "z80 main" },
    { 0x0038, z80_vint_routine, "z80 vint" },
};

static nmd_cart cart;

static void put32(uint32_t at, uint32_t value)
{
    rom[at + 0] = (uint8_t)(value >> 24);
    rom[at + 1] = (uint8_t)(value >> 16);
    rom[at + 2] = (uint8_t)(value >> 8);
    rom[at + 3] = (uint8_t)value;
}

static void setup(void (*body)(void))
{
    memset(rom, 0, sizeof rom);
    put32(0, RESET_SSP);
    put32(4, RESET_PC);
    put32(0x70, HINT_PC);
    put32(0x78, VINT_PC);
    memset(&cart, 0, sizeof cart);
    cart.title = "execution check";
    cart.rom = rom;
    cart.rom_size = sizeof rom;
    cart.routines = routines;
    cart.routine_count = sizeof routines / sizeof routines[0];
    cart.execution = NMD_EXECUTION_NATIVE;
    main_body = body;
    alt_body = NULL;
    vint_body = NULL;
    hint_body = NULL;
    z80_main_body = NULL;
    z80_vint_body = NULL;
    reset_calls = alt_calls = vint_calls = hint_calls = 0;
    main_resumes = 0;
    in_handler = 0;
    order_count = 0;
    memset(order, 0, sizeof order);
    z80_starts = z80_vints = z80_loops = 0;
    z80_ei = 0;
}

static void frames(unsigned count)
{
    while (count--)
        nmd_run_frame();
}

static uint16_t ram16(uint32_t addr)
{
    const uint8_t *ram = nmd_work_ram();
    return (uint16_t)((uint16_t)ram[addr & 0xFFFFu] << 8 | ram[(addr + 1u) & 0xFFFFu]);
}

static uint64_t digest(void)
{
    const uint8_t *ram = nmd_work_ram();
    uint64_t hash = 1469598103934665603ull ^ md_clock;
    unsigned i;
    for (i = 0; i < 0x10000u; i++)
        hash = (hash ^ ram[i]) * 1099511628211ull;
    return hash;
}

static void enable_vint(void)
{
    wr16(NMD_VDP_CONTROL, 0x8124);
}

static void wait_forever(void)
{
    for (;;)
        nmd_wait_vblank();
}

/* ---- reset entry and resume ---- */

static void boot_main(void)
{
    seen_a7 = nmd_cpu.a[7];
    seen_pc = nmd_cpu.pc;
    seen_sr = nmd_cpu.sr;
    wait_forever();
}

static void test_boots(void)
{
    setup(boot_main);
    nmd_power(&cart);
    assert(reset_calls == 0); /* power only loads the reset state */
    assert(nmd_cpu.a[7] == RESET_SSP && nmd_cpu.pc == RESET_PC && nmd_cpu.sr == 0x2700);
    frames(1);
    assert(reset_calls == 1);
    assert(seen_a7 == RESET_SSP && seen_pc == RESET_PC && seen_sr == 0x2700);
    assert(md_clock >= md_vdp_frame_clocks() - 38ull * MD_MASTER_CLOCKS_PER_LINE);
    frames(3);
    assert(reset_calls == 1);
}

static void counting_main(void)
{
    volatile uint16_t local = 0;
    for (;;) {
        nmd_wait_vblank();
        local++;
        main_resumes = local;
        wr16(COUNTER, local);
    }
}

static void test_resumes(void)
{
    unsigned i;
    setup(counting_main);
    nmd_power(&cart);
    for (i = 0; i < 5; i++) {
        uint64_t before = md_clock;
        frames(1);
        assert(md_clock > before);
        assert(ram16(COUNTER) == i); /* the wait that ends frame i+1 returns in the next */
        assert(main_resumes == i);
    }
}

static void test_host_mode_untouched(void)
{
    setup(counting_main);
    cart.execution = NMD_EXECUTION_HOST;
    nmd_power(&cart);
    assert(nmd_cpu.a[7] == 0 && nmd_cpu.pc == 0);
    frames(3);
    assert(reset_calls == 0 && ram16(COUNTER) == 0);
}

static void slow_vint(void)
{
    wr16(VINT_COUNTER, (uint16_t)vint_calls);
    in_handler = 1;
    nmd_wait_vblank();
    in_handler = 0;
}

static void irq_main(void)
{
    enable_vint();
    nmd_set_sr(0x2000);
    counting_main();
}

static void test_repower(void)
{
    uint64_t first, second;
    unsigned calls;
    setup(irq_main);
    vint_body = slow_vint;
    nmd_power(&cart);
    frames(3);
    first = digest();
    calls = vint_calls;
    assert(calls >= 1 && in_handler); /* suspended inside the handler */
    nmd_power(&cart);
    assert(nmd_cpu.a[7] == RESET_SSP && md_clock == 0);
    in_handler = 0;
    vint_calls = 0;
    frames(3);
    second = digest();
    assert(first == second && vint_calls == calls && reset_calls == 2);
    nmd_power(&cart); /* and twice with no frame between */
    nmd_power(&cart);
    vint_calls = 0;
    frames(3);
    assert(digest() == first && reset_calls == 3);
}

/* ---- 68000 interrupts ---- */

static void count_vint(void)
{
    wr16(VINT_COUNTER, (uint16_t)vint_calls);
}

static void test_irq_runs(void)
{
    setup(irq_main);
    vint_body = count_vint;
    nmd_power(&cart);
    frames(4);
    assert(vint_calls == 3 && ram16(VINT_COUNTER) == 3);
    assert(ram16(COUNTER) == 3); /* main went on beside the handler */
}

static void clobber_vint(void)
{
    nmd_cpu.d[0] = 0xABCD0000u | vint_calls;
}

static void register_main(void)
{
    enable_vint();
    nmd_set_sr(0x2000);
    nmd_cpu.d[0] = 1;
    nmd_wait_vblank();
    seen_d0 = nmd_cpu.d[0];
    wait_forever();
}

static void test_irq_keeps_registers(void)
{
    setup(register_main);
    vint_body = clobber_vint;
    nmd_power(&cart);
    frames(4);
    assert(seen_d0 == 0xABCD0001u); /* nothing but SR and PC is restored */

    /* 1.0's host-driven cart still gets its whole state back */
    setup(register_main);
    vint_body = clobber_vint;
    cart.execution = NMD_EXECUTION_HOST;
    nmd_power(&cart);
    nmd_cpu.a[7] = 0x00FF0000;
    nmd_cpu.sr = 0x2000;
    nmd_cpu.d[0] = 1;
    md_irq_request_at(6, 100);
    md_advance_to(100);
    md_irq_dispatch_pending();
    assert(vint_calls == 1 && nmd_cpu.d[0] == 1);
}

static void frame_vint(void)
{
    handler_a7 = nmd_cpu.a[7];
    handler_sr = nmd_cpu.sr;
    frame_sr = rd16(nmd_cpu.a[7]);
    frame_pc = rd32(nmd_cpu.a[7] + 2u);
}

static void frame_main(void)
{
    enable_vint();
    nmd_cpu.pc = 0x000234;
    nmd_set_sr(0x2010);
    nmd_wait_vblank();
    nmd_wait_vblank();
    seen_a7 = nmd_cpu.a[7];
    seen_sr = nmd_cpu.sr;
    seen_pc = nmd_cpu.pc;
    wait_forever();
}

static void test_irq_frame(void)
{
    const uint8_t *ram;
    setup(frame_main);
    vint_body = frame_vint;
    nmd_power(&cart);
    frames(4);
    assert(handler_a7 == RESET_SSP - 6u && handler_sr == 0x2610);
    assert(frame_sr == 0x2010 && frame_pc == 0x000234);
    assert(seen_a7 == RESET_SSP && seen_sr == 0x2010 && seen_pc == 0x000234);
    ram = nmd_work_ram();
    assert(ram[0xFDFA] == 0x20 && ram[0xFDFB] == 0x10); /* the frame is in guest memory */
    assert(ram[0xFDFE] == 0x02 && ram[0xFDFF] == 0x34);
}

static void mask_vint(void)
{
    wr16(nmd_cpu.a[7], (uint16_t)(rd16(nmd_cpu.a[7]) | 0x0700u));
}

static void test_irq_sr_edit(void)
{
    setup(frame_main);
    vint_body = mask_vint;
    nmd_power(&cart);
    frames(6);
    assert(seen_sr == 0x2710); /* the handler's edit of the stacked SR is what returns */
    assert(vint_calls == 1);
}

static void note(char c)
{
    assert(order_count < sizeof order - 1);
    order[order_count++] = c;
}

static void nest_hint(void)
{
    note('a');
    nmd_wait_vblank();
    note('c');
}

static void nest_vint(void)
{
    note('b');
}

static void nest_main(void)
{
    enable_vint();
    md_irq_request_at(4, md_clock + 1000u);
    nmd_set_sr(0x2000);
    wait_forever();
}

static void test_irq_nests(void)
{
    setup(nest_main);
    hint_body = nest_hint;
    vint_body = nest_vint;
    nmd_power(&cart);
    frames(2);
    assert(order_count >= 3 && !strncmp(order, "abc", 3));
}

static void user_main(void)
{
    enable_vint();
    nmd_cpu.usp = 0x00FF8000;
    nmd_set_sr(0x0000);
    seen_d0 = nmd_cpu.a[7];
    nmd_wait_vblank();
    nmd_wait_vblank();
    seen_a7 = nmd_cpu.a[7];
    seen_sr = nmd_cpu.sr;
    wait_forever();
}

static void test_irq_user_mode(void)
{
    setup(user_main);
    vint_body = frame_vint;
    nmd_power(&cart);
    frames(4);
    assert(seen_d0 == 0x00FF8000);
    assert(handler_a7 == RESET_SSP - 6u && handler_sr == 0x2600 && frame_sr == 0x0000);
    assert(seen_a7 == 0x00FF8000 && seen_sr == 0x0000);
    assert(nmd_cpu.ssp == RESET_SSP && nmd_cpu.usp == 0x00FF8000);
}

static void redirect_vint(void)
{
    if (vint_calls == 1)
        wr32(nmd_cpu.a[7] + 2u, ALT_PC);
}

static void alt_counts(void)
{
    seen_a7 = nmd_cpu.a[7];
    seen_pc = nmd_cpu.pc;
    for (;;) {
        nmd_wait_vblank();
        wr16(COUNTER + 2u, (uint16_t)(ram16(COUNTER + 2u) + 1u));
    }
}

static void test_return_elsewhere(void)
{
    uint16_t main_count;
    setup(irq_main);
    vint_body = redirect_vint;
    alt_body = alt_counts;
    nmd_power(&cart);
    frames(2);
    main_count = ram16(COUNTER);
    frames(4);
    assert(alt_calls == 1 && seen_pc == ALT_PC && seen_a7 == RESET_SSP);
    assert(ram16(COUNTER) == main_count); /* main's frames were dropped */
    assert(ram16(COUNTER + 2u) >= 2 && vint_calls >= 3);
}

static void deep(unsigned depth)
{
    volatile uint8_t pad[64];
    pad[0] = (uint8_t)depth;
    if (depth)
        deep(depth - 1u);
    else
        nmd_restart(ALT_PC | 0xFF000000u);
    (void)pad[0];
}

static void restart_main(void)
{
    nmd_wait_vblank();
    deep(3);
}

static void test_restart(void)
{
    setup(restart_main);
    alt_body = alt_counts;
    nmd_power(&cart);
    frames(4);
    assert(reset_calls == 1 && alt_calls == 1 && seen_pc == ALT_PC);
    assert(ram16(COUNTER + 2u) >= 1);
}

/* ---- Z80 ---- */

static int z80_poll(void *ctx)
{
    unsigned *polls = (unsigned *)ctx;
    return ++*polls >= 2000u;
}

static void z80_counting(void)
{
    nmd_z80.sp = 0x1FF0;
    for (;;) {
        unsigned polls = 0;
        z80_loops++;
        zwr(0x1000, (uint8_t)z80_loops);
        (void)nmd_z80_wait_until(z80_poll, &polls);
    }
}

static void z80_release_main(void)
{
    enable_vint();
    wr16(0xA11200, 0x0100);
    wr16(0xA11100, 0x0000);
    wait_forever();
}

static void test_z80_runs(void)
{
    uint8_t first;
    setup(z80_release_main);
    cart.z80_routines = z80_routines;
    cart.z80_routine_count = sizeof z80_routines / sizeof z80_routines[0];
    z80_main_body = z80_counting;
    nmd_power(&cart);
    frames(2);
    first = nmd_z80_ram()[0x1000];
    assert(z80_starts == 1 && first > 0);
    frames(2);
    assert(nmd_z80_ram()[0x1000] != first && z80_starts == 1);
}

static int z80_vint_seen(void *ctx)
{
    return z80_vints != *(unsigned *)ctx;
}

static void z80_irq_main(void)
{
    nmd_z80.sp = 0x1FF0;
    nmd_z80.im = 1;
    nmd_z80.a2 = 0x5A;
    nmd_z80.iff1 = nmd_z80.iff2 = 1;
    for (;;) {
        unsigned seen = z80_vints;
        nmd_z80.pc = 0x0123;
        (void)nmd_z80_wait_until(z80_vint_seen, &seen);
        z80_after_sp = nmd_z80.sp;
        z80_after_a2 = nmd_z80.a2;
        z80_after_iff1 = nmd_z80.iff1;
    }
}

static void z80_frame_vint(void)
{
    uint8_t swap;
    z80_handler_sp = nmd_z80.sp;
    z80_handler_iff1 = nmd_z80.iff1;
    z80_frame_pc = (uint16_t)(zrd(nmd_z80.sp) | (uint16_t)zrd((uint16_t)(nmd_z80.sp + 1u)) << 8);
    swap = nmd_z80.a; /* EX AF,AF' and back */
    nmd_z80.a = nmd_z80.a2;
    nmd_z80.a2 = swap;
    swap = nmd_z80.a;
    nmd_z80.a = nmd_z80.a2;
    nmd_z80.a2 = swap;
    if (z80_ei)
        nmd_z80.iff1 = nmd_z80.iff2 = 1;
}

static void z80_vint_case(int ei)
{
    setup(z80_release_main);
    cart.z80_routines = z80_routines;
    cart.z80_routine_count = sizeof z80_routines / sizeof z80_routines[0];
    z80_main_body = z80_irq_main;
    z80_vint_body = z80_frame_vint;
    z80_ei = ei;
    nmd_power(&cart);
    frames(5);
}

static void test_z80_vint_frame(void)
{
    const uint8_t *ram;
    z80_vint_case(0);
    assert(z80_vints == 1); /* no EI: the handler left interrupts off */
    assert(z80_handler_sp == 0x1FEE && z80_handler_iff1 == 0 && z80_frame_pc == 0x0123);
    assert(z80_after_sp == 0x1FF0 && z80_after_a2 == 0x5A);
    assert(z80_after_iff1 == 0 && nmd_z80.iff1 == 0 && nmd_z80.pc == 0x0123);
    ram = nmd_z80_ram();
    assert(ram[0x1FEE] == 0x23 && ram[0x1FEF] == 0x01); /* pushed on the Z80's stack */

    z80_vint_case(1);
    assert(z80_vints >= 3 && z80_after_sp == 0x1FF0 && z80_after_iff1 == 1);
}

/* ---- fatal cases: each must exit 70 with its message ---- */

static void returning_main(void)
{
    nmd_wait_vblank();
}

static void run_frame_main(void)
{
    nmd_run_frame();
}

static void power_main(void)
{
    nmd_power(&cart);
}

static void bad_return_vint(void)
{
    wr32(nmd_cpu.a[7] + 2u, 0x000555);
}

static void restart_missing_main(void)
{
    nmd_restart(0x000555);
}

static int fatal_case(const char *name)
{
    setup(boot_main);
    if (!strcmp(name, "missing_reset")) {
        put32(4, 0x000280);
    } else if (!strcmp(name, "returns")) {
        main_body = returning_main;
    } else if (!strcmp(name, "run_frame_inside")) {
        main_body = run_frame_main;
    } else if (!strcmp(name, "power_inside")) {
        main_body = power_main;
    } else if (!strcmp(name, "short_rom")) {
        cart.rom_size = 4;
    } else if (!strcmp(name, "bad_return")) {
        main_body = irq_main;
        vint_body = bad_return_vint;
    } else if (!strcmp(name, "restart_missing")) {
        main_body = restart_missing_main;
    } else if (!strcmp(name, "restart_host")) {
        nmd_power(&cart);
        nmd_restart(ALT_PC);
    } else if (!strcmp(name, "missing_vint")) {
        main_body = irq_main;
        put32(0x78, 0x000340);
    } else if (!strcmp(name, "z80_missing_vint")) {
        main_body = z80_release_main;
        cart.z80_routines = z80_routines;
        cart.z80_routine_count = 1;
        z80_main_body = z80_irq_main;
    } else {
        fprintf(stderr, "execution_check: unknown case %s\n", name);
        return 2;
    }
    nmd_power(&cart);
    frames(4);
    fprintf(stderr, "execution_check: %s did not fail\n", name);
    return 1;
}

static const struct {
    const char *name;
    void (*run)(void);
} cases[] = {
    { "boots", test_boots },
    { "resumes", test_resumes },
    { "host_mode_untouched", test_host_mode_untouched },
    { "repower", test_repower },
    { "irq_runs", test_irq_runs },
    { "irq_keeps_registers", test_irq_keeps_registers },
    { "irq_frame", test_irq_frame },
    { "irq_sr_edit", test_irq_sr_edit },
    { "irq_nests", test_irq_nests },
    { "irq_user_mode", test_irq_user_mode },
    { "return_elsewhere", test_return_elsewhere },
    { "restart", test_restart },
    { "z80_runs", test_z80_runs },
    { "z80_vint_frame", test_z80_vint_frame },
};

/* `case NAME [PLANT]` runs one passing case, with an error planted in the
 * run when PLANT is given: the runner expects that to fail. */
int main(int argc, char **argv)
{
    unsigned i;
    if (argc > 2 && !strcmp(argv[1], "case")) {
        if (argc > 3)
            md_m68k_plant = argv[3][0] - '0';
        for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            if (!strcmp(cases[i].name, argv[2])) {
                cases[i].run();
                printf("execution_check: %s ok\n", argv[2]);
                return 0;
            }
        }
        fprintf(stderr, "execution_check: unknown case %s\n", argv[2]);
        return 2;
    }
    if (argc > 1)
        return fatal_case(argv[1]);
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++)
        cases[i].run();
    puts("execution_check: ok");
    return 0;
}
