/*
 * replay.c: the game-facing nmd bus and CPU state, served from one recorded
 * call of the original. Test-only: it is linked with a game's C routines
 * into the hybrid library that check_calls.py drives, never into a game.
 *
 * Memory model. ROM reads come from the image. Work RAM and Z80 RAM hold the
 * call's entry contents where the original's accesses reveal them (a byte it
 * read before writing); a C read of any other byte before the C wrote it is a
 * difference, since the original did not depend on it. Every other address
 * is hardware: the C's accesses must be the original's, in order, with the
 * same address, width and written value, and reads return what the original
 * read. A 68000 longword is two word transfers, as recorded.
 */
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#include <stddef.h>

#include "nmd/cart.h"
#include "nmd/machine.h"
#include "nmd/z80.h"
#include "replay.h"

/* the game's tables (hybrid.py) */
extern const nmd_routine nmd_game_routines[];
extern const size_t nmd_game_routine_count;
extern const nmd_z80_routine nmd_game_z80_routines[];
extern const size_t nmd_game_z80_routine_count;

#define MAX_CALLS 65536u

nmd_cpu_state nmd_cpu;
nmd_z80_state nmd_z80;

#define WORK_SIZE 0x10000u
#define ZRAM_SIZE 0x2000u
#define KNOWN 1u
#define WRITTEN 2u

static const uint8_t *rom;
static uint32_t rom_size;
static uint8_t work[WORK_SIZE], work_state[WORK_SIZE];
static uint8_t zram[ZRAM_SIZE], zram_state[ZRAM_SIZE];
static const nmd_check_access *hardware;
static uint32_t hardware_count, hardware_next;
static int active_cpu;
static nmd_check_result result;
static jmp_buf stop;
static int running;
static nmd_check_fallback fallback;
static nmd_check_call calls[MAX_CALLS];
static uint32_t call_count;
static uint32_t work_written[WORK_SIZE], zram_written[ZRAM_SIZE];   /* offsets, in first-write order */
static uint32_t work_written_count, zram_written_count;

static void fail(int kind, uint32_t address, uint32_t width, uint32_t value, int write)
{
    result.status = kind;
    result.address = address;
    result.width = width;
    result.value = value;
    result.write = write;
    result.hardware_index = hardware_next;
    if (running)
        longjmp(stop, 1);
}

void nmd_check_reset(const uint8_t *image, uint32_t size)
{
    rom = image;
    rom_size = size;
    memset(work_state, 0, sizeof work_state);
    memset(zram_state, 0, sizeof zram_state);
    memset(&result, 0, sizeof result);
    memset(&nmd_cpu, 0, sizeof nmd_cpu);
    memset(&nmd_z80, 0, sizeof nmd_z80);
    hardware = 0;
    hardware_count = hardware_next = 0;
    fallback = 0;
    call_count = 0;
    work_written_count = zram_written_count = 0;
}

static void store(int space, uint32_t offset, uint8_t value)
{
    if (space == NMD_CHECK_WORK) {
        if (!(work_state[offset] & WRITTEN))
            work_written[work_written_count++] = offset;
        work[offset] = value;
        work_state[offset] |= WRITTEN;
    } else {
        if (!(zram_state[offset] & WRITTEN))
            zram_written[zram_written_count++] = offset;
        zram[offset] = value;
        zram_state[offset] |= WRITTEN;
    }
}

const uint32_t *nmd_check_written(int space, uint32_t *count)
{
    *count = space == NMD_CHECK_WORK ? work_written_count : zram_written_count;
    return space == NMD_CHECK_WORK ? work_written : zram_written;
}

static void record_call(int cpu, uint32_t address, int through_original)
{
    if (call_count == MAX_CALLS)
        fail(NMD_CHECK_UNSUPPORTED, address, 0, 0, 0);
    calls[call_count].cpu = (uint8_t)cpu;
    calls[call_count].address = address;
    calls[call_count].fallback = (uint8_t)through_original;
    call_count++;
}

/* The game's C is compiled with -finstrument-functions: every function entry
 * arrives here, and the ones that are table routines are the calls it made. */
