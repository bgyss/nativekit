"""Authored mapper-0 cartridge: NMI counter, serial pad reads, palette and tone."""
from pathlib import Path


def write_rom(path: Path):
    prg = bytearray([0xEA] * 16384)
    code = bytearray()

    def emit(*values):
        code.extend(values)

    def store(address, value):
        emit(0xA9, value, 0x8D, address & 255, address >> 8)

    emit(0x78, 0xD8, 0xA2, 0xFF, 0x9A)  # SEI; CLD; LDX #$ff; TXS
    store(0x2000, 0)
    store(0x2001, 0)
    store(0x4017, 0x40)
    # Wait for two vblanks before enabling rendering.
    for _ in range(2):
        emit(0x2C, 0x02, 0x20, 0x10, 0xFB)  # BIT $2002; BPL -5
    store(0x2006, 0x3F)
    store(0x2006, 0)
    store(0x2007, 0x21)
    store(0x4015, 1)
    store(0x4000, 0xBF)
    store(0x4002, 0x80)
    store(0x4003, 8)
    store(0x2000, 0x80)
    store(0x2001, 8)
    spin = 0x8000 + len(code)
    emit(0x4C, spin & 255, spin >> 8)
    prg[:len(code)] = code
    nmi = bytearray.fromhex(
        "48 8A 48 A9 01 8D 16 40 A9 00 8D 16 40 A2 08 "
        "AD 16 40 4A 26 20 AD 17 40 4A 26 21 CA D0 F1 E6 10 68 AA 68 40")
    prg[0x100:0x100 + len(nmi)] = nmi
    prg[0x3FFA:0x4000] = bytes.fromhex("00 81 00 80 00 81")
    path.write_bytes(b"NES\x1a" + bytes([1, 1]) + bytes(10) + prg + bytes(8192))


if __name__ == "__main__":
    import sys
    write_rom(Path(sys.argv[1]))
