"""Genuine stock-Main reloads and Studio working-copy save in private folders."""
import hashlib
import json
from pathlib import Path
import struct
import shlex
import re
import sys
import time
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
from service_png_test import modern, legacy


def pmem(test, path):
    def read():
        with test.client.open_sftp() as sftp:
            with sftp.open(path, 'rb') as stream: return stream.read(1037)
    data = test.read_operation(read)
    assert len(data) == 1036 and data[:4] == b'TMPM'
    assert struct.unpack_from('<II', data, 4) == (1, zlib.crc32(data[12:]))
    return struct.unpack_from('<256I', data, 12)


def code(data):
    banks = {}; position = 0
    while position < len(data):
        assert len(data) - position >= 4
        header = data[position]; size = int.from_bytes(data[position+1:position+3],'little')
        size = size or (65536 if header & 31 == 5 else 0)
        position += 4; assert position + size <= len(data)
        payload = data[position:position+size]; position += size
        if header & 31 == 5: banks[header >> 5] = payload
        elif header & 31 == 16: banks[0] = zlib.decompress(payload)
    return b''.join(banks[n] for n in sorted(banks,reverse=True)).rstrip(b'\0')


def switch_payload(cart, kind):
    # A full MGL core reload can briefly resume the retained cartridge before
    # Main's delayed file action. Give the new cart its own persistent identity
    # so that the retained cart's BOOT cannot satisfy the transfer observation.
    original = re.search(rb'^-- saveid: (\S+)$', code(cart), re.M).group(1)
    assert len(original) >= 32 and cart.count(original) == 1
    saveid = original[:-32] + hashlib.md5(original + kind.encode()).hexdigest().encode()
    native = cart.replace(original, saveid)
    if kind == 'native': return native, native, 'tic', saveid.decode()
    assert kind in ('modern', 'legacy')
    return native, (modern if kind == 'modern' else legacy)(native), 'png', saveid.decode()


def validate_retained_boot(before, after):
    assert after == before, ('Retained cartridge executed BOOT during delayed MGL replacement', before, after)