void __cyg_profile_func_enter(void *function, void *site)
{
    size_t index;

    (void)site;
    if (!running)
        return;
    for (index = 0; index < nmd_game_routine_count; index++)
        if ((void *)nmd_game_routines[index].abi == function) {
            record_call(NMD_CHECK_M68K, nmd_game_routines[index].addr, 0);
            return;
        }
    for (index = 0; index < nmd_game_z80_routine_count; index++)
        if ((void *)nmd_game_z80_routines[index].abi == function) {
            record_call(NMD_CHECK_Z80, nmd_game_z80_routines[index].addr, 0);
            return;
        }
}

void __cyg_profile_func_exit(void *function, void *site)
{
    (void)function;
    (void)site;
}

uint32_t nmd_check_calls(const nmd_check_call **list)
{
    *list = calls;
    return call_count;
}

void nmd_check_entry_byte(int space, uint32_t offset, uint8_t value)
{
    if (space == NMD_CHECK_WORK && offset < WORK_SIZE) {
        work[offset] = value;
        work_state[offset] = KNOWN;
    } else if (space == NMD_CHECK_ZRAM && offset < ZRAM_SIZE) {
        zram[offset] = value;
        zram_state[offset] = KNOWN;
    }
}

int nmd_check_exit_byte(int space, uint32_t offset)
{
    if (space == NMD_CHECK_WORK && offset < WORK_SIZE && (work_state[offset] & WRITTEN))
        return work[offset];
    if (space == NMD_CHECK_ZRAM && offset < ZRAM_SIZE && (zram_state[offset] & WRITTEN))
        return zram[offset];
    return -1;
}

void nmd_check_hardware(int cpu, const nmd_check_access *list, uint32_t count)
{
    active_cpu = cpu;
    hardware = list;
    hardware_count = count;
    hardware_next = 0;
}

void nmd_check_set_fallback(nmd_check_fallback handler)
{
    fallback = handler;
}

uint32_t nmd_check_hardware_next(void)
{
    return hardware_next;
}

void nmd_check_hardware_skip(uint32_t count)
{
    hardware_next += count;
    if (hardware_next > hardware_count)
        fail(NMD_CHECK_PAST_RECORD, 0, 0, 0, 0);
}

void nmd_check_ram_store(int space, uint32_t offset, uint8_t value)
{
    if ((space == NMD_CHECK_WORK && offset < WORK_SIZE) ||
            (space == NMD_CHECK_ZRAM && offset < ZRAM_SIZE))
        store(space, offset, value);
}

const nmd_check_result *nmd_check_run(void (*routine)(void))
{
    result.status = NMD_CHECK_OK;
    running = 1;
    if (!setjmp(stop))
        routine();
    running = 0;
    result.hardware_consumed = hardware_next;
    return &result;
}

void nmd_check_call_fallback(int cpu, uint32_t address)
{
    record_call(cpu, address, 1);
    if (!fallback || !fallback(cpu, address))
        fail(NMD_CHECK_NO_ROUTINE, address, 0, 0, 0);
    result.fallbacks++;
}

static uint32_t hardware_access(uint32_t address, uint32_t width, uint32_t value, int write)
{
    const nmd_check_access *expected;

    if (hardware_next >= hardware_count)
        fail(NMD_CHECK_PAST_RECORD, address, width, value, write);
    expected = &hardware[hardware_next];
    if (expected->address != address || expected->width != width || expected->write != write)
        fail(NMD_CHECK_ACCESS, address, width, value, write);
    if (write && expected->value != value)
        fail(NMD_CHECK_VALUE, address, width, value, write);
    hardware_next++;
    return expected->value;
}

/* 68000 */

static int is_work(uint32_t address)
{
    return address >= 0xE00000u;
}

static uint8_t ram_read(uint8_t *bytes, uint8_t *state, uint32_t offset, uint32_t address)
{
    if (!state[offset])
        fail(NMD_CHECK_UNKNOWN_READ, address, 1, 0, 0);
    return bytes[offset];
}

static uint8_t m68k_byte(uint32_t address)
{
    address &= 0xFFFFFFu;
    if (address < rom_size)
        return rom[address];
    if (is_work(address))
        return ram_read(work, work_state, address & 0xFFFFu, address);
    fail(NMD_CHECK_UNMAPPED, address, 1, 0, 0);
    return 0;
}

static int m68k_memory(uint32_t address)
{
    address &= 0xFFFFFFu;
    return address < rom_size || is_work(address);
}

uint8_t rd8(uint32_t addr)
{
    result.accesses++;
    if (m68k_memory(addr))
        return m68k_byte(addr);
    return (uint8_t)hardware_access(addr & 0xFFFFFFu, 1, 0, 0);
}

