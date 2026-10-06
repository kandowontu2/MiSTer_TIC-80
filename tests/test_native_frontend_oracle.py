"""Offline controls for the native test oracle and targeted cleanup."""
import json
from pathlib import Path
import sys
import types
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
try:
    import paramiko
except ModuleNotFoundError as error:
    if error.name != 'paramiko': raise
    def no_network():
        raise AssertionError('An offline test attempted a real SSH connection')
    sys.modules['paramiko'] = types.SimpleNamespace(SSHClient=no_network)
from test_hid_frontends_native import PrivateTest, STAGES, idle_child, validate_rows, cartridge


def receipt():
    rows = [dict(main_pid=42, event=f'/dev/input/event{n}', raw=f'/dev/hidraw{n}',
                 main_fd_verified=True, evdev_grab_busy=True,
                 only_main_and_probe_evdev_fds=True) for n in range(5)]
    rows += [dict(stage=stage, pan=pan, boot=1, repeat_mismatches=0, ticks=180+n*60)
             for n, (stage, pan) in enumerate(STAGES)]
    rows += [dict(osd_open=True, epoch=4095), dict(osd_open=False, epoch=0)]
    rows += [dict(native_frontend_passed=True, pan=-5, main_pid=42,
                  frontend_pid=43, blocked_feature_gets=2)]
    return rows


class NativeOracle(unittest.TestCase):
    def check(self, rows):
        return validate_rows('\n'.join(map(json.dumps, rows)), '42', '43')

    def test_valid_and_epoch_wrap(self):
        self.check(receipt())

    def test_negative_controls(self):
        changes = [(0,'main_pid',41), (0,'main_fd_verified',False),
                   (0,'evdev_grab_busy',False), (1,'event','/dev/input/event0'),
                   (5,'pan',1), (5,'boot',2), (5,'repeat_mismatches',1),
                   (6,'ticks',180), (14,'osd_open',False), (15,'epoch',1),
                   (16,'frontend_pid',44), (16,'blocked_feature_gets',0)]
        for index, field, value in changes:
            with self.subTest(field=field, index=index):
                rows = receipt(); rows[index][field] = value
                with self.assertRaises(AssertionError): self.check(rows)
        for rows in (receipt()[:-1], receipt()+[{}]):
            with self.assertRaises((AssertionError,KeyError)): self.check(rows)

    def test_only_normal_sleep_is_idle(self):
        self.assertTrue(idle_child([]))
        self.assertTrue(idle_child(['/bin/sleep','1']))
        for argv in (['/bin/sleep','10'], ['/bin/sh','_handler.sh'], ['sleep'], ['sh','sleep','1']):
            self.assertFalse(idle_child(argv))

    def test_reused_pid_never_signalled(self):
        test = PrivateTest(None, None)
        test.frontend_identity = lambda pid, digest: dict(pid=pid, birth='new', sha256=digest)
        test.dispatch = lambda *args: self.fail('Signal must not be dispatched')
        with self.assertRaises(AssertionError):
            test.stop_frontend(dict(pid='43',birth='old',sha256='hash'), 'cleanup')

    def test_signal_is_bound_to_birth(self):
        test = PrivateTest(None, None)
        identity = dict(pid='43',birth='123',sha256='hash')
        test.frontend_identity = lambda *args: identity
        calls = []; test.dispatch = lambda *args: calls.append(args)
        test.stop_frontend(identity, 'cleanup')
        self.assertIn('/proc/43/stat', calls[0][1])
        self.assertIn('= 123 && kill -TERM 43', calls[0][1])

    def test_real_api_observation_cartridge(self):
        data = cartridge('unique-save')
        self.assertEqual(data[0],5)
        self.assertEqual(int.from_bytes(data[1:3],'little'),len(data)-4)
        self.assertIn(b'-- saveid: unique-save', data)
        self.assertEqual(data.count(b'=mouse()'),2)

    def test_terminal_journal_published_during_liveness_read(self):
        test = PrivateTest(None, None)
        replies = iter(['', '43', '', '0'])
        commands = []; test.save = lambda: None
        def run(command):
            commands.append(command); return next(replies)
        test.run = run
        self.assertEqual(test.collect('/tmp/original-job', .1),0)
        self.assertTrue(test.result['original_jobs']['/tmp/original-job']['exit_race_reconciled'])
        self.assertFalse(any('nohup' in command or 'kill -TERM' in command for command in commands))

    def test_genuinely_missing_original_journal_fails_without_restart(self):
        test = PrivateTest(None, None)
        replies = iter(['', '43', '', '']); test.save = lambda: None
        test.run = lambda command: next(replies)
        with self.assertRaisesRegex(AssertionError,'without terminal journal'):
            test.collect('/tmp/original-job', .1)


if __name__ == '__main__': unittest.main()
