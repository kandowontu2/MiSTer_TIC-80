"""Compare exact stock-Main cartridge delivery and scaler RGB for both frontends."""
import hashlib
from io import BytesIO
import json
from pathlib import Path
import shlex
import time
from PIL import Image

LANGUAGES = 'lua js moon yue fennel scheme squirrel python wren janet wasm ruby miniscript forth'.split()
FORMATS = [('native', '.tic'), ('png', '.png'), ('legacy', '-legacy.png')]
PNG_END = b'\x00\x00\x00\x00IEND\xaeB\x60\x82'


def compare_frame(data, references):
    assert data.endswith(PNG_END), 'Incomplete scaler capture'
    with Image.open(BytesIO(data)) as image:
        assert image.size == (256, 144), image.size
        actual = image.convert('RGB').tobytes()
    mismatches = []
    for rgba in references:
        assert len(rgba) == 256 * 144 * 4
        expected = bytes(channel for pixel in zip(rgba[0::4], rgba[1::4], rgba[2::4]) for channel in pixel)
        assert len(actual) == len(expected)
        mismatches.append(sum(a != b for a, b in zip(actual, expected)))
    assert mismatches and min(mismatches) == 0, ('Full scaler RGB mismatch', mismatches)
    return mismatches


def validate_delivery(data, expected, ticket, size, ack, source_length):
    assert ticket & 3 == 2 and ticket == ack and size == len(expected)
    assert source_length == 0, 'Stock Main unexpectedly supplied source metadata'
    assert data == expected, 'FPGA cartridge bytes differ from the selected file'


