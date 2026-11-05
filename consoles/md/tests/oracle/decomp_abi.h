/* Versioned, candidate-only reference execution hooks. */
#ifndef NMD_DECOMP_ABI_H
#define NMD_DECOMP_ABI_H

#include <stdint.h>

#define NMD_DECOMP_ABI_VERSION 1u
#define NMD_DECOMP_PAYLOAD_BYTES 256u
#define NMD_DECOMP_MAX_FETCH_BYTES 16u

#define NMD_DECOMP_CAP_M68K_INSTRUCTION (UINT64_C(1) << 0)
#define NMD_DECOMP_CAP_M68K_STATE (UINT64_C(1) << 1)
#define NMD_DECOMP_CAP_Z80_INSTRUCTION (UINT64_C(1) << 2)
#define NMD_DECOMP_CAP_Z80_STATE (UINT64_C(1) << 3)
#define NMD_DECOMP_CAP_FLOW_EDGE (UINT64_C(1) << 4)
#define NMD_DECOMP_CAP_RESET_EXCEPTION_INTERRUPT (UINT64_C(1) << 5)
#define NMD_DECOMP_CAP_FETCH_PROVENANCE (UINT64_C(1) << 6)
#define NMD_DECOMP_CAP_BUS_INTERVALS (UINT64_C(1) << 7)
#define NMD_DECOMP_CAP_MASTER_CLOCK (UINT64_C(1) << 8)
#define NMD_DECOMP_CAP_SYNC_CHECKPOINT (UINT64_C(1) << 9)
#define NMD_DECOMP_CAP_CONTROLLED_STOP (UINT64_C(1) << 10)
#define NMD_DECOMP_CAP_REPLACE_HANDOFF (UINT64_C(1) << 11)

enum nmd_decomp_action {
    NMD_DECOMP_CONTINUE = 0,
    NMD_DECOMP_STOP = 1,
    NMD_DECOMP_REPLACE = 2
};

enum nmd_decomp_cpu {
    NMD_DECOMP_M68K = 1,
    NMD_DECOMP_Z80 = 2
};

enum nmd_decomp_event_kind {
    NMD_DECOMP_INSTRUCTION = 1,
    NMD_DECOMP_FLOW_EDGE = 2,
    NMD_DECOMP_BUS_ACCESS = 3,
    NMD_DECOMP_RESET = 4,
    NMD_DECOMP_EXCEPTION = 5,
    NMD_DECOMP_INTERRUPT = 6,
    NMD_DECOMP_STOP_EVENT = 7,
    NMD_DECOMP_REPLACE_EVENT = 8
};

enum nmd_decomp_stop_criterion {
    NMD_DECOMP_STOP_PC = 1,
    NMD_DECOMP_STOP_RETURN_SENTINEL = 2,
    NMD_DECOMP_STOP_INTERRUPT = 3,
    NMD_DECOMP_STOP_INSTRUCTION_BUDGET = 4
};

enum nmd_decomp_provenance {
    NMD_DECOMP_PROVENANCE_FETCH = 1,
    NMD_DECOMP_PROVENANCE_OPERAND = 2,
    NMD_DECOMP_PROVENANCE_DATA = 3
};

enum nmd_decomp_access {
    NMD_DECOMP_ACCESS_READ = 1,
    NMD_DECOMP_ACCESS_WRITE = 2
};

struct nmd_decomp_m68k_state {
    uint32_t d[8];
    uint32_t a[8];
    uint32_t other_sp;
    uint32_t cycles;
    uint32_t target_cycle;
    uint32_t sync_cycle;
    uint32_t int_cycle;
    uint16_t prefetch;
    uint16_t reserved;
    uint8_t status;
    uint8_t ccr;
    uint8_t xflag;
    uint8_t nflag;
    uint8_t zflag;
    uint8_t vflag;
    uint8_t cflag;
    uint8_t trace_pending;
    uint8_t stopped;
    uint8_t int_priority;
    uint8_t int_pending_num;
    uint8_t int_pending;
    uint8_t int_num;
    uint8_t int_ack;
    uint8_t reserved_flags[2];
};

struct nmd_decomp_z80_state {
    uint8_t main[8];
    uint8_t alt[8];
    uint32_t cycles;
    uint32_t sync_cycle;
    uint32_t nmi_cycle;
    uint32_t int_end_cycle;
    uint32_t int_cycle;
    uint16_t wz;
    uint16_t sp;
    uint16_t pc;
    uint16_t iy;
    uint16_t ix;
    uint8_t i;
    uint8_t r;
    uint8_t rhigh;
    uint8_t zflag;
    uint8_t reset;
    uint8_t pvflag;
    uint8_t nflag;
    uint8_t last_flag_result;
    uint8_t int_value;
    uint8_t imode;
    uint8_t iff2;
    uint8_t iff1;
    uint8_t chflags;
    uint8_t busreq;
    uint8_t busack;
};

