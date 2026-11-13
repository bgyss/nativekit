#include "ppu_internal.h"

#include <string.h>

enum {
    PPU_FRAME_DOTS = 341 * 262,
    PPU_VBLANK_START = 241 * 341 + 1,
    PPU_VBLANK_END = 261 * 341 + 1,
    PPU_STATUS_VBLANK = 0x80
};

void nnes_ppu_init(nnes_ppu *ppu)
{
    if (ppu) memset(ppu, 0, sizeof(*ppu));
}

static void ppu_advance_dots(nnes_ppu *ppu, uint64_t dots)
{
    if (dots >= PPU_FRAME_DOTS) {
        uint64_t full_frames = dots / PPU_FRAME_DOTS;
        dots %= PPU_FRAME_DOTS;
        if (full_frames) {
            if (ppu->ctrl & 0x80) ppu->nmi_pending = 1;
            if (ppu->frame_dot >= PPU_VBLANK_START &&
                ppu->frame_dot < PPU_VBLANK_END)
                ppu->status |= PPU_STATUS_VBLANK;
            else
                ppu->status &= (uint8_t)~PPU_STATUS_VBLANK;
        }
    }

    while (dots) {
        uint64_t next = ppu->frame_dot < PPU_VBLANK_START
            ? PPU_VBLANK_START
            : ppu->frame_dot < PPU_VBLANK_END
                ? PPU_VBLANK_END
                : PPU_FRAME_DOTS;
        uint64_t step = next - ppu->frame_dot;
        if (step > dots) step = dots;
        ppu->frame_dot += step;
        dots -= step;

        if (ppu->frame_dot == PPU_VBLANK_START) {
            ppu->status |= PPU_STATUS_VBLANK;
            if (ppu->ctrl & 0x80) ppu->nmi_pending = 1;
        } else if (ppu->frame_dot == PPU_VBLANK_END) {
            ppu->status &= (uint8_t)~PPU_STATUS_VBLANK;
        } else if (ppu->frame_dot == PPU_FRAME_DOTS) {
            ppu->frame_dot = 0;
        }
    }
}

void nnes_ppu_advance_tenths(nnes_ppu *ppu, uint64_t tenths)
{
    if (!ppu) return;
    uint64_t cpu_cycles = tenths / 10;
    unsigned fractional = ppu->time_remainder + (unsigned)(tenths % 10);
    cpu_cycles += fractional / 10;
    ppu->time_remainder = (uint8_t)(fractional % 10);
    ppu_advance_dots(ppu, cpu_cycles * 3);
}

static nnes_result ppu_get_mirroring(const nnes_ppu_mapper *mapper,
                                     nnes_mirroring *mirroring)
{
    if (mapper && mapper->mirroring)
        return mapper->mirroring(mapper->user, mirroring);
    *mirroring = NNES_MIRROR_HORIZONTAL;
    return NNES_OK;
}

static nnes_result ppu_memory_read(nnes_ppu *ppu,
                                  const nnes_ppu_mapper *mapper,
                                  uint16_t address, uint8_t *value)
{
    address &= 0x3fff;
    if (address < 0x2000) {
        if (!mapper || !mapper->read) return NNES_UNSUPPORTED;
        return mapper->read(mapper->user, address, value);
    }
    if (address < 0x3f00) {
        address &= 0x2fff;
        if (address >= 0x3000) address -= 0x1000;
        nnes_mirroring mirroring;
        nnes_result result = ppu_get_mirroring(mapper, &mirroring);
        if (result != NNES_OK) return result;
        unsigned table = (address - 0x2000) >> 10;
        unsigned offset = address & 0x03ff;
        switch (mirroring) {
        case NNES_MIRROR_SINGLE_LOWER: table = 0; break;
        case NNES_MIRROR_SINGLE_UPPER: table = 1; break;
        case NNES_MIRROR_VERTICAL: table &= 1; break;
        case NNES_MIRROR_HORIZONTAL: table >>= 1; break;
        case NNES_MIRROR_FOUR_SCREEN: break;
        default: return NNES_INVALID_ARGUMENT;
        }
        *value = ppu->nametable[table * 0x400 + offset];
        return NNES_OK;
    }

    unsigned index = (address - 0x3f00) & 0x1f;
    if (index >= 0x10 && (index & 3) == 0) index -= 0x10;
    *value = ppu->palette[index] & 0x3f;
    return NNES_OK;
}

static nnes_result ppu_memory_write(nnes_ppu *ppu,
                                   const nnes_ppu_mapper *mapper,
                                   uint16_t address, uint8_t value)
{
    address &= 0x3fff;
    if (address < 0x2000) {
        if (!mapper || !mapper->write) return NNES_UNSUPPORTED;
        return mapper->write(mapper->user, address, value);
    }
    if (address < 0x3f00) {
        address &= 0x2fff;
        if (address >= 0x3000) address -= 0x1000;
        nnes_mirroring mirroring;
        nnes_result result = ppu_get_mirroring(mapper, &mirroring);
        if (result != NNES_OK) return result;
        unsigned table = (address - 0x2000) >> 10;
        unsigned offset = address & 0x03ff;
        switch (mirroring) {
        case NNES_MIRROR_SINGLE_LOWER: table = 0; break;
        case NNES_MIRROR_SINGLE_UPPER: table = 1; break;
        case NNES_MIRROR_VERTICAL: table &= 1; break;
        case NNES_MIRROR_HORIZONTAL: table >>= 1; break;
        case NNES_MIRROR_FOUR_SCREEN: break;
        default: return NNES_INVALID_ARGUMENT;
        }
        ppu->nametable[table * 0x400 + offset] = value;
        return NNES_OK;
    }

    unsigned index = (address - 0x3f00) & 0x1f;
    if (index >= 0x10 && (index & 3) == 0) index -= 0x10;
    ppu->palette[index] = value & 0x3f;
    return NNES_OK;
}

