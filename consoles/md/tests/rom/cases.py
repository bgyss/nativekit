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
        _vdp_reg(12, int(h40)),
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
                     display=0x54, h40=True, words=4)]
