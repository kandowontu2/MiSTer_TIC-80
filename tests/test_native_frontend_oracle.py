"""Offline controls for the native test oracle and targeted cleanup."""
import json
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
try:
    import paramiko
except ModuleNotFoundError as error:
    if error.name != 'paramiko': raise
    def no_network():
        raise AssertionError('An offline test attempted a real SSH connection')
    sys.modules['paramiko'] = types.SimpleNamespace(SSHClient=no_network, SSHException=ConnectionError)
from test_hid_frontends_native import PrivateTest, STAGES, idle_child, validate_rows, cartridge, schedule
from native_frontend_lifecycle import code, switch_payload, validate_retained_boot


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
    def test_replacement_must_not_boot_the_retained_cartridge(self):
        validate_retained_boot(5, 5)
        for observed in (4, 6, 0):
            with self.assertRaisesRegex(AssertionError, 'Retained cartridge executed BOOT'):
                validate_retained_boot(5, observed)

    def test_switch_cannot_accept_retained_cartridge_boot(self):
        original = cartridge('music-private-' + 'a'*32)
        identities = []
        for kind in ('native', 'modern', 'legacy'):
            native, payload, extension, saveid = switch_payload(original, kind)
            self.assertEqual(len(native), len(original))
            self.assertIn(saveid.encode(), code(native))
            self.assertNotIn(b'music-private-' + b'a'*32, code(native))
            self.assertEqual(extension, 'tic' if kind == 'native' else 'png')
            self.assertTrue(payload == native if kind == 'native' else payload.startswith(b'\x89PNG'))
            identities.append(saveid)
        self.assertEqual(len(set(identities)), 3)

    def test_lost_mutation_reply_never_reconnects_or_redispatches(self):
        test = PrivateTest(None, None, reconnect=lambda: self.fail('Mutation must not reconnect'))
        test.save = lambda: None
        with patch('test_hid_frontends_native.command', side_effect=ConnectionResetError('lost reply')) as command:
            with self.assertRaises(ConnectionResetError): test.dispatch('original-load', 'one mutation')
        command.assert_called_once_with(None, 'one mutation')
        self.assertEqual(len(test.result['dispatches']), 1)

    def test_read_disconnect_reconnects_without_dispatch(self):
        reconnects = []
        test = PrivateTest(None, None, reconnect=lambda: reconnects.append('connected'))
        test.save = lambda: None
        with patch('hardware_ssh.time.sleep'), patch('test_hid_frontends_native.command',
                side_effect=[ConnectionResetError('lost read'), 'original status']) as command:
            self.assertEqual(test.run('read original status'), 'original status')
        self.assertEqual(reconnects, ['connected'])
        self.assertEqual(command.call_count, 2)
        self.assertEqual(test.result['dispatches'], [])

    def test_repeated_cycles_have_unique_save_and_job_labels(self):
        self.assertEqual(schedule(['player','studio'],1),[('player','player'),('studio','studio')])
        selected = schedule(['player','studio'],16)
        self.assertEqual(len(selected),32)
        self.assertEqual(len({label for _,label in selected}),32)
        for cycles,frontends in ((0,['player']),(17,['player']),(1,[]),(1,['player','player']),(1,['unknown'])):
            with self.assertRaises(AssertionError): schedule(frontends,cycles)
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

    def test_editor_save_requires_the_actual_changed_code(self):
        original = cartridge('unique-save')
        expected = code(original) + b' '
        text = code(original)
        encoded = bytes([5]) + (len(text)+1).to_bytes(2,'little') + b'\0' + text + b' '
        self.assertEqual(code(encoded), expected)
        self.assertNotEqual(code(original), expected)
        compressed = zlib.compress(text + b' ')
        zipped = bytes([16]) + len(compressed).to_bytes(2,'little') + b'\0' + compressed
        self.assertEqual(code(zipped), expected)
        for corrupt in (original[:3], original[:-1], b'\x05\xff\xff\0x'):
            with self.assertRaises(AssertionError): code(corrupt)

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
