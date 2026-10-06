"""Observe human controls from cartridge pmem without injecting events.

The keyboard counters here cover controller-emulated WASD/Enter/Esc/Q/E.
They do not qualify a physical keyboard's complete key/modifier interface.
"""
import json
import struct
import time
import zlib


def pmem_words(data):
    assert len(data) == 1036 and data[:4] == b'TMPM'
    assert struct.unpack_from('<I', data, 4)[0] == 1
    assert struct.unpack_from('<I', data, 8)[0] == zlib.crc32(data[12:])
    w = struct.unpack_from('<256I', data, 12)
    assert w[0] == 1, 'Fresh input diagnostic restarted'
    return w


def parse_pmem(data):
    w = pmem_words(data)
    return dict(
        boots=w[0], gamepad_seen=w[1], gamepad_held=w[2],
        mouse_seen=w[3], mouse_held=w[4], wheel_up=w[5], wheel_down=w[6],
        x_changes=w[10], y_changes=w[11], released_ticks=w[12], ticks=w[13],
        keyboard_seen=w[14], keyboard_held=w[15],
        gamepad_press_counts=list(w[20:28]), keyboard_press_counts=list(w[30:38]),
        passed=(w[1] == w[14] == 255 and w[3] == 7 and w[5] > 0 and w[6] > 0
                and w[10] > 0 and w[11] > 0 and w[12] >= 120
                and w[2] == w[4] == w[15] == 0
                and all(w[i] > 0 for i in (*range(20, 28), *range(30, 38)))))


def parse_keyboard_pmem(data):
    w = pmem_words(data)
    presses, releases, holds = list(w[20:42]), list(w[60:82]), list(w[140:162])
    return dict(boots=w[0], keyboard_seen=w[1], keyboard_held=w[2],
                gamepad_seen=w[3], gamepad_held=w[4], released_ticks=w[5], ticks=w[6],
                chords_seen=w[7], W_repeat_count=w[8], key_press_counts=presses,
                key_release_counts=releases, max_hold_ticks=holds,
                passed=(w[1] == (1 << 22) - 1 and w[2] == 0 and w[3] == 255 and w[4] == 0
                        and w[5] >= 120 and w[7] == 7 and w[8] >= 3 and holds[8] >= 60
                        and all(x > 0 for x in presses) and all(x > 0 for x in releases)))


def read_words(test, path, keyboard=False):
    def read():
        with test.client.open_sftp() as sftp:
            with sftp.open(path, 'rb') as stream:
                return stream.read(1037)
    return (parse_keyboard_pmem if keyboard else parse_pmem)(test.read_operation(read))


def run_physical(test, args, save_path, identity, main_pid, mgl, frontend):
    assert frontend == 'player'
    keyboard = bool(args.physical_keyboard)
    deadline = time.monotonic() + args.soak_seconds
    observations = []
    while True:
        assert test.core() == 'TIC-80' and test.main_pid() == main_pid
        assert mgl in test.argv(main_pid)
        assert test.frontend_identity(identity['pid'], identity['sha256']) == identity
        try:
            row = read_words(test, save_path, keyboard=keyboard)
        except FileNotFoundError:
            row = None
        if row:
            if observations:
                assert row['ticks'] >= observations[-1]['ticks']
            observations.append(row)
            (args.evidence / ('physical-keyboard-observations.json' if keyboard else 'physical-observations.json')).write_text(
                json.dumps(dict(input_injected=False, observations=observations), indent=2)
                + '\n', encoding='utf-8')
            print('PHYSICAL ' + json.dumps(row), flush=True)
            if row['passed']:
                return dict(input_injected=False, physical_input_passed=True,
                            kind='keyboard' if keyboard else 'controller-mouse', final=row)
        assert time.monotonic() < deadline, 'Human-operated input check remains incomplete'
        time.sleep(3)
