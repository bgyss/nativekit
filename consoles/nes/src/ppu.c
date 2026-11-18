#include "ppu_internal.h"

#include <string.h>

enum {
    PPU_FRAME_DOTS = 341 * 262,
    /* MesenCE's default NES input scanline is 241, sampled at cycle 0. */
    PPU_INPUT_POLL = 241 * 341,
    PPU_VBLANK_START = 241 * 341 + 1,
    PPU_VBLANK_END = 261 * 341 + 1,
    PPU_STATUS_SPRITE0_HIT = 0x40,
    PPU_STATUS_VBLANK = 0x80
};

static nnes_result ppu_memory_read(nnes_ppu *ppu,
                                  const nnes_ppu_mapper *mapper,
                                  uint16_t address, uint8_t *value);
static nnes_result ppu_check_sprite0_hit(nnes_ppu *ppu,
                                         const nnes_ppu_mapper *mapper,
                                         uint64_t first_dot,
                                         uint64_t last_dot);

void nnes_ppu_init(nnes_ppu *ppu)
{
    if (ppu) memset(ppu, 0, sizeof(*ppu));
}

static nnes_result ppu_advance_dots(nnes_ppu *ppu,
                                    const nnes_ppu_mapper *mapper,
                                    uint64_t dots,
                                    nnes_ppu_frame_fn frame, void *user)
{
    int sprite_rendering = mapper && mapper->read &&
                           (ppu->mask & 0x18) == 0x18;
    if (dots >= PPU_FRAME_DOTS && !frame && !sprite_rendering) {
        uint64_t full_frames = dots / PPU_FRAME_DOTS;
        dots %= PPU_FRAME_DOTS;
        if (full_frames) {
            ppu->frame_count += full_frames;
            if (ppu->ctrl & 0x80) ppu->nmi_pending = 1;
            ppu->status &= (uint8_t)~PPU_STATUS_SPRITE0_HIT;
            if (ppu->frame_dot >= PPU_VBLANK_START &&
                ppu->frame_dot < PPU_VBLANK_END)
                ppu->status |= PPU_STATUS_VBLANK;
            else
                ppu->status &= (uint8_t)~PPU_STATUS_VBLANK;
        }
    }

    while (dots) {
        uint64_t next = ppu->frame_dot < PPU_INPUT_POLL
            ? PPU_INPUT_POLL
            : ppu->frame_dot < PPU_VBLANK_START
                ? PPU_VBLANK_START
                : ppu->frame_dot < PPU_VBLANK_END
                    ? PPU_VBLANK_END
                    : PPU_FRAME_DOTS;
        uint64_t step = next - ppu->frame_dot;
        if (step > dots) step = dots;
        if (sprite_rendering &&
            !(ppu->status & PPU_STATUS_SPRITE0_HIT)) {
            nnes_result result = ppu_check_sprite0_hit(
                ppu, mapper, ppu->frame_dot + 1, ppu->frame_dot + step);
            if (result != NNES_OK) return result;
        }
        ppu->frame_dot += step;
        dots -= step;

        if (ppu->frame_dot == PPU_INPUT_POLL) {
            if (frame) frame(user, ppu->frame_count + 2);
        } else if (ppu->frame_dot == PPU_VBLANK_START) {
            ppu->status |= PPU_STATUS_VBLANK;
            if (ppu->ctrl & 0x80) ppu->nmi_pending = 1;
        } else if (ppu->frame_dot == PPU_VBLANK_END) {
            ppu->status &= (uint8_t)~PPU_STATUS_VBLANK;
        } else if (ppu->frame_dot == PPU_FRAME_DOTS) {
            ppu->frame_dot = 0;
            ++ppu->frame_count;
            ppu->status &= (uint8_t)~PPU_STATUS_SPRITE0_HIT;
        }
    }
    return NNES_OK;
}

