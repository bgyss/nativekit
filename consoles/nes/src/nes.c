#include "nnes/machine.h"
#include "apu_internal.h"
#include "nes_internal.h"
#include "ppu_internal.h"
#include <limits.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

struct nnes {
    uint8_t ram[0x800];
    uint8_t open_bus;
    nnes_apu apu;
    nnes_ppu ppu;
    uint8_t buttons[2], latched[2], position[2], strobe;
    uint16_t rmw_address;
    uint8_t rmw_value, rmw_open, rmw_phase;
    uint8_t oam_dma_pending, oam_dma_page, oam_dma_needs_alignment;
    uint64_t bus_cycles, cpu_tenths;
    nnes_access_counts access_counts;
    nnes_cpu_state cpu;
    jmp_buf restart_target;
    uint16_t restart_address;
    unsigned native_depth, cartridge_callback_depth;
    unsigned bus_trace_callback_depth;
    uint8_t restart_active;
    uint8_t scheduled_nmi_active;
    nnes_cost_profile cost_profile;
    uint8_t has_cost_profile, cpu_costs_on;
    unsigned access_stats_suppressed;
    nnes_cartridge cartridge;
    nnes_routine *routines;
};

static nnes_result ppu_mapper_read(void *user, uint16_t address,
                                   uint8_t *value)
{
    nnes *n = user;
    if (!n || !n->cartridge.ppu_read) return NNES_UNSUPPORTED;
    ++n->cartridge_callback_depth;
    nnes_result result = n->cartridge.ppu_read(n->cartridge.user, address,
                                               value);
    --n->cartridge_callback_depth;
    return result;
}

static nnes_result ppu_mapper_write(void *user, uint16_t address,
                                    uint8_t value)
{
    nnes *n = user;
    if (!n || !n->cartridge.ppu_write) return NNES_UNSUPPORTED;
    ++n->cartridge_callback_depth;
    nnes_result result = n->cartridge.ppu_write(n->cartridge.user, address,
                                                value);
    --n->cartridge_callback_depth;
    return result;
}

static nnes_result ppu_mapper_mirroring(void *user,
                                       nnes_mirroring *mirroring)
{
    nnes *n = user;
    if (!n || !mirroring) return NNES_INVALID_ARGUMENT;
    if (!n->cartridge.ppu_mirroring) {
        *mirroring = NNES_MIRROR_HORIZONTAL;
        return NNES_OK;
    }
    ++n->cartridge_callback_depth;
    nnes_result result = n->cartridge.ppu_mirroring(n->cartridge.user,
                                                   mirroring);
    --n->cartridge_callback_depth;
    return result;
}

static nnes_ppu_mapper make_ppu_mapper(nnes *n)
{
    nnes_ppu_mapper mapper = {
        .user = n,
        .read = ppu_mapper_read,
        .write = ppu_mapper_write,
        .mirroring = ppu_mapper_mirroring
    };
    return mapper;
}

static nnes_result read_bus(nnes *n, uint16_t address, uint8_t *value,
                            int native_access, nnes_bus_operation operation,
                            uint8_t flags);
static nnes_result write_bus(nnes *n, uint16_t address, uint8_t value,
                             int native_access, nnes_bus_operation operation,
                             uint8_t flags);
static nnes_result perform_oam_dma(nnes *n, uint8_t page,
                                   int needs_alignment);

static void observe_bus_access(nnes *n, const nnes_bus_access *access)
{
    if (!n || !n->cartridge.bus_trace || !access) return;
    ++n->cartridge_callback_depth;
    ++n->bus_trace_callback_depth;
    n->cartridge.bus_trace(n->cartridge.bus_trace_user, access);
    --n->bus_trace_callback_depth;
    --n->cartridge_callback_depth;
}

static void observe_simple_access(nnes *n, uint16_t address, uint8_t value,
                                  nnes_bus_operation operation, uint8_t flags,
                                  uint8_t open_bus_before)
{
    nnes_bus_access access = {
        .address = address,
        .value = value,
        .operation = operation,
        .flags = flags,
        .open_bus_before = open_bus_before,
        .cpu_cycle_tenths = n->cpu_tenths
    };
    observe_bus_access(n, &access);
}

static int compare_routines(const void *left, const void *right)
{
    const nnes_routine *a = left;
    const nnes_routine *b = right;
    if (a->code_identity != b->code_identity)
        return (a->code_identity > b->code_identity) -
               (a->code_identity < b->code_identity);
    return (a->address > b->address) - (a->address < b->address);
}