struct nmd_decomp_flow_edge {
    uint32_t previous_pc;
    uint32_t current_pc;
    uint32_t transfer_kind;
    uint32_t target_known;
};

struct nmd_decomp_bus_access {
    uint64_t start_clock;
    uint64_t completion_clock;
    uint32_t address;
    uint32_t value;
    uint8_t width;
    uint8_t operation;
    uint8_t provenance;
    uint8_t reserved;
};

struct nmd_decomp_interrupt {
    uint32_t vector;
    uint32_t reason;
    uint32_t phase;
    uint32_t stack_pointer;
};

struct nmd_decomp_stop {
    uint32_t criterion;
    uint32_t reason;
    uint64_t value;
    uint64_t instruction_count;
    uint64_t stop_clock;
};

struct nmd_decomp_handoff {
    struct nmd_decomp_stop stop;
    uint64_t master_clock;
    uint64_t z80_master_clock;
    uint64_t vdp_clock;
    uint64_t ym_clock;
    uint64_t psg_clock;
    uint64_t io_clock;
    struct nmd_decomp_m68k_state m68k;
    struct nmd_decomp_z80_state z80;
};

_Static_assert(sizeof(struct nmd_decomp_handoff) <= NMD_DECOMP_PAYLOAD_BYTES,
               "NMDH handoff must fit in the copied event payload");

union nmd_decomp_cpu_state {
    struct nmd_decomp_m68k_state m68k;
    struct nmd_decomp_z80_state z80;
};

struct nmd_decomp_instruction {
    uint8_t fetched_length;
    uint8_t fetched_bytes[NMD_DECOMP_MAX_FETCH_BYTES];
    uint16_t opcode;
    uint16_t reserved;
    union nmd_decomp_cpu_state state;
};

union nmd_decomp_payload {
    struct nmd_decomp_instruction instruction;
    struct nmd_decomp_m68k_state m68k;
    struct nmd_decomp_z80_state z80;
    struct nmd_decomp_flow_edge flow;
    struct nmd_decomp_bus_access bus;
    struct nmd_decomp_interrupt interrupt;
    struct nmd_decomp_stop stop;
    struct nmd_decomp_handoff handoff;
    uint8_t bytes[NMD_DECOMP_PAYLOAD_BYTES];
};

struct nmd_decomp_event {
    uint32_t struct_size;
    uint32_t kind;
    uint32_t cpu;
    uint32_t flags;
    uint64_t sequence;
    uint64_t master_clock;
    uint64_t epoch;
    uint32_t pc;
    uint32_t next_pc;
    union nmd_decomp_payload payload;
};

typedef enum nmd_decomp_action (*nmd_decomp_callback)(
    void *user, const struct nmd_decomp_event *event);

uint32_t nmd_decomp_abi_version(void);
uint64_t nmd_decomp_capabilities(void);
int nmd_decomp_register(uint32_t version, nmd_decomp_callback callback, void *user);
void nmd_decomp_reset(void);
int nmd_decomp_error(void);
enum nmd_decomp_action nmd_decomp_publish(struct nmd_decomp_event *event);
int nmd_decomp_stop_pending(void);
uint32_t nmd_decomp_stop_action(void);
uint32_t nmd_decomp_stop_cpu(void);
uint32_t nmd_decomp_stop_pc(void);
uint32_t nmd_decomp_stop_next_pc(void);
uint64_t nmd_decomp_stop_clock(void);
int nmd_decomp_should_stop_cpu(uint32_t cpu, uint64_t master_clock);
int nmd_decomp_handed_off(void);
void nmd_decomp_set_stop_metadata(uint32_t criterion, uint64_t value,
                                  uint64_t instruction_count);
void nmd_decomp_fill_stop(struct nmd_decomp_stop *stop);
void nmd_decomp_mark_handed_off(void);
uint64_t nmd_decomp_clock(uint32_t local_clock);
uint64_t nmd_decomp_clock_epoch(void);
void nmd_decomp_adjust_clock(uint32_t deduction);
void nmd_decomp_request_test_rebase(void);
int nmd_decomp_take_test_rebase(void);

#endif