static void ppu_increment_address(nnes_ppu *ppu)
{
    ppu->v = (uint16_t)((ppu->v + ((ppu->ctrl & 0x04) ? 32 : 1)) & 0x7fff);
}

nnes_result nnes_ppu_cpu_read(nnes_ppu *ppu, const nnes_ppu_mapper *mapper,
                              uint16_t address, uint8_t *value)
{
    if (!ppu || !value || address < 0x2000 || address >= 0x4000)
        return NNES_INVALID_ARGUMENT;
    unsigned reg = address & 7;
    uint8_t result;
    if (reg == 2) {
        result = (uint8_t)((ppu->status & 0xe0) | (ppu->io_latch & 0x1f));
        ppu->status &= (uint8_t)~PPU_STATUS_VBLANK;
        ppu->write_toggle = 0;
    } else if (reg == 4) {
        result = ppu->oam[ppu->oam_address];
    } else if (reg == 7) {
        uint16_t ppu_address = ppu->v & 0x3fff;
        uint8_t memory_value;
        nnes_result read_result = ppu_memory_read(ppu, mapper, ppu_address,
                                                  &memory_value);
        if (read_result != NNES_OK) return read_result;
        if (ppu_address >= 0x3f00) {
            result = (uint8_t)((ppu->io_latch & 0xc0) | memory_value);
            uint8_t buffered;
            read_result = ppu_memory_read(ppu, mapper,
                                           (uint16_t)(ppu_address - 0x1000),
                                           &buffered);
            if (read_result == NNES_OK) ppu->read_buffer = buffered;
        } else {
            result = ppu->read_buffer;
            ppu->read_buffer = memory_value;
        }
        ppu_increment_address(ppu);
    } else {
        result = ppu->io_latch;
    }
    ppu->io_latch = result;
    *value = result;
    return NNES_OK;
}

nnes_result nnes_ppu_cpu_write(nnes_ppu *ppu, const nnes_ppu_mapper *mapper,
                               uint16_t address, uint8_t value)
{
    if (!ppu || address < 0x2000 || address >= 0x4000)
        return NNES_INVALID_ARGUMENT;
    unsigned reg = address & 7;
    ppu->io_latch = value;
    switch (reg) {
    case 0: {
        uint8_t old_nmi = ppu->ctrl & 0x80;
        ppu->ctrl = value;
        ppu->t = (uint16_t)((ppu->t & 0x73ff) | ((uint16_t)(value & 3) << 10));
        if (!old_nmi && (value & 0x80) && (ppu->status & PPU_STATUS_VBLANK))
            ppu->nmi_pending = 1;
        return NNES_OK;
    }
    case 1:
        ppu->mask = value;
        return NNES_OK;
    case 2:
        return NNES_OK;
    case 3:
        ppu->oam_address = value;
        return NNES_OK;
    case 4:
        ppu->oam[ppu->oam_address++] = value;
        return NNES_OK;
    case 5:
        if (!ppu->write_toggle) {
            ppu->fine_x = value & 7;
            ppu->t = (uint16_t)((ppu->t & 0x7fe0) | (value >> 3));
        } else {
            ppu->t = (uint16_t)((ppu->t & 0x0c1f) |
                                ((uint16_t)(value & 0xf8) << 2) |
                                ((uint16_t)(value & 7) << 12));
        }
        ppu->write_toggle ^= 1;
        return NNES_OK;
    case 6:
        if (!ppu->write_toggle) {
            ppu->t = (uint16_t)((ppu->t & 0x00ff) |
                                ((uint16_t)(value & 0x3f) << 8));
        } else {
            ppu->t = (uint16_t)((ppu->t & 0x7f00) | value);
            ppu->v = ppu->t;
        }
        ppu->write_toggle ^= 1;
        return NNES_OK;
    case 7: {
        nnes_result write_result = ppu_memory_write(ppu, mapper,
                                                    ppu->v & 0x3fff, value);
        if (write_result != NNES_OK) return write_result;
        ppu_increment_address(ppu);
        return NNES_OK;
    }
    }
    return NNES_INVALID_ARGUMENT;
}

nnes_result nnes_ppu_take_pending_nmi(nnes_ppu *ppu, int *pending)
{
    if (!ppu || !pending) return NNES_INVALID_ARGUMENT;
    *pending = ppu->nmi_pending != 0;
    ppu->nmi_pending = 0;
    return NNES_OK;
}