static const nnes_routine *find_routine(const nnes *n,
                                        nnes_code_identity code_identity,
                                        uint16_t address)
{
    size_t lo = 0, hi = n->cartridge.routine_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const nnes_routine *routine = &n->routines[mid];
        if (routine->code_identity == code_identity &&
            routine->address == address)
            return routine;
        if (routine->code_identity < code_identity ||
            (routine->code_identity == code_identity &&
             routine->address < address))
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

static nnes_result resolve_routine(const nnes *n, uint16_t address,
                                   const nnes_routine **routine)
{
    if (!n || !routine) return NNES_INVALID_ARGUMENT;
    nnes_code_identity code_identity = 0;
    if (n->cartridge.code_identity) {
        nnes_result result = n->cartridge.code_identity(
            n->cartridge.user, address, &code_identity);
        if (result != NNES_OK) return result;
    }
    *routine = find_routine(n, code_identity, address);
    return *routine ? NNES_OK : NNES_MISSING_ROUTINE;
}

static nnes_result classify_access(const nnes *n, uint16_t address,
                                   nnes_access_kind *kind)
{
    if (address < 0x2000) *kind = NNES_KIND_WRAM;
    else if (address < 0x4020) *kind = NNES_KIND_IO;
    else if (n->cartridge.access_kind)
        *kind = n->cartridge.access_kind(n->cartridge.user, address);
    else *kind = NNES_KIND_ROM;
    if ((unsigned)*kind >= NNES_KINDS) return NNES_INVALID_ARGUMENT;
    return NNES_OK;
}

static nnes_result count_bus_cycle(nnes *n, uint16_t address, int write,
                                   int native_access, uint64_t *profile_tenths)
{
    if (!n || !profile_tenths) return NNES_INVALID_ARGUMENT;
    *profile_tenths = 0;
    if (n->bus_cycles == UINT64_MAX) return NNES_CYCLE_OVERFLOW;
    uint64_t spent = 10;
    nnes_access_kind kind = NNES_KIND_ROM;
    if (native_access) {
        nnes_result result = classify_access(n, address, &kind);
        if (result != NNES_OK) return result;
        if (!n->access_stats_suppressed &&
            n->access_counts.by_access[write != 0][kind] == UINT64_MAX)
            return NNES_CYCLE_OVERFLOW;
        if (n->cpu_costs_on && n->has_cost_profile) {
            *profile_tenths = n->cost_profile.cpu_tenths[write != 0][kind];
            spent += *profile_tenths;
        }
    }
    if (spent > UINT64_MAX - n->cpu_tenths) return NNES_CYCLE_OVERFLOW;
    nnes_result apu_result = nnes_apu_advance_tenths(&n->apu, 10);
    if (apu_result != NNES_OK) return apu_result;
    ++n->bus_cycles;
    n->cpu_tenths += spent;
    /* The bus device samples during its base CPU cycle. Native-C profile time
     * elapses after that operation, before the next bus access. */
    nnes_ppu_advance_tenths(&n->ppu, 10);
    if (native_access && !n->access_stats_suppressed)
        ++n->access_counts.by_access[write != 0][kind];
    return NNES_OK;
}

nnes_result nnes_create(const nnes_cartridge *cartridge, nnes **out)
{
    if (!out) return NNES_INVALID_ARGUMENT;
    *out = NULL;
    if (cartridge) {
        if (cartridge->routine_count > SIZE_MAX / sizeof(nnes_routine) ||
            (cartridge->routine_count && !cartridge->routines) ||
            (cartridge->cost_profile &&
             cartridge->cost_profile->version != NNES_COST_PROFILE_VERSION))
            return NNES_INVALID_ARGUMENT;
        for (size_t i = 0; i < cartridge->routine_count; ++i)
            if (!cartridge->routines[i].run) return NNES_INVALID_ARGUMENT;
    }
    nnes *n = calloc(1, sizeof *n);
    if (!n) return NNES_OUT_OF_MEMORY;
    n->cpu_costs_on = 1;
    nnes_ppu_init(&n->ppu);
    nnes_apu_init(&n->apu);
    n->cpu.s = 0;
    n->cpu.p = 0x24; /* deterministic native entry defaults: U and I */
    if (cartridge) {
        n->cartridge = *cartridge;
        if (cartridge->cost_profile) {
            n->cost_profile = *cartridge->cost_profile;
            n->has_cost_profile = 1;
            n->cartridge.cost_profile = &n->cost_profile;
        }
        if (cartridge->routine_count) {
            size_t bytes = cartridge->routine_count * sizeof(nnes_routine);
            n->routines = malloc(bytes);
            if (!n->routines) { free(n); return NNES_OUT_OF_MEMORY; }
            memcpy(n->routines, cartridge->routines, bytes);
            qsort(n->routines, cartridge->routine_count,
                  sizeof(nnes_routine), compare_routines);
            for (size_t i = 1; i < cartridge->routine_count; ++i) {
                if (n->routines[i - 1].code_identity ==
                        n->routines[i].code_identity &&
                    n->routines[i - 1].address == n->routines[i].address) {
                    free(n->routines);
                    free(n);
                    return NNES_INVALID_ARGUMENT;
                }
            }
        }
        n->cartridge.routines = n->routines;
    }
    *out = n;
    return NNES_OK;
}

void nnes_destroy(nnes *n)
{
    if (n) { free(n->routines); free(n); }
}

nnes_cpu_state *nnes_cpu(nnes *n)
{
    return n ? &n->cpu : NULL;
}

uint64_t nnes_bus_cycles(const nnes *n)
{
    return n ? n->bus_cycles : 0;
}

nnes_result nnes_get_access_counts(const nnes *n, nnes_access_counts *counts)
{
    if (!n || !counts) return NNES_INVALID_ARGUMENT;
    if (n->rmw_open) return NNES_INVALID_SEQUENCE;
    *counts = n->access_counts;
    return NNES_OK;
}

nnes_result nnes_reset_access_counts(nnes *n)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    if (n->rmw_open) return NNES_INVALID_SEQUENCE;
    memset(&n->access_counts, 0, sizeof n->access_counts);
    return NNES_OK;
}

