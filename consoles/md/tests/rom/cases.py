"""Shared bus operations used by the BlastEm ROM and native testcart."""
from dataclasses import dataclass


READ = 1
WRITE = 2
TAS = 3
POLL = 4


@dataclass(frozen=True)
class Operation:
    code: int
    width: int
    addr: int
    value: int = 0
    compare_mask: int = 0


@dataclass(frozen=True)
class Case:
    name: str
    operations: tuple[Operation, ...]
    direct: bool = False
    render: bool = False
    region: str = "U"


def _vdp_reg(index, value):
    return Operation(WRITE, 2, 0xC00004, 0x8000 | (index << 8) | value)


def _vdp_command(code, address):
    first = ((code & 3) << 14) | (address & 0x3FFF)
    second = ((code & 0x3C) << 2) | ((address >> 14) & 3)
    return (first << 16) | second


def _memory_dma_case(name, write_code, read_code, display=0x14, h40=False, words=2):
    source = 0x4000
    source_hi = (source >> 17) & 0x7F
    source_mid = (source >> 9) & 0xFF
    source_lo = (source >> 1) & 0xFF
    operations = []
    if display & 0x40:
        operations.append(_vdp_reg(0, 0x04))  # Select Mode 5 before the display starts.
    operations.extend([
        _vdp_reg(1, display),
        _vdp_reg(12, 0x81 if h40 else 0),
        _vdp_reg(15, 2),
        _vdp_reg(19, words & 0xFF),
        _vdp_reg(20, (words >> 8) & 0xFF),
        _vdp_reg(21, source_lo),
        _vdp_reg(22, source_mid),
        _vdp_reg(23, source_hi),
        Operation(WRITE, 4, 0xC00004, _vdp_command(write_code | 0x20, 0)),
        Operation(WRITE, 4, 0xC00004, _vdp_command(read_code, 0)),
        *(Operation(READ, 2, 0xC00000) for _ in range(words)),
    ])
    return Case(name, tuple(operations))


def _vdp_write_words(code, address, words):
    return [Operation(WRITE, 4, 0xC00004, _vdp_command(code, address)),
            *(Operation(WRITE, 2, 0xC00000, word) for word in words)]


def _render_scene(name, tile_colors, plane_a_rows=None, plane_b_row=None,
                  window_row=None, hscroll=(0, 0), vsram=(0, 0),
                  mode11=0, h40=False, v30=False, plane_size=0,
                  window_h=0, window_v=0, region="U"):
    width_cells = 64 if plane_size & 0x03 == 1 else 32
    operations = [
        Operation(POLL, 2, 0xC00004, 0x0008, compare_mask=0x0008),
        _vdp_reg(0, 0x04),
        _vdp_reg(1, 0x44 | (0x08 if v30 else 0)),
        _vdp_reg(2, 0x10),
        _vdp_reg(3, 0x20),
        _vdp_reg(4, 0x03),
        _vdp_reg(7, 0),
        _vdp_reg(11, mode11),
        _vdp_reg(12, 0x81 if h40 else 0),
        _vdp_reg(13, 0x2E),
        _vdp_reg(15, 2),
        _vdp_reg(16, plane_size),
        _vdp_reg(17, window_h),
        _vdp_reg(18, window_v),
    ]

    for tile, color in sorted(tile_colors.items()):
        packed = color * 0x1111
        operations.extend(_vdp_write_words(1, tile * 32, [packed] * 16))
    operations.extend(_vdp_write_words(3, 2, [0x000E, 0x00E0, 0x0E00, 0x0EEE]))

    for row, entries in sorted((plane_a_rows or {}).items()):
        operations.extend(_vdp_write_words(1, 0x4000 + row * width_cells * 2, entries))
    if plane_b_row:
        operations.extend(_vdp_write_words(1, 0x6000, plane_b_row))
    if window_row:
        operations.extend(_vdp_write_words(1, 0x8000, window_row))
    operations.extend(_vdp_write_words(1, 0xB800, hscroll))
    if vsram != (0, 0):
        operations.extend(_vdp_write_words(5, 0, vsram))
    return Case(name, tuple(operations), render=True, region=region)


