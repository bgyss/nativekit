#ifndef NNES_NES_H
#define NNES_NES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nnes nnes;
/* Native 2A03 game state. P carries N/V/D/I/Z/C; B is represented only in
 * PHP's pushed byte. The deterministic initial values are a native contract,
 * not a claim about the power-on state of every console. */
typedef struct {
    uint8_t a, x, y, s, p;
    uint16_t pc;
} nnes_cpu_state;
typedef enum {
    NNES_OK = 0,
    NNES_INVALID_ARGUMENT,
    NNES_INVALID_SEQUENCE,
    NNES_UNSUPPORTED,
    NNES_MISSING_ROUTINE,
    NNES_OUT_OF_MEMORY,
    NNES_INTERRUPT_MASKED,
    NNES_CYCLE_OVERFLOW
} nnes_result;

typedef enum {
    NNES_KIND_WRAM = 0,
    NNES_KIND_ROM,
    NNES_KIND_CART_RAM,
    NNES_KIND_IO,
    NNES_KINDS
} nnes_access_kind;

typedef enum {
    NNES_MIRROR_SINGLE_LOWER,
    NNES_MIRROR_SINGLE_UPPER,
    NNES_MIRROR_VERTICAL,
    NNES_MIRROR_HORIZONTAL,
    NNES_MIRROR_FOUR_SCREEN
} nnes_mirroring;

#define NNES_COST_PROFILE_VERSION 1
typedef struct {
    int version;
    /* Native-C work charged around each access, in tenths of a CPU cycle. */
    uint32_t cpu_tenths[2][NNES_KINDS]; /* [read/write][kind] */
} nnes_cost_profile;

enum {
    NNES_A = 1u << 0, NNES_B = 1u << 1,
    NNES_SELECT = 1u << 2, NNES_START = 1u << 3,
    NNES_UP = 1u << 4, NNES_DOWN = 1u << 5,
    NNES_LEFT = 1u << 6, NNES_RIGHT = 1u << 7
};

typedef nnes_result (*nnes_read_fn)(void *user, uint16_t address, uint8_t *value);
typedef nnes_result (*nnes_write_fn)(void *user, uint16_t address, uint8_t value);
typedef enum {
    NNES_BUS_READ,
    NNES_BUS_WRITE,
    NNES_BUS_DUMMY_READ,
    NNES_BUS_DUMMY_WRITE,
    NNES_BUS_OPCODE_FETCH,
    NNES_BUS_OPERAND_FETCH,
    NNES_BUS_DMA_READ,
    NNES_BUS_DMA_WRITE
} nnes_bus_operation;
/* Marks both writes in the old-value/new-value RMW pair. */
enum {
    NNES_BUS_ACCESS_RMW_PAIR = 1u << 0
};
typedef struct {
    uint16_t address;
    uint8_t value;
    nnes_bus_operation operation;
    uint8_t flags;
    /* CPU data-bus latch value immediately before this access. A device may
     * return it on an explicitly open-bus read. */
    uint8_t open_bus_before;
    /* Cumulative CPU time in tenths of a cycle after this access cost. */
    uint64_t cpu_cycle_tenths;
} nnes_bus_access;
/* Full cartridge bus adapter. For reads, fill access->value; for writes,
 * access->value is input. The operation and timestamp describe this bus event;
 * the RMW flag identifies both writes in one read-modify-write pair, and
 * open_bus_before is the CPU data-bus latch before the access. This supersedes
 * legacy read/write callbacks when present. */
typedef nnes_result (*nnes_bus_access_fn)(void *user,
                                          nnes_bus_access *access);
/* Optional read-only observer for every successful CPU bus access, including
 * internal RAM, PPU registers, cartridge operations, and DMA reads/writes. It
 * must not reenter or mutate the console. */
typedef void (*nnes_bus_trace_fn)(void *user,
                                 const nnes_bus_access *access);
/* PPU pattern-table bus adapters. Addresses are in $0000-$1FFF. A mapper may
 * update mirroring as its control register changes. */
typedef nnes_result (*nnes_ppu_read_fn)(void *user, uint16_t address,
                                        uint8_t *value);
