#include "nnes/machine.h"
#include "nes_internal.h"

nnes_result nnes_rd8(nnes *n, uint16_t address, uint8_t *value)
{
    return nnes_read(n, address, value);
}

nnes_result nnes_rd8_absolute(nnes *n, uint16_t address, uint8_t *value)
{
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_seed_open_bus(n, (uint8_t)(address >> 8));
    if (result != NNES_OK) return result;
    return nnes_rd8(n, address, value);
}

nnes_result nnes_rd8_zp(nnes *n, uint8_t address, uint8_t *value)
{
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_seed_open_bus(n, address);
    if (result != NNES_OK) return result;
    return nnes_rd8(n, address, value);
}

nnes_result nnes_rd8_discard(nnes *n, uint16_t address)
{
    uint8_t ignored;
    return nnes_rd8(n, address, &ignored);
}

nnes_result nnes_wr8(nnes *n, uint16_t address, uint8_t value)
{
    return nnes_write(n, address, value);
}

nnes_result nnes_rd8_zp_indexed(nnes *n, uint8_t base, uint8_t index,
                                uint8_t *value)
{
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_seed_open_bus(n, base);
    if (result != NNES_OK) return result;
    result = nnes_rd8_dummy(n, base);
    if (result != NNES_OK) return result;
    return nnes_rd8(n, nnes_zp_index(base, index), value);
}

nnes_result nnes_wr8_zp_indexed(nnes *n, uint8_t base, uint8_t index,
                                uint8_t value)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_seed_open_bus(n, base);
    if (result != NNES_OK) return result;
    result = nnes_rd8_dummy(n, base);
    if (result != NNES_OK) return result;
    return nnes_wr8(n, nnes_zp_index(base, index), value);
}

nnes_result nnes_rd8_zp_rmw(nnes *n, uint8_t address, uint8_t *value)
{
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_seed_open_bus(n, address);
    if (result != NNES_OK) return result;
    return nnes_rd8_rmw(n, address, value);
}

nnes_result nnes_rd8_indexed(nnes *n, nnes_indexed_address address,
                             uint8_t *value)
{
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_seed_open_bus(n, (uint8_t)(address.base >> 8));
    if (result != NNES_OK) return result;
    if ((address.base & 0xff00) != (address.address & 0xff00)) {
        result = nnes_rd8_dummy(
            n, nnes_page_fixup_dummy_address(address.base, address.address));
        if (result != NNES_OK) return result;
    }
    return nnes_rd8(n, address.address, value);
}

nnes_result nnes_wr8_indexed(nnes *n, nnes_indexed_address address,
                             uint8_t value)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_seed_open_bus(n, (uint8_t)(address.base >> 8));
    if (result != NNES_OK) return result;
    result = nnes_rd8_dummy(
        n, nnes_page_fixup_dummy_address(address.base, address.address));
    if (result != NNES_OK) return result;
    return nnes_wr8(n, address.address, value);
}

nnes_result nnes_rd8_absolute_indexed(nnes *n, uint16_t base, uint8_t index,
                                      uint8_t *value)
{
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    return nnes_rd8_indexed(n, nnes_add_index16(base, index), value);
}

nnes_result nnes_wr8_absolute_indexed(nnes *n, uint16_t base, uint8_t index,
                                      uint8_t value)
{
    if (!n) return NNES_INVALID_ARGUMENT;
    return nnes_wr8_indexed(n, nnes_add_index16(base, index), value);
}

nnes_result nnes_rd8_zp_indexed_rmw(nnes *n, uint8_t base, uint8_t index,
                                   uint8_t *value)
{
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_seed_open_bus(n, base);
    if (result != NNES_OK) return result;
    result = nnes_rd8_dummy(n, base);
    if (result != NNES_OK) return result;
    return nnes_rd8_rmw(n, nnes_zp_index(base, index), value);
}