def all_cases():
    return [Case("bus_map", (
        Operation(READ, 2, 0x00004000),
        Operation(WRITE, 2, 0xE00000, 0xABCD),
        Operation(READ, 2, 0xFF0000),
        Operation(WRITE, 1, 0xE00000, 0x12),
        Operation(READ, 2, 0xFF0000),
        Operation(WRITE, 4, 0xFF0002, 0x13579BDF),
        Operation(READ, 4, 0xE00002),
        Operation(READ, 2, 0x01004000),
        Operation(WRITE, 2, 0xA11000, 0x0100),
        Operation(WRITE, 2, 0xA04000, 0x3000),
        Operation(WRITE, 1, 0xA06000, 0x01),
        Operation(WRITE, 1, 0xA10003, 0xFF),
        Operation(WRITE, 2, 0xC00004, 0x8104),
        Operation(WRITE, 2, 0xC00006, 0x8104),
        Operation(WRITE, 2, 0xD00004, 0x8104),
        Operation(WRITE, 2, 0xC00004, 0x8F02),
        Operation(WRITE, 4, 0xC00004, 0x40800000),
        Operation(WRITE, 2, 0xC00000, 0x000E),
        Operation(WRITE, 1, 0xC00011, 0x9F),
        Operation(WRITE, 2, 0xA11200, 0x0000),
        Operation(WRITE, 2, 0xA11100, 0x0100),
        Operation(WRITE, 2, 0xA11200, 0x0100),
        Operation(POLL, 2, 0xA11100, 0x0000, compare_mask=0x0100),
        Operation(WRITE, 1, 0xA00000, 0x5A),
        Operation(READ, 1, 0xA00000),
        Operation(WRITE, 2, 0xA11100, 0x0000),
        Operation(POLL, 2, 0xA11100, 0x0100, compare_mask=0x0100),
        Operation(WRITE, 1, 0xFF0010, 0x00),
        Operation(READ, 1, 0xFF0010),
    )), Case("tas_write", (
        Operation(WRITE, 1, 0xFF0020, 0x00),
        Operation(TAS, 1, 0xFF0020),
        Operation(READ, 1, 0xFF0020),
    )), Case("vdp_port_timing", (
        Operation(WRITE, 2, 0xC00004, 0x8104),
        Operation(WRITE, 2, 0xC00000, 0x000E),
        Operation(READ, 2, 0xC00004, compare_mask=0x0001),
        Operation(READ, 2, 0xC00000),
    ), direct=True), Case("vdp_fifo", (
        Operation(WRITE, 2, 0xC00004, 0x8154),
        Operation(WRITE, 2, 0xC00004, 0x8C01),
        Operation(WRITE, 2, 0xC00004, 0x8F02),
        Operation(WRITE, 4, 0xC00004, 0x40000000),
        *(Operation(WRITE, 2, 0xC00000, 0x1234 + i) for i in range(16)),
    )), _memory_dma_case("dma_memory_vram", 1, 0),
    _memory_dma_case("dma_memory_cram", 3, 8),
    _memory_dma_case("dma_memory_vsram", 5, 4), Case("dma_fill", (
        _vdp_reg(1, 0x14),
        _vdp_reg(15, 1),
        _vdp_reg(19, 3),
        _vdp_reg(20, 0),
        _vdp_reg(23, 0x80),
        Operation(WRITE, 4, 0xC00004, _vdp_command(0x21, 0x100)),
        Operation(WRITE, 2, 0xC00000, 0xA500),
        *(Operation(READ, 2, 0xE00000) for _ in range(20)),
        Operation(WRITE, 4, 0xC00004, _vdp_command(0, 0x100)),
        Operation(READ, 2, 0xC00000),
        Operation(READ, 2, 0xC00000),
        Operation(READ, 2, 0xC00000),
    )), Case("dma_copy", (
        _vdp_reg(1, 0x14),
        _vdp_reg(15, 1),
        Operation(WRITE, 4, 0xC00004, _vdp_command(1, 0)),
        Operation(WRITE, 2, 0xC00000, 0xA55A),
        _vdp_reg(19, 2),
        _vdp_reg(20, 0),
        _vdp_reg(21, 0),
        _vdp_reg(22, 0),
        _vdp_reg(23, 0xC0),
        Operation(WRITE, 4, 0xC00004, _vdp_command(0x21, 4)),
        *(Operation(READ, 2, 0xE00000) for _ in range(8)),
        Operation(WRITE, 4, 0xC00004, _vdp_command(0, 4)),
        Operation(READ, 2, 0xC00000),
    )), _memory_dma_case("dma_active_vram_h32", 1, 0,
                          display=0x54, words=4),
    _memory_dma_case("dma_active_vram_h40", 1, 0,
                     display=0x54, h40=True, words=4), Case("render_plane_a", (
        Operation(POLL, 2, 0xC00004, 0x0008, compare_mask=0x0008),
        _vdp_reg(0, 0x04),
        _vdp_reg(1, 0x44),
        _vdp_reg(2, 0x10),
        _vdp_reg(7, 0),
        _vdp_reg(11, 0),
        _vdp_reg(12, 0),
        _vdp_reg(13, 0),
        _vdp_reg(15, 2),
        _vdp_reg(16, 0),
        Operation(WRITE, 4, 0xC00004, _vdp_command(1, 0)),
        *(op for _row in range(8) for op in (
            Operation(WRITE, 2, 0xC00000, 0x1111),
            Operation(WRITE, 2, 0xC00000, 0x1111),
        )),
        Operation(WRITE, 4, 0xC00004, _vdp_command(1, 0x4000)),
        Operation(WRITE, 2, 0xC00000, 0),
        Operation(WRITE, 4, 0xC00004, _vdp_command(3, 2)),
        Operation(WRITE, 2, 0xC00000, 0x000E),
        Operation(WRITE, 4, 0xC00004, _vdp_command(0, 0)),
        Operation(READ, 2, 0xC00000),
        Operation(WRITE, 4, 0xC00004, _vdp_command(0, 0x4000)),
        Operation(READ, 2, 0xC00000),
    ), render=True), _render_scene("render_plane_b", {1: 2}, plane_b_row=[1]),
    _render_scene("render_tile_map", {0: 1, 1: 2, 2: 3, 3: 4},
                  plane_a_rows={0: [0, 1, 2, 3]}),
    _render_scene("render_scroll_horizontal", {0: 1, 1: 2, 2: 3},
                  plane_a_rows={0: [0, 1, 2, 0]}, hscroll=(8, 0)),
    _render_scene("render_scroll_vertical", {0: 1, 1: 2},
                  plane_a_rows={1: [1] * 32}, vsram=(8, 0)),
    _render_scene("render_window", {0: 1, 1: 3}, window_row=[1, 1], window_h=1),
    _render_scene("render_h40", {0: 1}, h40=True, plane_size=1),
    _render_scene("render_h32_v30", {0: 1}, v30=True, region="E"),
    _render_scene("render_h40_v30", {0: 1}, h40=True, v30=True,
                  plane_size=1, region="E")]