typedef nnes_result (*nnes_ppu_write_fn)(void *user, uint16_t address,
                                         uint8_t value);
typedef nnes_result (*nnes_ppu_mirroring_fn)(void *user,
                                              nnes_mirroring *mirroring);
/* Pure address classification for cartridge addresses $4020-$FFFF. Return one
 * of NNES_KIND_*; when absent, cartridge accesses count as PRG ROM. */
typedef nnes_access_kind (*nnes_access_kind_fn)(void *user, uint16_t address);
typedef nnes_result (*nnes_native_fn)(nnes *console, void *user);
/* Code identity names the physical bytes currently mapped at a CPU address.
 * Use distinct IDs when one CPU address can refer to different PRG banks or
 * RAM-loaded code generations. Zero is the unbanked identity. */
typedef uint32_t nnes_code_identity;
typedef nnes_result (*nnes_code_identity_fn)(void *user, uint16_t address,
                                             nnes_code_identity *identity);
typedef struct {
    uint16_t address;
    nnes_native_fn run;
    /* Physical PRG bank, RAM segment/generation, or zero for unbanked code. */
    nnes_code_identity code_identity;
} nnes_routine;
typedef struct {
    void *user;
    nnes_read_fn read;
    nnes_write_fn write;
    const nnes_routine *routines;
    size_t routine_count;
    const nnes_cost_profile *cost_profile;
    nnes_access_kind_fn access_kind;
    /* Pure CPU-address to currently mapped code-identity lookup. When absent,
     * all routines use identity zero. It must not issue bus accesses or change
     * mapper state. */
    nnes_code_identity_fn code_identity;
    /* Optional operation-aware adapter for mapper register timing and other
     * cartridge bus effects; takes precedence over legacy read/write callbacks. */
    nnes_bus_access_fn bus_access;
    nnes_ppu_read_fn ppu_read;
    nnes_ppu_write_fn ppu_write;
    nnes_ppu_mirroring_fn ppu_mirroring;
    /* Optional observer with separately borrowed user data. It cannot change
     * access results; copy the event during the callback if it must be kept. */
    void *bus_trace_user;
    nnes_bus_trace_fn bus_trace;
} nnes_cartridge;

/* Copies descriptors, routines (ordered by code identity then address), and the
 * optional version-1 cost profile. user remains borrowed until destruction. A
 * NULL cartridge creates a console-only instance. On failure, out is set NULL. */
nnes_result nnes_create(const nnes_cartridge *cartridge, nnes **out);
void nnes_destroy(nnes *console);
/* The NES CPU register file used by native routine wrappers. The returned
 * pointer belongs to this console and remains valid until destruction. */
nnes_cpu_state *nnes_cpu(nnes *console);
/* RAM is zero-initialized for deterministic native startup. A bus access costs
 * one CPU cycle, adds its enabled profile weight, advances the NTSC PPU by the
 * corresponding three dots, and advances the APU frame-status clock. APU
 * channels, DMC, sample output, and asynchronous interrupt delivery remain
 * incomplete. Failed reads leave *value unchanged. */
nnes_result nnes_read(nnes *console, uint16_t address, uint8_t *value);
nnes_result nnes_write(nnes *console, uint16_t address, uint8_t value);
/* Consume a pending PPU NMI edge manually. nnes_wait_until also dispatches a
 * pending PPU NMI after its readiness callback completes. */
nnes_result nnes_ppu_take_nmi(nnes *console, int *pending);
/* Port is 0 or 1. Standard joypads only; read results expose bit 0 only. */
nnes_result nnes_set_buttons(nnes *console, unsigned port, uint8_t buttons);
/* Invokes the registered routine whose (mapped code identity, CPU address)
 * matches the cartridge's current mapping; never executes machine code. */
nnes_result nnes_call(nnes *console, uint16_t address);
/* Model the seven reset bus cycles: dummy reads, three stack-pointer
 * decrements, and the $FFFC/$FFFD vector fetch, then enter native reset C.
 * These bus accesses advance PPU and APU time. PPU NMI delivery is limited to
 * wait-poll boundaries; general asynchronous interrupt scheduling remains
 * absent. */
nnes_result nnes_start(nnes *console);

#ifdef __cplusplus
}
#endif
#endif