nnes_result nnes_rd8_zp_indexed_rmw_resolved(nnes *n, uint8_t base,
                                             uint8_t index,
                                             uint8_t *resolved,
                                             uint8_t *value)
{
    if (!n || !resolved || !value) return NNES_INVALID_ARGUMENT;
    uint8_t address = nnes_zp_index(base, index);
    uint8_t original;
    nnes_result result = nnes_rd8_zp_indexed_rmw(n, base, index, &original);
    if (result != NNES_OK) return result;
    *resolved = address;
    *value = original;
    return NNES_OK;
}

nnes_result nnes_rd8_indexed_rmw(nnes *n, nnes_indexed_address address,
                                 uint8_t *value)
{
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_seed_open_bus(n, (uint8_t)(address.base >> 8));
    if (result != NNES_OK) return result;
    result = nnes_rd8_dummy(
        n, nnes_page_fixup_dummy_address(address.base, address.address));
    if (result != NNES_OK) return result;
    return nnes_rd8_rmw(n, address.address, value);
}

nnes_result nnes_rd8_absolute_indexed_rmw(nnes *n, uint16_t base,
                                          uint8_t index,
                                          nnes_indexed_address *resolved,
                                          uint8_t *value)
{
    if (!n || !resolved || !value) return NNES_INVALID_ARGUMENT;
    nnes_indexed_address address = nnes_add_index16(base, index);
    uint8_t original;
    nnes_result result = nnes_rd8_indexed_rmw(n, address, &original);
    if (result != NNES_OK) return result;
    *resolved = address;
    *value = original;
    return NNES_OK;
}

nnes_result nnes_rd8_indirect_x_rmw(nnes *n, uint8_t pointer, uint8_t x,
                                    uint16_t *resolved, uint8_t *value)
{
    if (!n || !resolved || !value) return NNES_INVALID_ARGUMENT;
    uint16_t address;
    uint8_t original;
    nnes_result result = nnes_resolve_zp_indirect_x(n, pointer, x, &address);
    if (result != NNES_OK) return result;
    result = nnes_rd8_rmw(n, address, &original);
    if (result != NNES_OK) return result;
    *resolved = address;
    *value = original;
    return NNES_OK;
}

nnes_result nnes_rd8_indirect_y_rmw(nnes *n, uint8_t pointer, uint8_t y,
                                    nnes_indexed_address *resolved,
                                    uint8_t *value)
{
    if (!n || !resolved || !value) return NNES_INVALID_ARGUMENT;
    nnes_indexed_address address;
    uint8_t original;
    nnes_result result = nnes_resolve_zp_indirect_y(n, pointer, y, &address);
    if (result != NNES_OK) return result;
    result = nnes_rd8_indexed_rmw(n, address, &original);
    if (result != NNES_OK) return result;
    *resolved = address;
    *value = original;
    return NNES_OK;
}

nnes_result nnes_rd16(nnes *n, uint16_t address, uint16_t *value)
{
    uint8_t lo, hi;
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_rd8(n, address, &lo);
    if (result != NNES_OK) return result;
    result = nnes_rd8(n, (uint16_t)(address + 1), &hi);
    if (result != NNES_OK) return result;
    *value = (uint16_t)(lo | ((uint16_t)hi << 8));
    return NNES_OK;
}

nnes_result nnes_rd16_zp(nnes *n, uint8_t address, uint16_t *value)
{
    uint8_t lo, hi;
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_seed_open_bus(n, address);
    if (result != NNES_OK) return result;
    result = nnes_rd8(n, address, &lo);
    if (result != NNES_OK) return result;
    result = nnes_rd8(n, (uint8_t)(address + 1), &hi);
    if (result != NNES_OK) return result;
    *value = (uint16_t)(lo | ((uint16_t)hi << 8));
    return NNES_OK;
}

nnes_result nnes_resolve_zp_indirect_x(nnes *n, uint8_t pointer,
                                      uint8_t x, uint16_t *address)
{
    if (!n || !address) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_seed_open_bus(n, pointer);
    if (result != NNES_OK) return result;
    result = nnes_rd8_dummy(n, pointer);
    if (result != NNES_OK) return result;
    return nnes_rd16_zp(n, nnes_zp_index(pointer, x), address);
}