uint16_t rd16(uint32_t addr)
{
    result.accesses++;
    if (addr & 1)
        fail(NMD_CHECK_ODD, addr, 2, 0, 0);
    if (m68k_memory(addr))
        return (uint16_t)(m68k_byte(addr) << 8 | m68k_byte(addr + 1));
    return (uint16_t)hardware_access(addr & 0xFFFFFFu, 2, 0, 0);
}

uint32_t rd32(uint32_t addr)
{
    uint32_t high = rd16(addr);

    return high << 16 | rd16(addr + 2);
}

static void m68k_store(uint32_t address, uint8_t value)
{
    address &= 0xFFFFFFu;
    if (!is_work(address))
        fail(NMD_CHECK_ROM_WRITE, address, 1, value, 1);
    store(NMD_CHECK_WORK, address & 0xFFFFu, value);
}

void wr8(uint32_t addr, uint8_t value)
{
    result.accesses++;
    if (m68k_memory(addr))
        m68k_store(addr, value);
    else
        hardware_access(addr & 0xFFFFFFu, 1, value, 1);
}

void wr16(uint32_t addr, uint16_t value)
{
    result.accesses++;
    if (addr & 1)
        fail(NMD_CHECK_ODD, addr, 2, value, 1);
    if (m68k_memory(addr)) {
        m68k_store(addr, (uint8_t)(value >> 8));
        m68k_store(addr + 1, (uint8_t)value);
    } else {
        hardware_access(addr & 0xFFFFFFu, 2, value, 1);
    }
}

void wr32(uint32_t addr, uint32_t value)
{
    wr16(addr, (uint16_t)(value >> 16));
    wr16(addr + 2, (uint16_t)value);
}

uint8_t nmd_tas(uint32_t addr)
{
    uint8_t value = rd8(addr);

    wr8(addr, value | 0x80);
    return value;
}

void nmd_set_sr(uint16_t sr)
{
    if ((sr ^ nmd_cpu.sr) & 0x2000) {
        uint32_t other = (sr & 0x2000) ? nmd_cpu.ssp : nmd_cpu.usp;

        if (sr & 0x2000)
            nmd_cpu.usp = nmd_cpu.a[7];
        else
            nmd_cpu.ssp = nmd_cpu.a[7];
        nmd_cpu.a[7] = other;
    }
    nmd_cpu.sr = sr;
}

uint64_t nmd_wait_until(int (*ready)(void *ctx), void *ctx)
{
    uint64_t polls = 0;

    /* every poll that is not ready makes hardware reads; the record ends them */
    while (!ready(ctx))
        if (++polls > 100000000u)
            fail(NMD_CHECK_UNSUPPORTED, 0, 0, 0, 0);
    return polls;
}

void nmd_wait_vblank(void)
{
    fail(NMD_CHECK_UNSUPPORTED, 0, 0, 0, 0);
}

void nmd_stop(void)
{
    fail(NMD_CHECK_UNSUPPORTED, 0, 0, 0, 0);
}

void nmd_restart(uint32_t addr)
{
    fail(NMD_CHECK_UNSUPPORTED, addr, 0, 0, 0);
    for (;;) {
    }
}

/* Z80 */

uint8_t zrd(uint16_t addr)
{
    result.accesses++;
    if (addr < ZRAM_SIZE)
        return ram_read(zram, zram_state, addr, addr);
    return (uint8_t)hardware_access(addr, 1, 0, 0);
}

void zwr(uint16_t addr, uint8_t value)
{
    result.accesses++;
    if (addr < ZRAM_SIZE) {
        store(NMD_CHECK_ZRAM, addr, value);
    } else {
        hardware_access(addr, 1, value, 1);
    }
}

uint8_t zin(uint8_t port)
{
    (void)port;
    fail(NMD_CHECK_UNSUPPORTED, port, 1, 0, 0);
    return 0;
}

void zout(uint8_t port, uint8_t value)
{
    fail(NMD_CHECK_UNSUPPORTED, port, 1, value, 1);
}

void nmd_z80_restart(uint16_t addr)
{
    fail(NMD_CHECK_UNSUPPORTED, addr, 0, 0, 0);
}

uint64_t nmd_z80_wait_until(int (*ready)(void *ctx), void *ctx)
{
    return nmd_wait_until(ready, ctx);
}
