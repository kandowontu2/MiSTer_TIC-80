"""The physical-input recorder must reject incomplete or corrupt evidence."""
from pathlib import Path
import struct
import sys
import unittest
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_physical_input import parse_pmem


def complete_words():
    w = [0] * 256
    w[0] = 1
    w[1] = w[14] = 255
    w[3] = 7
    w[5] = w[6] = w[10] = w[11] = 1
    w[12] = 120
    w[13] = 500
    for i in (*range(20, 28), *range(30, 38)):
        w[i] = 1
    return w


def encode(w):
    payload = struct.pack('<256I', *w)
    return b'TMPM' + struct.pack('<II', 1, zlib.crc32(payload)) + payload


class PhysicalOracle(unittest.TestCase):
    def test_complete_single_boot_and_release(self):
        self.assertTrue(parse_pmem(encode(complete_words()))['passed'])

    def test_each_missing_control_or_release_is_incomplete(self):
        for index in (1, 3, 5, 6, 10, 11, 12, 14, *range(20, 28), *range(30, 38)):
            w = complete_words(); w[index] = 0
            self.assertFalse(parse_pmem(encode(w))['passed'], index)
        for index in (2, 4, 15):
            w = complete_words(); w[index] = 1
            self.assertFalse(parse_pmem(encode(w))['passed'], index)
        w = complete_words(); w[12] = 119
        self.assertFalse(parse_pmem(encode(w))['passed'])

    def test_corruption_truncation_version_and_restart_reject(self):
        data = encode(complete_words())
        for broken in (data[:-1], data + b'\0', data[:-1] + b'\xff',
                       b'BAD!' + data[4:], data[:4] + struct.pack('<I', 2) + data[8:]):
            with self.assertRaises(AssertionError): parse_pmem(broken)
        for boots in (0, 2):
            w = complete_words(); w[0] = boots
            with self.assertRaises(AssertionError): parse_pmem(encode(w))


if __name__ == '__main__': unittest.main()