nnes_result nnes_resolve_zp_indirect_y(nnes *n, uint8_t pointer,
                                      uint8_t y,
                                      nnes_indexed_address *resolved)
{
    uint16_t base;
    if (!n || !resolved) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_rd16_zp(n, pointer, &base);
    if (result != NNES_OK) return result;
    *resolved = nnes_add_index16(base, y);
    return NNES_OK;
}

nnes_result nnes_rd8_indirect_x(nnes *n, uint8_t pointer, uint8_t x,
                               uint8_t *value)
{
    uint16_t address;
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_resolve_zp_indirect_x(n, pointer, x, &address);
    if (result != NNES_OK) return result;
    return nnes_rd8(n, address, value);
}

nnes_result nnes_wr8_indirect_x(nnes *n, uint8_t pointer, uint8_t x,
                               uint8_t value)
{
    uint16_t address;
    nnes_result result = nnes_resolve_zp_indirect_x(n, pointer, x, &address);
    if (result != NNES_OK) return result;
    return nnes_wr8(n, address, value);
}

nnes_result nnes_rd8_indirect_y(nnes *n, uint8_t pointer, uint8_t y,
                               uint8_t *value)
{
    nnes_indexed_address address;
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_resolve_zp_indirect_y(n, pointer, y, &address);
    if (result != NNES_OK) return result;
    return nnes_rd8_indexed(n, address, value);
}

nnes_result nnes_wr8_indirect_y(nnes *n, uint8_t pointer, uint8_t y,
                               uint8_t value)
{
    nnes_indexed_address address;
    nnes_result result = nnes_resolve_zp_indirect_y(n, pointer, y, &address);
    if (result != NNES_OK) return result;
    return nnes_wr8_indexed(n, address, value);
}

nnes_result nnes_rd16_jmp_indirect(nnes *n, uint16_t address, uint16_t *value)
{
    uint8_t lo, hi;
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    uint16_t high_address = (uint16_t)((address & 0xff00) |
                                       (uint8_t)(address + 1));
    nnes_result result = nnes_rd8(n, address, &lo);
    if (result != NNES_OK) return result;
    result = nnes_rd8(n, high_address, &hi);
    if (result != NNES_OK) return result;
    *value = (uint16_t)(lo | ((uint16_t)hi << 8));
    return NNES_OK;
}

nnes_result nnes_jump_indirect(nnes *n, uint16_t address)
{
    uint16_t target;
    if (!n) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_rd16_jmp_indirect(n, address, &target);
    if (result != NNES_OK) return result;
    return nnes_jump_absolute(n, target);
}

nnes_result nnes_wr16(nnes *n, uint16_t address, uint16_t value)
{
    nnes_result result = nnes_wr8(n, address, (uint8_t)value);
    if (result != NNES_OK) return result;
    return nnes_wr8(n, (uint16_t)(address + 1), (uint8_t)(value >> 8));
}

nnes_result nnes_wr16_zp(nnes *n, uint8_t address, uint16_t value)
{
    nnes_result result = nnes_wr8(n, address, (uint8_t)value);
    if (result != NNES_OK) return result;
    return nnes_wr8(n, (uint8_t)(address + 1), (uint8_t)(value >> 8));
}

nnes_result nnes_push8(nnes *n, uint8_t value)
{
    nnes_cpu_state *cpu = nnes_cpu(n);
    if (!cpu) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_wr8(n, (uint16_t)(0x0100 | cpu->s), value);
    if (result == NNES_OK) --cpu->s;
    return result;
}

nnes_result nnes_pull8(nnes *n, uint8_t *value)
{
    nnes_cpu_state *cpu = nnes_cpu(n);
    uint8_t result_byte;
    if (!cpu || !value) return NNES_INVALID_ARGUMENT;
    uint8_t next_s = (uint8_t)(cpu->s + 1);
    nnes_result result = nnes_rd8(n, (uint16_t)(0x0100 | next_s), &result_byte);
    if (result != NNES_OK) return result;
    cpu->s = next_s;
    *value = result_byte;
    return NNES_OK;
}