nnes_result nnes_ppu_advance_tenths(nnes_ppu *ppu,
                                    const nnes_ppu_mapper *mapper,
                                    uint64_t tenths,
                                    nnes_ppu_frame_fn frame, void *user)
{
    if (!ppu) return NNES_INVALID_ARGUMENT;
    uint64_t cpu_cycles = tenths / 10;
    unsigned fractional = ppu->time_remainder + (unsigned)(tenths % 10);
    cpu_cycles += fractional / 10;
    if (cpu_cycles > UINT64_MAX / 3) return NNES_CYCLE_OVERFLOW;
    ppu->time_remainder = (uint8_t)(fractional % 10);
    return ppu_advance_dots(ppu, mapper, cpu_cycles * 3, frame, user);
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

static nnes_result ppu_background_pixel_opaque(nnes_ppu *ppu,
                                                const nnes_ppu_mapper *mapper,
                                                uint16_t x, uint16_t scanline,
                                                int *opaque)
{
    *opaque = 0;
    if (!(ppu->mask & 0x08) || (x < 8 && !(ppu->mask & 0x02)))
        return NNES_OK;

    uint32_t scroll_x = ((uint32_t)(ppu->t & 0x001f) << 3) | ppu->fine_x;
    uint32_t scroll_y = ((uint32_t)((ppu->t >> 5) & 0x001f) << 3) |
                        ((ppu->t >> 12) & 7);
    uint32_t world_x = (((ppu->t >> 10) & 1) * 256u + scroll_x + x) % 512u;
    uint32_t world_y = (((ppu->t >> 11) & 1) * 240u + scroll_y +
                        scanline) % 480u;
    uint16_t coarse_x = (uint16_t)((world_x % 256u) >> 3);
    uint16_t coarse_y = (uint16_t)((world_y % 240u) >> 3);
    uint16_t table_x = (uint16_t)(world_x / 256u);
    uint16_t table_y = (uint16_t)(world_y / 240u);
    uint16_t tile_address = (uint16_t)(0x2000 | (table_y << 11) |
                                       (table_x << 10) | (coarse_y << 5) |
                                       coarse_x);
    uint8_t tile = 0;
    nnes_result result = ppu_memory_read(ppu, mapper, tile_address, &tile);
    if (result != NNES_OK) return result;

    uint16_t pattern_address = (uint16_t)(((ppu->ctrl & 0x10) ? 0x1000 : 0) |
                                          ((uint16_t)tile << 4) |
                                          (world_y & 7));
    uint8_t low = 0, high = 0;
    result = ppu_memory_read(ppu, mapper, pattern_address, &low);
    if (result != NNES_OK) return result;
    result = ppu_memory_read(ppu, mapper,
                             (uint16_t)(pattern_address + 8), &high);
    if (result != NNES_OK) return result;

    unsigned bit = 7u - (world_x & 7u);
    *opaque = (((low >> bit) | (high >> bit)) & 1u) != 0;
    return NNES_OK;
}

static nnes_result ppu_first_sprite_pixel_opaque(nnes_ppu *ppu,
                                                 const nnes_ppu_mapper *mapper,
                                                 uint16_t x, uint16_t scanline,
                                                 int *opaque)
{
    *opaque = 0;
    if (!(ppu->mask & 0x10) || (x < 8 && !(ppu->mask & 0x04)))
        return NNES_OK;

    uint8_t height = (ppu->ctrl & 0x20) ? 16 : 8;
    unsigned first_sprite = (ppu->oam_address & 0xfc) >> 2;
    unsigned selected = 64;
    uint8_t row = 0;
    for (unsigned offset = 0; offset < 64; ++offset) {
        unsigned index = (first_sprite + offset) & 0x3f;
        uint8_t sprite_y = ppu->oam[index * 4];
        uint8_t candidate_row = (uint8_t)((uint8_t)scanline - sprite_y);
        if (candidate_row < height) {
            selected = index;
            row = candidate_row;
            break;
        }
    }
    if (selected == 64) return NNES_OK;

    uint8_t tile = ppu->oam[selected * 4 + 1];
    uint8_t attributes = ppu->oam[selected * 4 + 2];
    uint8_t sprite_x = ppu->oam[selected * 4 + 3];
    if (x < sprite_x || x >= (uint16_t)sprite_x + 8) return NNES_OK;
    if (attributes & 0x80) row ^= (uint8_t)(height - 1);

    uint16_t pattern_address;
    if (height == 16) {
        pattern_address = (uint16_t)(((uint16_t)(tile & 1) << 12) |
                                     ((uint16_t)(tile & 0xfe) << 4) |
                                     ((uint16_t)(row & 8) << 1) |
                                     (row & 7));
    } else {
        pattern_address = (uint16_t)(((ppu->ctrl & 0x08) ? 0x1000 : 0) |
                                     ((uint16_t)tile << 4) | row);
    }

    uint8_t low = 0, high = 0;
    nnes_result result = ppu_memory_read(ppu, mapper, pattern_address, &low);
    if (result != NNES_OK) return result;
    result = ppu_memory_read(ppu, mapper,
                             (uint16_t)(pattern_address + 8), &high);
    if (result != NNES_OK) return result;

    unsigned column = x - sprite_x;
    unsigned bit = (attributes & 0x40) ? column : 7u - column;
    *opaque = (((low >> bit) | (high >> bit)) & 1u) != 0;
    return NNES_OK;
}

static nnes_result ppu_check_sprite0_hit(nnes_ppu *ppu,
                                         const nnes_ppu_mapper *mapper,
                                         uint64_t first_dot,
                                         uint64_t last_dot)
{
    if (!mapper || !mapper->read || (ppu->mask & 0x18) != 0x18 ||
        (ppu->status & PPU_STATUS_SPRITE0_HIT))
        return NNES_OK;

    uint64_t dot = first_dot;
    while (dot <= last_dot) {
        uint16_t scanline = (uint16_t)(dot / 341u);
        uint16_t cycle = (uint16_t)(dot % 341u);
        if (scanline >= 239) break;
        if (cycle == 0) {
            dot = (uint64_t)scanline * 341u + 1;
            continue;
        }
        if (cycle > 255) {
            dot = (uint64_t)(scanline + 1) * 341u + 1;
            continue;
        }

        uint16_t x = (uint16_t)(cycle - 1);
        int sprite_opaque = 0, background_opaque = 0;
        nnes_result result = ppu_first_sprite_pixel_opaque(
            ppu, mapper, x, scanline, &sprite_opaque);
        if (result != NNES_OK) return result;
        if (sprite_opaque) {
            result = ppu_background_pixel_opaque(ppu, mapper, x, scanline,
                                                 &background_opaque);
            if (result != NNES_OK) return result;
            if (background_opaque) {
                ppu->status |= PPU_STATUS_SPRITE0_HIT;
                return NNES_OK;
            }
        }
        ++dot;
    }
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
