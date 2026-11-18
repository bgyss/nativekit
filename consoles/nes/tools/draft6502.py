"""Draft one branch-terminated basic block from a known MMC1 16 KiB PRG mapping.

Supply the physical bank and the mapped CPU address from a trace. Register the
result under that bank in the title adapter. Cycle timing needs separate checks.
"""
import argparse
from pathlib import Path


def draft(image, bank, pc):
    if len(image) < 16 or image[:4] != b"NES\x1a" or image[7] & 0x0c == 8:
        raise ValueError("expected an iNES 1.0 cartridge")
    if (image[6] >> 4 | image[7] & 0xf0) != 1:
        raise ValueError("expected mapper 1")
    start = 16 + (512 if image[6] & 4 else 0)
    if not 0 <= bank < image[4] or len(image) < start + image[4] * 16384:
        raise ValueError("missing PRG bank")
    if not 0x8000 <= pc <= 0xffff:
        raise ValueError("entry must be in a PRG window")
    data = image[start + bank * 16384:start + (bank + 1) * 16384]
    offset = pc & 0x3fff
    lines = ['#include "nnes/machine.h"', '',
             f'nnes_result draft_bank{bank:02x}_{pc:04x}(nnes *console, void *user)',
             '{', '    (void)user;', '    nnes_cpu_state *cpu = nnes_cpu(console);',
             '    if (!cpu) return NNES_INVALID_ARGUMENT;']
    while offset + 2 <= len(data):
        opcode, operand = data[offset:offset + 2]
        pc += 2
        offset += 2
        if pc > 0xffff:
            raise ValueError("block crosses CPU address boundary")
        if opcode == 0xa2:
            lines.extend([f'    cpu->x = 0x{operand:02x};', '    cpu->p = nnes_nz8(cpu->p, cpu->x);'])
        elif opcode == 0xa5:
            lines.extend([f'    cpu->pc = 0x{pc:04x};', '    {',
                          f'        nnes_result rc = nnes_rd8_zp(console, 0x{operand:02x}, &cpu->a);',
                          '        if (rc != NNES_OK) return rc;', '    }',
                          '    cpu->p = nnes_nz8(cpu->p, cpu->a);'])
        elif opcode == 0xc9:
            lines.append(f'    cpu->p = nnes_cmp8(cpu->p, cpu->a, 0x{operand:02x});')
        elif opcode in (0xf0, 0xd0):
            branch = 'BEQ' if opcode == 0xf0 else 'BNE'
            lines.extend(['    int taken;', f'    nnes_result rc = nnes_branch_relative(console, NNES_BRANCH_{branch},',
                          f'                                          0x{pc:04x}, 0x{operand:02x}, &taken);',
                          '    if (rc != NNES_OK) return rc;', '    return nnes_jump_routine(console, cpu->pc);', '}'])
            return '\n'.join(lines)
        else:
            raise ValueError(f"unsupported opcode ${opcode:02X} at ${pc - 2:04X}")
    raise ValueError("block crosses the physical PRG bank boundary")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--bank", required=True, type=lambda s: int(s, 0))
    parser.add_argument("--pc", required=True, type=lambda s: int(s, 0))
    args = parser.parse_args()
    try:
        print(draft(args.image.read_bytes(), args.bank, args.pc))
    except (OSError, ValueError) as error:
        parser.exit(1, f"draft6502: {error}\n")