uint64_t nnes_cpu_cycles(const nnes *n)
{
    return n ? n->cpu_tenths / 10 : 0;
}

uint64_t nnes_cpu_cycle_tenths(const nnes *n)
{
    return n ? n->cpu_tenths : 0;
}

nnes_result nnes_set_cpu_costs(nnes *n, int on)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    if (n->rmw_open) return NNES_INVALID_SEQUENCE;
    n->cpu_costs_on = on != 0;
    return NNES_OK;
}

nnes_result nnes_charge_cpu_tenths(nnes *n, uint64_t tenths)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    if (n->rmw_open) return NNES_INVALID_SEQUENCE;
    if (tenths > UINT64_MAX - n->cpu_tenths) return NNES_CYCLE_OVERFLOW;
    nnes_result apu_result = nnes_apu_advance_tenths(&n->apu, tenths);
    if (apu_result != NNES_OK) return apu_result;
    n->cpu_tenths += tenths;
    nnes_ppu_advance_tenths(&n->ppu, tenths);
    return NNES_OK;
}

nnes_result nnes_charge_cpu_cycles(nnes *n, uint64_t cycles)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    if (cycles > UINT64_MAX / 10) return NNES_CYCLE_OVERFLOW;
    return nnes_charge_cpu_tenths(n, cycles * 10);
}

nnes_result nnes_set_buttons(nnes *n, unsigned port, uint8_t buttons)
{
    if (!n || port > 1) return NNES_INVALID_ARGUMENT;
    n->buttons[port] = buttons;
    return NNES_OK;
}

nnes_result nnes_ppu_take_nmi(nnes *n, int *pending)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    return nnes_ppu_take_pending_nmi(&n->ppu, pending);
}

static nnes_result read_bus(nnes *n, uint16_t address, uint8_t *value,
                            int native_access, nnes_bus_operation operation,
                            uint8_t flags)
{
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    if (n->bus_trace_callback_depth) return NNES_INVALID_SEQUENCE;
    if (n->rmw_open) return NNES_INVALID_SEQUENCE;
    uint8_t open_bus_before = n->open_bus;
    uint64_t profile_tenths = 0;
    nnes_result cycle_result = count_bus_cycle(n, address, 0, native_access,
                                               &profile_tenths);
    if (cycle_result != NNES_OK) return cycle_result;
    nnes_result result = NNES_UNSUPPORTED;
    nnes_bus_access trace_event = {0};
    int has_trace_event = 0;
    if (address < 0x2000) {
        *value = n->ram[address & 0x7ff];
        result = NNES_OK;
    } else if (address >= 0x2000 && address < 0x4000) {
        nnes_ppu_mapper mapper = make_ppu_mapper(n);
        result = nnes_ppu_cpu_read(&n->ppu, &mapper, address, value);
    } else if (address >= 0x4000 && address <= 0x4014) {
        /* APU channel and OAM-DMA registers are write-only; reads float the
         * CPU bus and return its current open-bus value. */
        *value = open_bus_before;
        result = NNES_OK;
    } else if (address == 0x4015) {
        result = nnes_apu_read_status(&n->apu, open_bus_before, value);
    } else if (address == 0x4016 || address == 0x4017) {
        unsigned port = address & 1;
        uint8_t controller_bit;
        if (n->strobe) controller_bit = n->buttons[port] & 1;
        else if (n->position[port] < 8) {
            controller_bit = (n->latched[port] >> n->position[port]) & 1;
            ++n->position[port];
        } else controller_bit = 1;
        /* Standard pads drive the low controller-data lines; bits 7-5 float
         * and retain the preceding CPU open-bus value. */
        *value = (uint8_t)((open_bus_before & 0xe0) | controller_bit);
        result = NNES_OK;
    } else if (address >= 0x4020 && n->cartridge.bus_access) {
        trace_event = (nnes_bus_access){
            .address = address,
            .value = 0,
            .operation = operation,
            .flags = flags,
            .open_bus_before = n->open_bus,
            .cpu_cycle_tenths = n->cpu_tenths
        };
        has_trace_event = 1;
        ++n->cartridge_callback_depth;
        result = n->cartridge.bus_access(n->cartridge.user, &trace_event);
        --n->cartridge_callback_depth;
        if (result == NNES_OK) *value = trace_event.value;
    } else if (address >= 0x4020 && n->cartridge.read) {
        uint8_t temporary = 0;
        ++n->cartridge_callback_depth;
        result = n->cartridge.read(n->cartridge.user, address, &temporary);
        --n->cartridge_callback_depth;
        if (result == NNES_OK) *value = temporary;
    }

    if (result == NNES_OK && address != 0x4015) n->open_bus = *value;
    nnes_ppu_advance_tenths(&n->ppu, profile_tenths);
    nnes_result apu_profile_result =
        nnes_apu_advance_tenths(&n->apu, profile_tenths);
    if (apu_profile_result != NNES_OK) return apu_profile_result;
    if (result == NNES_OK) {
        if (has_trace_event) observe_bus_access(n, &trace_event);
        else observe_simple_access(n, address, *value, operation, flags,
                                   open_bus_before);
    }
    return result;
}