def run_lifecycle(test, args, stage, remote, label, cart, save_path, identity, frontend, update):
    rows = []
    original_cart = cart
    retained_ack = 0
    def wait_boot(boots):
        deadline = time.monotonic() + 25
        while True:
            assert test.core() == 'TIC-80'
            assert test.frontend_identity(identity['pid'], identity['sha256']) == identity
            try:
                values = pmem(test, save_path)
                assert values[2] <= boots, ('Unexpected repeated BOOT', values[:4], boots)
                if values[2] == boots and values[0] >= 120: return values
            except FileNotFoundError: pass
            assert time.monotonic() < deadline, 'Original frontend did not resume; it was not relaunched'
            time.sleep(.2)
    def observe(name, boots, path, size):
        nonlocal retained_ack
        values = wait_boot(boots)
        pid = test.main_pid(); assert path in test.argv(pid)
        words = test.read('for a in 0x3A000070 0x3A000074 0x3A000038 0x3A04C204; do devmem "$a" 32 || exit 32; done').split()
        ticket, received, ack, source_length = [int(word, 16) for word in words]
        assert source_length == 0
        cached_only = name.startswith('reload-') and (ticket, received) == (0,0)
        if cached_only:
            assert ack in (0,retained_ack), 'Unknown previous cartridge ACK after raw reload'
        else:
            assert ticket & 3 == 2 and ticket == ack and received == size
            if name.startswith('switch-'): assert ticket == ((retained_ack & ~3) + 4) | 2
            retained_ack = ack
        monitor = remote + '/' + label + '-' + name + '-clock'
        test.job(monitor, 'taskset 1 nice -n 19 ' + stage + '/monitor --seconds 2 --interval-ms 13 > ' + monitor + '.jsonl 2> ' + monitor + '.stderr')
        status = test.collect(monitor,10)
        payload = test.run('cat ' + monitor + '.jsonl')
        errors = test.run('cat ' + monitor + '.stderr')
        (args.evidence / (label + '-' + name + '-clock.jsonl')).write_text(payload)
        (args.evidence / (label + '-' + name + '-clock.stderr')).write_text(errors)
        assert status == 0, errors
        samples = [json.loads(line) for line in payload.splitlines()]
        assert len(samples) >= 100 and len({r['session'] for r in samples}) == 1
        assert all(row['underruns'] == 0 for row in samples)
        assert ((samples[-1]['played'] - samples[0]['played']) & 0xffffffff) > 90000
        row = dict(action=name, boots=values[2], ticks=values[0], main_pid=pid,
                   frontend_pid=identity['pid'], path=path, cart=[ticket, received], source_path='',
                   audio_samples=len(samples), session=samples[0]['session'],
                   cart_delivery='runtime-retained' if cached_only else 'stock-Main-transfer')
        rows.append(row); test.result['cycles'][-1]['lifecycle_actions'] = rows; test.save()
        print(f'{frontend}/{name}: BOOT {boots}, original frontend retained, audio active',flush=True)
        return row
    initial_mgl = remote + '/' + label + '.mgl'
    last = observe('cold',1,initial_mgl,len(cart))
    boots = 1
    for ordinal in range(args.lifecycle_reloads):
        path = stage + '/TIC80_20261003.rbf'; update(path)
        test.load(path,'TIC-80'); boots += 1
        last = observe('reload-' + str(ordinal),boots,path,len(cart))
    for kind in ('native', 'modern', 'legacy'):
        previous_save_path = save_path
        retained_boots = pmem(test, previous_save_path)[2]
        cart, data, extension, saveid = switch_payload(original_cart, kind)
        save_path = remote + '/saves/' + hashlib.md5(saveid.encode()).hexdigest() + '.pmem'
        path = remote + '/' + label + '-switch-' + kind
        # MGL's index is the menu slot F0. Main derives the extension ordinal
        # (0x00 TIC / 0x40 PNG) from the actual filename during file transfer.
        mgl = ('<mistergamedescription>\n <rbf>' + stage.removeprefix('/media/fat/') + '/TIC80</rbf>\n'
               f' <file delay="3" type="f" index="0" path="{path}.{extension}"/>\n</mistergamedescription>\n')
        with test.client.open_sftp() as sftp:
            with sftp.open(path + '.' + extension,'wb') as stream: stream.write(data)
            with sftp.open(path + '.mgl','wb') as stream: stream.write(mgl.encode())
        update(path + '.mgl'); test.load(path + '.mgl','TIC-80')
        last = observe('switch-' + kind,1,path + '.mgl',len(data))
        after = pmem(test, previous_save_path)[2]
        last.update(retained_boots_before=retained_boots, retained_boots_after=after)
        test.save()
        validate_retained_boot(retained_boots, after)
    if frontend == 'studio':
        # Stock Main supplied no source path. All edits and Ctrl+S writes stay
        # inside this test's private Studio directory.
        project = remote + '/projects/' + label + '/MiSTer cart.tic'
        prefix = remote + '/' + label + '-working-copy'
        test.job(prefix, stage + '/keyboard working-copy > ' + prefix + '.log 2>&1')
        assert test.collect(prefix,15) == 0, test.run('cat ' + prefix + '.log')
        deadline = time.monotonic() + 10
        while not test.run('test -f ' + shlex.quote(project) + ' && echo saved || true').strip():
            assert time.monotonic() < deadline, 'Studio did not publish a working copy'
            time.sleep(.2)
        with test.client.open_sftp() as sftp:
            with sftp.open(project,'rb') as stream: saved = stream.read(4*1024*1024+1)
        assert 4 <= len(saved) <= 4*1024*1024 and saved != cart
        assert code(saved) == code(cart) + b' ', 'Working-copy Save did not contain the requested editor change'
        with test.client.open_sftp() as sftp:
            with sftp.open(remote + '/' + label + '.tic','rb') as stream: original = stream.read()
        assert original == original_cart, 'Working-copy Save touched the original OSD cartridge'
        (args.evidence / 'studio-working-copy.tic').write_bytes(saved)
        rows.append(dict(action='working-copy-save',source_path='',original_unchanged=True,
                         saved_sha256=hashlib.sha256(saved).hexdigest(),saved_bytes=len(saved)))
        destination = remote + '/projects/' + label + '/validation.tic'
        with test.client.open_sftp() as sftp:
            with sftp.open(destination,'wb') as stream: stream.write(saved)
        prefix = remote + '/' + label + '-load-save-copy'
        test.job(prefix, stage + '/keyboard load-copy > ' + prefix + '.log 2>&1')
        assert test.collect(prefix,30) == 0, test.run('cat ' + prefix + '.log')
        with test.client.open_sftp() as sftp:
            with sftp.open(destination,'rb') as stream: reloaded = stream.read(4*1024*1024+1)
        assert code(reloaded) == code(saved) + b' ', 'Studio console load/editor Save did not update its established file'
        (args.evidence / 'studio-loaded-copy.tic').write_bytes(reloaded)
        rows.append(dict(action='console-load-editor-save',source='private validation.tic',
                         saved_sha256=hashlib.sha256(reloaded).hexdigest(),saved_bytes=len(reloaded)))
    return dict(passed=True, actions=rows, last=last, original_frontend_retained=True)