nnes_result nnes_push16(nnes *n, uint16_t value)
{
    nnes_result result = nnes_push8(n, (uint8_t)(value >> 8));
    if (result != NNES_OK) return result;
    return nnes_push8(n, (uint8_t)value);
}

nnes_result nnes_pull16(nnes *n, uint16_t *value)
{
    uint8_t lo, hi;
    if (!n || !value) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_pull8(n, &lo);
    if (result != NNES_OK) return result;
    result = nnes_pull8(n, &hi);
    if (result != NNES_OK) return result;
    *value = (uint16_t)(lo | ((uint16_t)hi << 8));
    return NNES_OK;
}

nnes_result nnes_dummy_next_pc(nnes *n)
{
    nnes_cpu_state *cpu = nnes_cpu(n);
    if (!cpu) return NNES_INVALID_ARGUMENT;
    return nnes_rd8_dummy(n, (uint16_t)(cpu->pc + 1));
}

static nnes_result dummy_stack_read(nnes *n)
{
    nnes_cpu_state *cpu = nnes_cpu(n);
    if (!cpu) return NNES_INVALID_ARGUMENT;
    return nnes_rd8_dummy(n, (uint16_t)(0x0100 | cpu->s));
}

nnes_result nnes_pha(nnes *n)
{
    nnes_cpu_state *cpu = nnes_cpu(n);
    if (!cpu) return NNES_INVALID_ARGUMENT;
    uint8_t value = cpu->a;
    nnes_result result = nnes_dummy_next_pc(n);
    if (result != NNES_OK) return result;
    return nnes_push8(n, value);
}

nnes_result nnes_pla(nnes *n)
{
    nnes_cpu_state *cpu = nnes_cpu(n);
    uint8_t value;
    if (!cpu) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_dummy_next_pc(n);
    if (result != NNES_OK) return result;
    result = dummy_stack_read(n);
    if (result != NNES_OK) return result;
    result = nnes_pull8(n, &value);
    if (result == NNES_OK) {
        nnes_alu8_result load = nnes_load8(cpu->p, value);
        cpu->a = load.value;
        cpu->p = load.p;
    }
    return result;
}

nnes_result nnes_php(nnes *n)
{
    nnes_cpu_state *cpu = nnes_cpu(n);
    if (!cpu) return NNES_INVALID_ARGUMENT;
    uint8_t value = (uint8_t)(cpu->p | NNES_P_B | NNES_P_U);
    nnes_result result = nnes_dummy_next_pc(n);
    if (result != NNES_OK) return result;
    return nnes_push8(n, value);
}

nnes_result nnes_plp(nnes *n)
{
    nnes_cpu_state *cpu = nnes_cpu(n);
    uint8_t value;
    if (!cpu) return NNES_INVALID_ARGUMENT;
    nnes_result result = nnes_dummy_next_pc(n);
    if (result != NNES_OK) return result;
    result = dummy_stack_read(n);
    if (result != NNES_OK) return result;
    result = nnes_pull8(n, &value);
    if (result == NNES_OK)
        cpu->p = nnes_p_set_flags(value, NNES_P_U);
    return result;
}

nnes_result nnes_rts(nnes *n)
{
    nnes_cpu_state *cpu = nnes_cpu(n);
    uint16_t return_address;
    if (!cpu) return NNES_INVALID_ARGUMENT;

    nnes_result result = nnes_dummy_next_pc(n);
    if (result != NNES_OK) return result;
    result = dummy_stack_read(n);
    if (result != NNES_OK) return result;
    result = nnes_pull16(n, &return_address);
    if (result != NNES_OK) return result;
    result = nnes_rd8_dummy(n, return_address);
    if (result != NNES_OK) return result;

    cpu->pc = (uint16_t)(return_address + 1);
    return NNES_OK;
}

nnes_result nnes_jump_rts(nnes *n)
{
    nnes_result result = nnes_rts(n);
    if (result != NNES_OK) return result;
    nnes_cpu_state *cpu = nnes_cpu(n);
    return nnes_jump_absolute(n, cpu->pc);
}