nnes_result nnes_read(nnes *n, uint16_t address, uint8_t *value)
{
    return read_bus(n, address, value, 1, NNES_BUS_READ, 0);
}

nnes_result nnes_rd8_dummy(nnes *n, uint16_t address)
{
    uint8_t ignored;
    if (!n) return NNES_INVALID_ARGUMENT;
    if (n->rmw_open) return NNES_INVALID_SEQUENCE;
    return read_bus(n, address, &ignored, 0, NNES_BUS_DUMMY_READ, 0);
}

nnes_result nnes_seed_open_bus(nnes *n, uint8_t value)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    if (n->rmw_open || n->cartridge_callback_depth ||
        n->bus_trace_callback_depth)
        return NNES_INVALID_SEQUENCE;
    n->open_bus = value;
    return NNES_OK;
}

static nnes_result write_bus(nnes *n, uint16_t address, uint8_t value,
                             int native_access, nnes_bus_operation operation,
                             uint8_t flags)
{
    uint8_t open_bus_before = n->open_bus;
    uint64_t profile_tenths = 0;
    nnes_result cycle_result = count_bus_cycle(n, address, 1, native_access,
                                               &profile_tenths);
    if (cycle_result != NNES_OK) return cycle_result;
    nnes_result result = NNES_UNSUPPORTED;
    nnes_bus_access trace_event = {0};
    int has_trace_event = 0;
    if (address < 0x2000) {
        n->ram[address & 0x7ff] = value;
        result = NNES_OK;
    } else if (address >= 0x2000 && address < 0x4000) {
        nnes_ppu_mapper mapper = make_ppu_mapper(n);
        result = nnes_ppu_cpu_write(&n->ppu, &mapper, address, value);
    } else if (address >= 0x4000 && address <= 0x4013) {
        result = nnes_apu_write_register(&n->apu, address, value);
    } else if (address == 0x4015) {
        result = nnes_apu_write_status(&n->apu, value);
    } else if (address == 0x4016) {
        uint8_t strobe = value & 1;
        if (n->strobe && !strobe) {
            for (unsigned i = 0; i < 2; ++i) {
                n->latched[i] = n->buttons[i];
                n->position[i] = 0;
            }
        }
        n->strobe = strobe;
        result = NNES_OK;
    } else if (address == 0x4017) {
        result = nnes_apu_write_frame_counter(&n->apu, value);
    } else if (address == 0x4014) {
        n->open_bus = value;
        observe_simple_access(n, address, value, operation, flags,
                             open_bus_before);
        has_trace_event = 1;
        result = NNES_OK;
        int needs_alignment =
            (((n->cpu_tenths - profile_tenths) / 10) & 1) == 0;
        if (operation == NNES_BUS_DUMMY_WRITE && n->rmw_open) {
            /* The RMW's second write determines the DMA page. */
        } else if (n->rmw_open) {
            n->oam_dma_page = value;
            n->oam_dma_needs_alignment = (uint8_t)needs_alignment;
            n->oam_dma_pending = 1;
        } else {
            result = perform_oam_dma(n, value, needs_alignment);
        }
    } else if (address >= 0x4020 && n->cartridge.bus_access) {
        trace_event = (nnes_bus_access){
            .address = address,
            .value = value,
            .operation = operation,
            .flags = flags,
            .open_bus_before = n->open_bus,
            .cpu_cycle_tenths = n->cpu_tenths
        };
        has_trace_event = 1;
        ++n->cartridge_callback_depth;
        result = n->cartridge.bus_access(n->cartridge.user, &trace_event);
        --n->cartridge_callback_depth;
    } else if (address >= 0x4020 && n->cartridge.write) {
        ++n->cartridge_callback_depth;
        result = n->cartridge.write(n->cartridge.user, address, value);
        --n->cartridge_callback_depth;
    }

    if (result == NNES_OK && address != 0x4014) n->open_bus = value;
    nnes_ppu_advance_tenths(&n->ppu, profile_tenths);
    nnes_result apu_profile_result =
        nnes_apu_advance_tenths(&n->apu, profile_tenths);
    if (apu_profile_result != NNES_OK) return apu_profile_result;
    if (result == NNES_OK && address != 0x4014) {
        if (has_trace_event) observe_bus_access(n, &trace_event);
        else observe_simple_access(n, address, value, operation, flags,
                                   open_bus_before);
    }
    return result;
}

