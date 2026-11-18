"""Bank identity, entry decoding and refusal controls for the basic-block drafter."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from draft6502 import draft


class DraftTest(unittest.TestCase):
    def setUp(self):
        header = b"NES\x1a" + bytes([2, 0, 0x14, 0]) + bytes(8)
        self.image = bytearray(header + bytes(512 + 32768))
        self.image[-16384:-16376] = bytes.fromhex("a2 03 a5 04 c9 05 d0 f8")

    def test_selected_bank_after_trainer(self):
        result = draft(self.image, 1, 0x8000)
        self.assertIn("cpu->x = 0x03", result)
        self.assertIn("nnes_rd8_zp(console, 0x04", result)
        self.assertIn("0x8008, 0xf8", result)

    def test_indexed_store_loop(self):
        offset = len(self.image) - 16384
        self.image[offset:offset + 10] = bytes.fromhex("a9 06 9d 00 02 e8 e0 08 d0 f8")
        result = draft(self.image, 1, 0x8000)
        self.assertIn("nnes_wr8_absolute_indexed(console, 0x0200", result)
        self.assertIn("if (taken) goto block_8002", result)
        self.assertIn("nnes_dummy_next_pc(console)", result)

    def test_unknown_opcode(self):
        with self.assertRaisesRegex(ValueError, "unsupported opcode"):
            draft(self.image, 0, 0x8000)

    def test_cross_bank_operand(self):
        with self.assertRaisesRegex(ValueError, "boundary"):
            draft(self.image, 1, 0xbfff)


if __name__ == "__main__":
    unittest.main()
