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
    ))]