static nnes_result perform_oam_dma(nnes *n, uint8_t page,
                                   int needs_alignment)
{
    if (!n || n->rmw_open) return NNES_INVALID_SEQUENCE;
    uint8_t discarded = 0;
    nnes_result result = read_bus(n, n->cpu.pc, &discarded, 0,
                                   NNES_BUS_DUMMY_READ, 0);
    if (result != NNES_OK) return result;
    if (needs_alignment) {
        result = read_bus(n, n->cpu.pc, &discarded, 0,
                          NNES_BUS_DUMMY_READ, 0);
        if (result != NNES_OK) return result;
    }

    for (unsigned offset = 0; offset < 256; ++offset) {
        uint8_t value = 0;
        uint16_t source = (uint16_t)(((uint16_t)page << 8) | offset);
        result = read_bus(n, source, &value, 0, NNES_BUS_DMA_READ, 0);
        if (result != NNES_OK) return result;
        result = write_bus(n, 0x2004, value, 0, NNES_BUS_DMA_WRITE, 0);
        if (result != NNES_OK) return result;
    }
    return NNES_OK;
}

static nnes_result stack_push_core(nnes *n, uint8_t value)
{
    nnes_result result = write_bus(n, (uint16_t)(0x0100 | n->cpu.s), value,
                                   0, NNES_BUS_WRITE, 0);
    if (result == NNES_OK) --n->cpu.s;
    return result;
}

static nnes_result stack_pull_core(nnes *n, uint8_t *value,
                                   int native_access)
{
    uint8_t next_s = (uint8_t)(n->cpu.s + 1);
    nnes_result result = read_bus(n, (uint16_t)(0x0100 | next_s), value,
                                  native_access, NNES_BUS_READ, 0);
    if (result == NNES_OK) n->cpu.s = next_s;
    return result;
}

static nnes_result rti_core(nnes *n, int native_access)
{
    uint8_t discarded, status, lo, hi;
    nnes_result result = read_bus(n, (uint16_t)(n->cpu.pc + 1), &discarded, 0,
                                  NNES_BUS_DUMMY_READ, 0);
    if (result != NNES_OK) return result;
    result = read_bus(n, (uint16_t)(0x0100 | n->cpu.s), &discarded, 0,
                      NNES_BUS_DUMMY_READ, 0);
    if (result != NNES_OK) return result;
    result = stack_pull_core(n, &status, native_access);
    if (result != NNES_OK) return result;
    result = stack_pull_core(n, &lo, native_access);
    if (result != NNES_OK) return result;
    result = stack_pull_core(n, &hi, native_access);
    if (result != NNES_OK) return result;
    n->cpu.p = (uint8_t)((status & (uint8_t)~NNES_P_B) | NNES_P_U);
    n->cpu.pc = (uint16_t)(lo | ((uint16_t)hi << 8));
    return NNES_OK;
}

nnes_result nnes_rti(nnes *n)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    return rti_core(n, 1);
}

