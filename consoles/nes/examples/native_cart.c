/* Synthetic cartridge data and native C: no ROM or CPU instruction stream. */
#include "nnes/nes.h"
#include "nnes/machine.h"
#include <stdio.h>

static nnes_result cart_read(void *user, uint16_t address, uint8_t *value)
{
    (void)user;
    switch (address) {
    case 0xfffc: *value = 0x00; return NNES_OK;
    case 0xfffd: *value = 0x80; return NNES_OK;
    case 0x8000: *value = 0x00; return NNES_OK; /* indexed page-cross dummy read */
    case 0x8002: *value = 0xa9; return NNES_OK; /* branch dummy read */
    case 0x8008: *value = 0xea; return NNES_OK; /* PHA next-PC dummy read */
    case 0x801f: *value = 0xea; return NNES_OK; /* RTS return-address read */
    case 0x8021: *value = 0xea; return NNES_OK; /* RTS next-PC read */
    case 0x8100: *value = 0x2a; return NNES_OK; /* game data */
    default: return NNES_UNSUPPORTED;
    }
}

static nnes_access_kind cart_access_kind(void *user, uint16_t address)
{
    (void)user;
    return address >= 0x6000 && address < 0x8000 ?
           NNES_KIND_CART_RAM : NNES_KIND_ROM;
}

/* The recovered subroutine at $8020 applies ANC #$0F to the value in A. */
static nnes_result mask_game_value(nnes *console, void *user)
{
    nnes_cpu_state *cpu = nnes_cpu(console);
    (void)user;
    return nnes_alu8_commit(cpu, &cpu->a,
                            nnes_anc8(cpu->p, cpu->a, 0x0f));
}

/* The recovered routine at $8000 is ordinary C. Bus accesses stay explicit.
 * Local control flow is C; computed targets use the cartridge routine table. */
static nnes_result reset(nnes *console, void *user)
{
    nnes_cpu_state *cpu = nnes_cpu(console);
    nnes_indexed_address rmw_address;
    uint8_t operand;
    (void)user;
    nnes_result result = nnes_wr16_zp(console, 0x20, 0x80ff);
    if (result != NNES_OK) return result;
    result = nnes_rd8_indirect_y(console, 0x20, 1, &operand);
    if (result != NNES_OK) return result;
    /* A memory NOP still makes a normal read from its data address. */
    result = nnes_rd8_discard(console, 0x8100);
    if (result != NNES_OK) return result;
    result = nnes_alu8_commit(cpu, &cpu->a, nnes_load8(cpu->p, operand));
    if (result != NNES_OK) return result;
    /* BNE skips LDA #$00; the branch helper retains its dummy bus read. */
    int skip_clear = 0;
    result = nnes_branch_relative(console, NNES_BRANCH_BNE, 0x8002, 0x02,
                                  &skip_clear);
    if (result != NNES_OK) return result;
    if (!skip_clear) {
        result = nnes_alu8_commit(cpu, &cpu->a, nnes_load8(cpu->p, 0));
        if (result != NNES_OK) return result;
    }
    cpu->pc = 0x8004;
    cpu->pc = 0x8020;
    result = nnes_call(console, 0x8020);
    if (result != NNES_OK) return result;
    /* SLO ($22),Y with Y=0: resolve, RMW, then OR the shifted value into A. */
    result = nnes_wr8(console, 0x0011, 0x80);
    if (result != NNES_OK) return result;
    result = nnes_wr16_zp(console, 0x22, 0x0011);
    if (result != NNES_OK) return result;
    result = nnes_rd8_indirect_y_rmw(console, 0x22, cpu->y,
                                     &rmw_address, &operand);
    if (result != NNES_OK) return result;
    nnes_rmw8_result slo = nnes_slo8(cpu->p, cpu->a, operand);
    nnes_alu8_result slo_alu = {slo.a, slo.p};
    result = nnes_alu8_commit(cpu, &cpu->a, slo_alu);
    if (result != NNES_OK) return result;
    result = nnes_wr8_rmw(console, rmw_address.address, slo.memory);
    if (result != NNES_OK) return result;
    cpu->pc = 0x8007;
    result = nnes_pha(console);
    if (result != NNES_OK) return result;
    result = nnes_alu8_commit(cpu, &cpu->a, nnes_load8(cpu->p, 0));
    if (result != NNES_OK) return result;
    result = nnes_pla(console);
    if (result != NNES_OK) return result;
    result = nnes_wr8(console, 0x0010, cpu->a);
    if (result != NNES_OK) return result;
    /* INC $0010,X with X=0: retain the resolved address for the RMW write. */
    result = nnes_rd8_absolute_indexed_rmw(console, 0x0010, cpu->x,
                                           &rmw_address, &operand);
    if (result != NNES_OK) return result;
    result = nnes_alu8_commit(cpu, &operand, nnes_inc8(cpu->p, operand));
    if (result != NNES_OK) return result;
    return nnes_wr8_rmw(console, rmw_address.address, operand);
}

int main(void)
{
    const nnes_routine routines[] = {
        {0x8000, reset},
        {0x8020, mask_game_value}
    };
    /* Synthetic weights illustrate the API; game profiles are measured from
     * their original code. */
    const nnes_cost_profile cost_profile = {
        .version = NNES_COST_PROFILE_VERSION,
        .cpu_tenths = {
            [0] = {[NNES_KIND_ROM] = 3, [NNES_KIND_CART_RAM] = 1},
            [1] = {[NNES_KIND_CART_RAM] = 1}
        }
    };
    const nnes_cartridge cartridge = {
        .user = NULL,
        .read = cart_read,
        .write = NULL,
        .routines = routines,
        .routine_count = sizeof(routines) / sizeof(routines[0]),
        .cost_profile = &cost_profile,
        .access_kind = cart_access_kind
    };
    nnes *console = NULL;
    uint8_t value = 0, indexed_value = 0, slo_value = 0;
    nnes_result result = nnes_create(&cartridge, &console);
    if (result == NNES_OK) result = nnes_start(console);
    /* The high-byte pointer wraps from $00FF to $0000 before dispatch. */
    if (result == NNES_OK)
        result = nnes_wr16_zp(console, 0x00ff, 0x8020);
    if (result == NNES_OK) result = nnes_jump_indirect(console, 0x00ff);
    if (result == NNES_OK) result = nnes_push16(console, 0x801f);
    if (result == NNES_OK) result = nnes_jump_rts(console);
    if (result == NNES_OK) result = nnes_jump_absolute(console, 0x8020);
    if (result == NNES_OK)
        result = nnes_rd8_absolute_indexed(console, 0x80ff, 1,
                                           &indexed_value);
    if (result == NNES_OK) result = nnes_read(console, 0x0011, &slo_value);
    if (result == NNES_OK) result = nnes_read(console, 0x0010, &value);
    nnes_destroy(console);
    if (result != NNES_OK || value != 0x0b || indexed_value != 0x2a ||
        slo_value != 0) return 1;
    puts("Native NES reset routine masked and incremented cartridge data");
    return 0;
}