def run_matrix(test, args, stage, remote, label, identity, frontend, update):
    root = Path(__file__).resolve().parents[1]
    fixture = args.cartridge_matrix
    manifest = json.loads((fixture / 'manifest.json').read_text(encoding='utf-8'))
    assert manifest['desktop_completed'] and manifest['languages'] == LANGUAGES
    for name, entry in manifest['files'].items():
        assert hashlib.sha256((fixture / name).read_bytes()).hexdigest() == entry['sha256'], name
    reference = root / 'build/studio-runtime-reference-c6180035'
    rm = json.loads((reference / 'manifest.json').read_text(encoding='utf-8'))
    assert rm['complete'] and rm['fixture']['manifest_sha256'] == hashlib.sha256((fixture / 'manifest.json').read_bytes()).hexdigest()
    for name, entry in rm['files'].items():
        assert hashlib.sha256((reference / name).read_bytes()).hexdigest() == entry['sha256'], name
    for key, path in [('matrix_fixture_manifest_sha256', fixture / 'manifest.json'),
                      ('matrix_reference_manifest_sha256', reference / 'manifest.json')]:
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        assert key not in test.result or test.result[key] == digest
        test.result[key] = digest
    test.result['matrix_languages'] = LANGUAGES
    test.save()
    expected_mouse = rm['mouse']['raw_word']
    rows = []
    case_root = remote + '/' + label + '-matrix'
    test.dispatch('create-private-matrix:' + label, 'mkdir ' + case_root)
    with test.client.open_sftp() as sftp:
        for _, suffix in FORMATS:
            for language in LANGUAGES:
                sftp.put(str(fixture / 'carts' / (language + suffix)), case_root + '/' + language + suffix)

    def read_file(path, maximum):
        def read():
            with test.client.open_sftp() as sftp:
                with sftp.open(path, 'rb') as file: return file.read(maximum + 1)
        result = test.read_operation(read)
        assert len(result) <= maximum
        return result

    def words():
        return [int(v, 16) for v in test.run(
            'for a in 0x3A000070 0x3A000074 0x3A000038 0x3A04C204; do devmem "$a" 32 || exit 32; done').split()]

    log_path = remote + '/' + label + '-frontend.log'
    marker = 'Cartridge loaded:' if frontend == 'player' else 'MiSTer cartridge running:'
    deadline = time.monotonic() + 25
    while marker not in test.run('cat ' + log_path):
        assert test.frontend_identity(identity['pid'], identity['sha256']) == identity
        assert time.monotonic() < deadline, 'Original warmup cartridge did not start'
        time.sleep(.2)

    for format_, suffix in FORMATS:
        for language in LANGUAGES:
            name = label + '-' + format_ + '-' + language
            data = (fixture / 'carts' / (language + suffix)).read_bytes()
            path = case_root + '/' + language + suffix
            mgl_path = case_root + '/' + format_ + '-' + language + '.mgl'
            mgl = ('<mistergamedescription>\n <rbf>' + stage.removeprefix('/media/fat/') + '/TIC80</rbf>\n' +
                   f' <file delay="3" type="f" index="0" path="{path}"/>\n</mistergamedescription>\n')
            with test.client.open_sftp() as sftp:
                with sftp.open(mgl_path, 'wb') as file: file.write(mgl.encode())
            (args.evidence / (name + '.mgl')).write_text(mgl, encoding='utf-8')
            previous_loaded = test.run('cat ' + log_path).count(marker)
            update(mgl_path)
            test.load(mgl_path, 'TIC-80')
            deadline = time.monotonic() + 40
            while True:
                core = test.core()
                assert core in ('', 'TIC-80'), ('Another core selected', core)
                if core == 'TIC-80':
                    main = test.main_pid()
                    if mgl_path in test.argv(main):
                        ticket, size, ack, length = words()
                        if ticket & 3 == 2 and ticket == ack and size == len(data) and length == 0:
                            log = test.run('cat ' + log_path)
                            assert log.count(marker) <= previous_loaded + 1, 'Cartridge ran more than once'
                            if log.count(marker) == previous_loaded + 1: break
                assert time.monotonic() < deadline, 'Original Main transfer did not complete'
                time.sleep(.2)
            assert test.frontend_identity(identity['pid'], identity['sha256']) == identity
            (args.evidence / (name + '-frontend.log')).write_text(log, encoding='utf-8')
            snapshot = remote + '/' + name + '-payload'
            test.job(snapshot, 'taskset 1 nice -n 19 ' + stage + '/cart-snapshot ' + str(ticket) + ' ' + str(size) +
                     ' ' + snapshot + '.bin > ' + snapshot + '.json 2> ' + snapshot + '.stderr')
            assert test.collect(snapshot, 10) == 0, test.run('cat ' + snapshot + '.stderr')
            raw = read_file(snapshot + '.bin', 4 * 1024 * 1024)
            meta = json.loads(test.run('cat ' + snapshot + '.json'))
            validate_delivery(raw, data, *words())
            assert meta['ticket'] == ticket and meta['bytes'] == size and meta['read_only'] is True
            (args.evidence / (name + '-payload.bin')).write_bytes(raw)
            monitor = remote + '/' + name + '-clock'
            test.job(monitor, 'taskset 1 nice -n 19 ' + stage + '/monitor --seconds 2 --interval-ms 13 > ' +
                     monitor + '.jsonl 2> ' + monitor + '.stderr')
            assert test.collect(monitor, 10) == 0, test.run('cat ' + monitor + '.stderr')
            payload = test.run('cat ' + monitor + '.jsonl')
            (args.evidence / (name + '-clock.jsonl')).write_text(payload, encoding='utf-8')
            samples = [json.loads(line) for line in payload.splitlines()]
            assert len(samples) >= 100 and {s['session'] for s in samples} == {meta['session']}
            assert all(s['underruns'] == 0 for s in samples)
            assert ((samples[-1]['played'] - samples[0]['played']) & 0xffffffff) > 90000
            assert (((samples[-1]['presented'] >> 2) - (samples[0]['presented'] >> 2)) & 0x3fffffff) >= 90
            mouse_before = int(test.run('devmem 0x3A000068 32').strip(), 16)
            assert mouse_before == expected_mouse, 'Physical input moved; do not mask the reference cursor'
            # Main accepts a filename ending in .png without adding a timestamp.
            # A UUID prefix binds this capture to one request without modifying settings.
            image_name = 'tic80-' + remote.rsplit('-', 1)[1] + '-' + name + '.png'
            image_path = '/media/fat/screenshots/' + image_name
            request = remote + '/' + name + '-capture-request'
            test.job(request, 'test "$(cat /tmp/CORENAME)" = TIC-80 && test ! -e ' + image_path +
                     ' && printf %s ' + shlex.quote('screenshot ' + image_name + '\n') + ' > /dev/MiSTer_cmd')
            assert test.collect(request, 10) == 0
            deadline = time.monotonic() + 15
            while True:
                try:
                    captured = read_file(image_path, 1024 * 1024)
                    if captured.endswith(PNG_END): break
                except FileNotFoundError:
                    pass
                assert test.core() == 'TIC-80' and test.main_pid() == main
                assert time.monotonic() < deadline, 'Original scaler capture did not complete; request was not repeated'
                time.sleep(.2)
            actual_path = args.evidence / (name + '.png')
            actual_path.write_bytes(captured)
            goldens = fixture / 'goldens' if frontend == 'player' else reference
            mismatches = compare_frame(captured, [(goldens / f'{language}-{ticks}.rgba').read_bytes() for ticks in (30, 60)])
            assert int(test.run('devmem 0x3A000068 32').strip(), 16) == mouse_before
            assert words() == [ticket, size, ack, length] and mgl_path in test.argv(main)
            assert test.frontend_identity(identity['pid'], identity['sha256']) == identity
            capture_hash = hashlib.sha256(captured).hexdigest()
            # Remove only this UUID-named, newly created, hash-verified screenshot.
            cleanup = remote + '/' + name + '-capture-cleanup'
            test.job(cleanup, 'test "$(sha256sum ' + image_path + ' | cut -d " " -f 1)" = ' + capture_hash +
                     ' && rm -- ' + image_path)
            assert test.collect(cleanup, 10) == 0
            row = dict(frontend=frontend, language=language, format=format_, main_pid=main,
                       frontend_identity=identity, ticket=ticket, acknowledged_ticket=ack,
                       source_length=length, delivered_bytes=size, payload_sha256=hashlib.sha256(raw).hexdigest(),
                       screenshot_sha256=capture_hash, reference_pose_mismatches=mismatches,
                       audio_samples=len(samples), session=meta['session'], stationary_mouse_word=mouse_before,
                       original_capture_request=request, screenshot_removed=True, passed=True)
            rows.append(row)
            test.result['cycles'][-1]['matrix_cases'] = rows
            test.save()
            print(f'{frontend}/{format_}/{language}: exact FPGA payload, full scaler RGB, audio active', flush=True)
    return dict(passed=True, cases=len(rows), original_frontend_retained=True,
                last=dict(main_pid=main, cart=[ticket, size], mgl=mgl_path))