nnes_result nnes_write(nnes *n, uint16_t address, uint8_t value)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    if (n->bus_trace_callback_depth) return NNES_INVALID_SEQUENCE;
    if (n->rmw_open) {
        if (n->rmw_phase || address != n->rmw_address)
            return NNES_INVALID_SEQUENCE;
        uint8_t original = n->rmw_value;
        n->rmw_phase = 1;
        nnes_result result = write_bus(n, address, original, 0,
                                       NNES_BUS_DUMMY_WRITE,
                                       NNES_BUS_ACCESS_RMW_PAIR);
        if (result != NNES_OK) {
            n->rmw_open = n->rmw_phase = 0;
            return result;
        }
        n->rmw_phase = 2;
        result = write_bus(n, address, value, 1, NNES_BUS_WRITE,
                           NNES_BUS_ACCESS_RMW_PAIR);
        n->rmw_open = n->rmw_phase = 0;
        if (result == NNES_OK && n->oam_dma_pending) {
            uint8_t page = n->oam_dma_page;
            uint8_t needs_alignment = n->oam_dma_needs_alignment;
            n->oam_dma_pending = 0;
            n->oam_dma_needs_alignment = 0;
            result = perform_oam_dma(n, page, needs_alignment);
        } else if (result != NNES_OK) {
            n->oam_dma_pending = 0;
        }
        return result;
    }
    return write_bus(n, address, value, 1, NNES_BUS_WRITE, 0);
}

nnes_result nnes_wr8_dummy(nnes *n, uint16_t address, uint8_t value)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    if (n->rmw_open) return NNES_INVALID_SEQUENCE;
    return write_bus(n, address, value, 0, NNES_BUS_DUMMY_WRITE, 0);
}

nnes_result nnes_rd8_rmw(nnes *n, uint16_t address, uint8_t *value)
{
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    if (n->rmw_open) return NNES_INVALID_SEQUENCE;
    nnes_result result = read_bus(n, address, value, 1, NNES_BUS_READ,
                                  NNES_BUS_ACCESS_RMW_PAIR);
    if (result == NNES_OK) {
        n->rmw_address = address;
        n->rmw_value = *value;
        n->rmw_open = 1;
        n->rmw_phase = 0;
    }
    return result;
}

nnes_result nnes_wr8_rmw(nnes *n, uint16_t address, uint8_t value)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    if (!n->rmw_open || address != n->rmw_address)
        return NNES_INVALID_SEQUENCE;
    return nnes_write(n, address, value);
}

nnes_result nnes_call(nnes *n, uint16_t address)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    if (n->bus_trace_callback_depth) return NNES_INVALID_SEQUENCE;
    if (n->rmw_open) return NNES_INVALID_SEQUENCE;
    const nnes_routine *routine;
    nnes_result resolve = resolve_routine(n, address, &routine);
    if (resolve != NNES_OK) return resolve;
    ++n->native_depth;
    nnes_result result = routine->run(n, n->cartridge.user);
    --n->native_depth;
    return result;
}

nnes_result nnes_jsr_call(nnes *n, uint16_t address,
                          uint16_t return_address, uint16_t rts_address)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    if (n->rmw_open || n->cartridge_callback_depth ||
        n->bus_trace_callback_depth)
        return NNES_INVALID_SEQUENCE;
    const nnes_routine *routine;
    nnes_result result = resolve_routine(n, address, &routine);
    if (result != NNES_OK) return result;
    (void)routine;

    result = nnes_rd8_dummy(n, (uint16_t)(0x0100 | n->cpu.s));
    if (result != NNES_OK) return result;
    result = nnes_push16(n, return_address);
    if (result != NNES_OK) return result;
    n->cpu.pc = address;
    result = nnes_call(n, address);
    if (result != NNES_OK) return result;
    n->cpu.pc = rts_address;
    return nnes_rts(n);
}

nnes_result nnes_jump_routine(nnes *n, uint16_t address)
{
    return nnes_call(n, address);
}

nnes_result nnes_jump_absolute(nnes *n, uint16_t target)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    if (n->rmw_open) return NNES_INVALID_SEQUENCE;
    n->cpu.pc = target;
    return nnes_jump_routine(n, target);
}

nnes_result nnes_jump_routine_pushed(nnes *n, uint16_t address,
                                     const uint8_t *pushed, size_t count)
{
    if (!n || (count && !pushed)) return NNES_INVALID_ARGUMENT;
    if (n->rmw_open) return NNES_INVALID_SEQUENCE;
    const nnes_routine *routine;
    nnes_result resolve = resolve_routine(n, address, &routine);
    if (resolve != NNES_OK) return resolve;
    (void)routine;
    for (size_t i = 0; i < count; ++i) {
        nnes_result result = nnes_push8(n, pushed[i]);
        if (result != NNES_OK) return result;
    }
    return nnes_call(n, address);
}

