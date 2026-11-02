#!/usr/bin/env python3
"""Tests for the candidate reference trace envelope and clock epochs."""
import io
import struct
import unittest

from oracle import trace


class TraceCheck(unittest.TestCase):
    def test_round_trip_and_independent_device_clocks(self):
        events = [trace.Event(100, trace.M68K, trace.READ, 2, 0xC00004, 0x1234),
                  trace.Event(90, trace.Z80, trace.WRITE, 1, 0x4000, 0x56),
                  trace.Event(103, trace.M68K, trace.FETCH, 2, 0x200, 0x60FE)]
        output = io.BytesIO()
        trace.write(output, events)
        self.assertEqual(list(trace.read(io.BytesIO(output.getvalue()))), events)

    def test_streaming_writer(self):
        output = io.BytesIO()
        writer = trace.Writer(output)
        writer.emit(trace.Event(10, trace.M68K, trace.FETCH, 2, 0x200, 0x60FE))
        writer.emit(trace.Event(11, trace.Z80, trace.READ, 1, 0x8000, 0x7F))
        writer.close()
        self.assertEqual(len(list(trace.read(io.BytesIO(output.getvalue())))), 2)

    def test_epoch_across_adjustment(self):
        epoch = trace.Epoch()
        before = epoch.clock(1000)
        epoch.adjust(900)
        self.assertEqual((before, epoch.clock(105)), (1000, 1005))
        with self.assertRaises(ValueError):
            epoch.adjust(-1)

    def test_invalid_version_and_truncation(self):
        stream = io.BytesIO()
        trace.write(stream, [trace.Event(1, trace.M68K, trace.READ, 1, 0, 1),
                             trace.Event(2, trace.M68K, trace.READ, 1, 1, 2)])
        data = stream.getvalue()
        for broken in (b"", data[:7], data[:-1], data[:-trace.RECORD.size],
                       data + b"x", data[:4] + struct.pack("<H", 99) + data[6:]):
            with self.subTest(broken=broken), self.assertRaises(ValueError):
                list(trace.read(io.BytesIO(broken)))

    def test_invalid_record_and_backward_clock(self):
        event = trace.Event(1, trace.M68K, trace.READ, 1, 0, 1)
        for events in ([event, trace.Event(0, trace.M68K, trace.READ, 1, 0, 1)],
                       [trace.Event(1, 255, trace.READ, 1, 0, 1)],
                       [trace.Event(1, trace.M68K, trace.READ, 3, 0, 1)]):
            with self.subTest(events=events), self.assertRaises(ValueError):
                trace.write(io.BytesIO(), events)

    def test_reader_rejects_invalid_flags_and_clock(self):
        stream = io.BytesIO()
        trace.write(stream, [trace.Event(5, trace.M68K, trace.READ, 1, 0, 1),
                             trace.Event(6, trace.M68K, trace.READ, 1, 1, 2)])
        data = bytearray(stream.getvalue())
        flags = bytearray(data)
        flags[trace.HEADER.size + 11] = 1
        backward = bytearray(data)
        struct.pack_into("<Q", backward, trace.HEADER.size + trace.RECORD.size, 4)
        for broken in (flags, backward):
            with self.assertRaises(ValueError):
                list(trace.read(io.BytesIO(broken)))

    def test_invalid_footer_is_rejected_before_any_event_is_exposed(self):
        stream = io.BytesIO()
        trace.write(stream, [trace.Event(1, trace.M68K, trace.READ, 1, 0, 1)])
        damaged = stream.getvalue()[:-1]
        with self.assertRaises(ValueError):
            next(trace.read(io.BytesIO(damaged)))


if __name__ == "__main__":
    unittest.main()
