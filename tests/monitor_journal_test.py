"""Retain partial appends and enforce terminal status without restarting work."""
import json
from pathlib import Path
import re
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from monitor_journal import MonitorJournal
from hardware_ssh import read_with_reconnect


class Journal(unittest.TestCase):
    def test_lost_launch_reply_is_never_retried_as_a_read(self):
        starts, reads = [], []
        def start(text):
            starts.append(text)
            raise ConnectionResetError('launch may have executed')
        def observe(text):
            reads.append(text)
            return ''
        with self.assertRaises(ConnectionResetError):
            MonitorJournal(start, 10, 197, read_command=observe)
        self.assertEqual(len(starts), 1)
        self.assertEqual(reads, [])

    def test_reconnect_keeps_original_monitor_and_byte_offset(self):
        starts, reads, connections = [], [], []
        first = json.dumps(dict(elapsed_ns=0)) + '\n'
        last = json.dumps(dict(elapsed_ns=10_000_000_000)) + '\n'
        def start(text):
            starts.append(text)
            return ''
        def remote_read(text):
            reads.append(text)
            if len(reads) == 1:
                return '__TM_MONITOR_EXIT__pending\n' + first
            if len(reads) == 2:
                raise ConnectionResetError('lost SSH')
            return '__TM_MONITOR_EXIT__0\n' + last
        def reconnect():
            connections.append('connect')
            if len(connections) < 3:
                raise TimeoutError('still unavailable')
        def observe(text):
            return read_with_reconnect(lambda: remote_read(text), reconnect)
        journal = MonitorJournal(start, 10, 197, read_command=observe)
        with patch('monitor_journal.time.sleep'), patch('hardware_ssh.time.sleep'):
            rows = list(journal.rows())
        self.assertEqual([row['elapsed_ns'] for row in rows], [0, 10_000_000_000])
        self.assertEqual(len(starts), 1)
        self.assertEqual(len(connections), 3)
        self.assertEqual(reads[1], reads[2])
        self.assertIn(f'tail -c +{len(first.encode()) + 1}', reads[2])
        self.assertTrue(all(journal.directory in text for text in starts + reads))

    def test_partial_append_is_not_lost_or_duplicated(self):
        first = json.dumps(dict(elapsed_ns=0)) + '\n'
        second = json.dumps(dict(elapsed_ns=100)) + '\n'
        third = json.dumps(dict(elapsed_ns=200)) + '\n'
        responses = iter(('__TM_MONITOR_EXIT__pending\n' + first + '{"elapsed_ns":1',
                          '__TM_MONITOR_EXIT__0\n' + second + third))
        calls = []
        def command(text):
            calls.append(text)
            return '' if text.startswith('set -e;') else next(responses)
        journal = MonitorJournal(command, 10, 197)
        with patch('monitor_journal.time.sleep'):
            rows = list(journal.rows())
        self.assertEqual([row['elapsed_ns'] for row in rows], [0, 100, 200])
        self.assertEqual(len([text for text in calls if text.startswith('set -e;')]), 1)
        positions = [int(re.search(r'tail -c \+(\d+)', text).group(1)) for text in calls[1:]]
        self.assertEqual(positions, [1, len(first.encode()) + 1])

    def test_failed_monitor_is_not_a_completed_run(self):
        def command(text):
            if text.startswith('set -e;'): return ''
            if text.startswith('cat '): return 'Session changed'
            return '__TM_MONITOR_EXIT__1\n'
        journal = MonitorJournal(command, 10, 197)
        with self.assertRaisesRegex(AssertionError, 'Session changed'):
            list(journal.rows())

    def test_completion_is_read_before_final_samples(self):
        calls = []
        def command(text):
            calls.append(text)
            if text.startswith('set -e;'): return ''
            return '__TM_MONITOR_EXIT__0\n' + json.dumps(dict(elapsed_ns=10_000_000_000)) + '\n'
        journal = MonitorJournal(command, 10, 197)
        self.assertEqual(list(journal.rows()), [dict(elapsed_ns=10_000_000_000)])
        read = calls[-1]
        self.assertLess(read.index('cat '), read.index('tail -c'))


if __name__ == '__main__': unittest.main()