nnes_result nnes_branch_relative(nnes *n, nnes_branch_condition branch,
                                uint16_t next_pc, uint8_t displacement,
                                int *taken)
{
    if (!n || !taken || branch < NNES_BRANCH_BCC || branch > NNES_BRANCH_BVS)
        return NNES_INVALID_ARGUMENT;
    if (n->cartridge_callback_depth || n->rmw_open)
        return NNES_INVALID_SEQUENCE;

    int branch_taken = nnes_branch_taken(n->cpu.p, branch);
    n->cpu.pc = next_pc;
    if (!branch_taken) {
        *taken = 0;
        return NNES_OK;
    }

    uint16_t target = nnes_branch_target(next_pc, displacement);
    nnes_result result = nnes_rd8_dummy(n, next_pc);
    if (result != NNES_OK) return result;
    if ((next_pc & 0xff00) != (target & 0xff00)) {
        uint16_t partial = nnes_page_fixup_dummy_address(next_pc, target);
        n->cpu.pc = partial;
        result = nnes_rd8_dummy(n, partial);
        if (result != NNES_OK) return result;
    }
    n->cpu.pc = target;
    *taken = 1;
    return NNES_OK;
}

nnes_result nnes_cpu_restart(nnes *n, uint16_t address)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    if (!n->restart_active || !n->native_depth || n->cartridge_callback_depth ||
        n->rmw_open)
        return NNES_INVALID_SEQUENCE;
    const nnes_routine *routine;
    nnes_result resolve = resolve_routine(n, address, &routine);
    if (resolve != NNES_OK) return resolve;
    (void)routine;
    n->cpu.s = 0xff;
    n->cpu.pc = address;
    n->restart_address = address;
    n->native_depth = 0;
    n->access_stats_suppressed = 0;
    longjmp(n->restart_target, 1);
}

nnes_result nnes_raise_interrupt(nnes *n, nnes_interrupt_kind kind)
{
    if (!n || (kind != NNES_INT_NMI && kind != NNES_INT_IRQ &&
               kind != NNES_INT_BRK))
        return NNES_INVALID_ARGUMENT;
    if (n->cartridge_callback_depth) return NNES_INVALID_SEQUENCE;
    if (n->rmw_open) return NNES_INVALID_SEQUENCE;
    if (kind == NNES_INT_IRQ && (n->cpu.p & NNES_P_I))
        return NNES_INTERRUPT_MASKED;

    uint8_t stack_before = n->cpu.s;
    int brk = kind == NNES_INT_BRK;
    uint16_t opcode_pc = n->cpu.pc;
    uint8_t discarded;
    nnes_bus_operation opcode_operation = brk ? NNES_BUS_OPCODE_FETCH : NNES_BUS_DUMMY_READ;
    nnes_bus_operation operand_operation = brk ? NNES_BUS_OPERAND_FETCH : NNES_BUS_DUMMY_READ;
    nnes_result result = read_bus(n, opcode_pc, &discarded, 0,
                                  opcode_operation, 0);
    if (result != NNES_OK) return result;
    result = read_bus(n, (uint16_t)(opcode_pc + (brk ? 1 : 0)), &discarded,
                      0, operand_operation, 0);
    if (result != NNES_OK) return result;

    uint16_t vector = kind == NNES_INT_NMI ? 0xfffa : 0xfffe;
    uint16_t return_pc = brk ? (uint16_t)(opcode_pc + 2) : opcode_pc;
    result = stack_push_core(n, (uint8_t)(return_pc >> 8));
    if (result != NNES_OK) return result;
    result = stack_push_core(n, (uint8_t)return_pc);
    if (result != NNES_OK) return result;
    uint8_t stacked_p = (uint8_t)((n->cpu.p & (uint8_t)~NNES_P_B) | NNES_P_U);
    if (brk) stacked_p |= NNES_P_B;
    result = stack_push_core(n, stacked_p);
    if (result != NNES_OK) return result;
    n->cpu.p = nnes_p_set_flags(n->cpu.p, NNES_P_I);

    uint8_t low, high;
    result = read_bus(n, vector, &low, 0, NNES_BUS_READ, 0);
    if (result != NNES_OK) return result;
    result = read_bus(n, (uint16_t)(vector + 1), &high, 0, NNES_BUS_READ, 0);
    if (result != NNES_OK) return result;
    uint16_t handler = (uint16_t)(low | ((uint16_t)high << 8));
    n->cpu.pc = handler;
    unsigned saved_suppression = n->access_stats_suppressed;
    n->access_stats_suppressed = 0;
    result = nnes_call(n, handler);
    n->access_stats_suppressed = saved_suppression;
    if (result != NNES_OK) return result;

    /* Native handlers may model RTI or a title-specific RTS that consumes the
     * complete interrupt frame. Do not return from the same frame twice. */
    if (n->cpu.s == stack_before) return NNES_OK;
    return rti_core(n, 0);
}

