from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_studio_native import PROGRAM, classify_studio_processes


class Processes(unittest.TestCase):
    def fixture(self):
        return {'10': [PROGRAM, '--folder', '/media/fat/games/TIC-80/Carts'],
                '11': ['tic80-studio', '--studio-worker', '10']}

    def test_normal_parent_and_worker_with_optional_wheel_child(self):
        p = self.fixture()
        self.assertEqual(classify_studio_processes(p), ('10', '11', []))
        p['12'] = ['tic80-hid-wheel', '--hid-wheel-worker', '/dev', '10']
        self.assertEqual(classify_studio_processes(p), ('10', '11', ['12']))

    def test_unknown_worker_or_wrong_parent_is_rejected(self):
        for argv in (['tic80-studio', '--studio-worker', '99'],
                     ['tic80-hid-wheel', '--hid-wheel-worker', '/dev', '99'],
                     ['unexpected-helper']):
            p = self.fixture(); p['12'] = argv
            with self.assertRaises(AssertionError): classify_studio_processes(p)

    def test_duplicate_or_missing_primary_process_is_rejected(self):
        p = self.fixture(); p['13'] = p['10']
        with self.assertRaises(AssertionError): classify_studio_processes(p)
        p = self.fixture(); del p['11']
        with self.assertRaises(AssertionError): classify_studio_processes(p)


if __name__ == '__main__': unittest.main()
