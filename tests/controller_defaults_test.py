import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('defaults', Path(__file__).resolve().parents[1]/'tools/controller_defaults.py')
defaults = importlib.util.module_from_spec(spec)
spec.loader.exec_module(defaults)


class ControllerDefaults(unittest.TestCase):
    def test_xbox_and_preserved_bindings(self):
        system = [0]*32
        system[:12] = [0x321,0x320,0x323,0x322,0x130,0x131,0x133,0x134,
                       0x136,0x137,0x13a,0x13b]
        existing = system.copy()
        existing[0] |= 769<<16
        existing[1] |= 768<<16
        existing[2] |= 771<<16
        existing[3] |= 770<<16
        existing[4] = 0x134  # user face-button customization
        existing[21] = 0x13c  # OSD binding
        pack = lambda data: struct.pack('<32I', *data)
        result = defaults.decode(defaults.create(pack(system),pack(existing)))
        self.assertEqual(result[:4],system[:4])
        self.assertEqual(result[4:8],existing[4:8])
        self.assertEqual(result[8:16],[770,768,771,769,0x13b,0x13a,0x136,0x137])
        self.assertEqual(result[16:],existing[16:])
        self.assertEqual(defaults.create(pack(system),pack(result)),pack(result))

    def test_custom_axes_and_new_map(self):
        system = [0]*32; system[10:12] = [0x13a,0x13b]
        result = defaults.decode(defaults.create(struct.pack('<32I',*system),axes=(3,4)))
        self.assertEqual(result[8:14],[776,774,777,775,0x13b,0x13a])
        with self.assertRaises(ValueError): defaults.create(b'')
        with self.assertRaises(ValueError): defaults.create(struct.pack('<32I',*system),axes=(0,0))


if __name__ == '__main__': unittest.main()