nnes_result nnes_start(nnes *n)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    if (n->restart_active) return NNES_INVALID_SEQUENCE;
    uint8_t discarded, low, high;
    n->cpu.p = (uint8_t)((n->cpu.p & (uint8_t)~NNES_P_B) | NNES_P_U | NNES_P_I);

    nnes_result result = read_bus(n, n->cpu.pc, &discarded, 0,
                                  NNES_BUS_DUMMY_READ, 0);
    if (result != NNES_OK) return result;
    result = read_bus(n, n->cpu.pc, &discarded, 0, NNES_BUS_DUMMY_READ, 0);
    if (result != NNES_OK) return result;
    for (unsigned i = 0; i < 3; ++i) {
        result = read_bus(n, (uint16_t)(0x0100 | n->cpu.s), &discarded, 0,
                          NNES_BUS_DUMMY_READ, 0);
        if (result != NNES_OK) return result;
        --n->cpu.s;
    }
    result = read_bus(n, 0xfffc, &low, 0, NNES_BUS_READ, 0);
    if (result != NNES_OK) return result;
    result = read_bus(n, 0xfffd, &high, 0, NNES_BUS_READ, 0);
    if (result != NNES_OK) return result;
    n->cpu.pc = (uint16_t)(low | ((uint16_t)high << 8));

    n->restart_active = 1;
    int restarted = setjmp(n->restart_target);
    uint16_t entry = restarted ? n->restart_address : n->cpu.pc;
    nnes_result entry_result = nnes_call(n, entry);
    n->restart_active = 0;
    return entry_result;
}

static nnes_result deliver_wait_boundary_nmi(nnes *n, int *delivered)
{
    *delivered = 0;
    if (n->scheduled_nmi_active) return NNES_OK;
    int pending = 0;
    nnes_result result = nnes_ppu_take_pending_nmi(&n->ppu, &pending);
    if (result != NNES_OK || !pending) return result;
    n->scheduled_nmi_active = 1;
    result = nnes_raise_interrupt(n, NNES_INT_NMI);
    n->scheduled_nmi_active = 0;
    if (result == NNES_OK) *delivered = 1;
    return result;
}

nnes_result nnes_interrupt_checkpoint(nnes *n, int *delivered)
{
    if (!n || !delivered) return NNES_INVALID_ARGUMENT;
    *delivered = 0;
    if (n->rmw_open || n->cartridge_callback_depth ||
        n->bus_trace_callback_depth)
        return NNES_INVALID_SEQUENCE;

    /* A checkpoint may run inside a suppressed wait-poll callback. Interrupt
     * handler accesses belong to the interrupted code path, so expose them to
     * access profiles while the handler runs, then restore the poll filter. */
    unsigned saved_suppression = n->access_stats_suppressed;
    if (saved_suppression) --n->access_stats_suppressed;
    nnes_result result = deliver_wait_boundary_nmi(n, delivered);
    n->access_stats_suppressed = saved_suppression;
    return result;
}

nnes_result nnes_wait_until(nnes *n, nnes_ready_fn ready, void *ctx,
                            uint64_t poll_tenths, uint64_t *waited_tenths)
{
    if (!n || !ready || !waited_tenths) return NNES_INVALID_ARGUMENT;
    *waited_tenths = 0;
    nnes_result result = nnes_charge_cpu_tenths(n, 0);
    if (result != NNES_OK) return result;
    uint64_t start = nnes_cpu_cycle_tenths(n);

    for (;;) {
        int ready_now = 0;
        uint64_t before = nnes_cpu_cycle_tenths(n);
        if (n->access_stats_suppressed == UINT_MAX) return NNES_CYCLE_OVERFLOW;
        ++n->access_stats_suppressed;
        result = ready(n, ctx, &ready_now);
        --n->access_stats_suppressed;
        if (n->rmw_open) {
            *waited_tenths = nnes_cpu_cycle_tenths(n) - start;
            return NNES_INVALID_SEQUENCE;
        }
        if (result != NNES_OK) {
            *waited_tenths = nnes_cpu_cycle_tenths(n) - start;
            return result;
        }
        int delivered_nmi = 0;
        result = nnes_interrupt_checkpoint(n, &delivered_nmi);
        if (result != NNES_OK) {
            *waited_tenths = nnes_cpu_cycle_tenths(n) - start;
            return result;
        }
        if (delivered_nmi) continue;
        if (ready_now) {
            *waited_tenths = nnes_cpu_cycle_tenths(n) - start;
            return NNES_OK;
        }
        if (poll_tenths) {
            result = nnes_charge_cpu_tenths(n, poll_tenths);
            if (result != NNES_OK) {
                *waited_tenths = nnes_cpu_cycle_tenths(n) - start;
                return result;
            }
        } else if (nnes_cpu_cycle_tenths(n) == before) {
            *waited_tenths = nnes_cpu_cycle_tenths(n) - start;
            return NNES_INVALID_SEQUENCE;
        }
    }
}
