"""Guarded development test of native MGL loading for every pinned runtime.

Selects TIC-80 and leaves it selected. Requires the matching player/RBF already
installed and tools/mister.py's trusted host key. No keyboard events are sent.
--format png tests compressed-chunk PNGs; --format legacy tests pixel-bit PNGs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import time
import paramiko
from hardware_ssh import command as ssh_command, connect
from hardware_source import source_snapshot

ROOT = Path(__file__).resolve().parents[1]
LANGUAGES = "lua js moon yue fennel scheme squirrel python wren janet wasm ruby miniscript forth".split()
p = argparse.ArgumentParser()
p.add_argument("--host", required=True)
p.add_argument("--format", choices=("native", "png", "legacy"), default="native")
p.add_argument("--label", default="", help="Keep a revision's captures/manifest separate")
p.add_argument("--audio-check", action="store_true", help="Require advancing playback and zero underruns in every demo")
p.add_argument("--resume", action="store_true", help="Retain a checked manifest prefix after an observer interruption")
p.add_argument("--languages", nargs='+', choices=LANGUAGES, default=LANGUAGES,
               help="Explicit runtime subset for a focused regression; default is all 14")
a = p.parse_args()
assert len(set(a.languages)) == len(a.languages), 'Duplicate runtimes'
if a.label and (len(a.label)>32 or any(ch not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for ch in a.label)):
    p.error('Label must contain at most 32 lowercase letters, digits or hyphens')
client = paramiko.SSHClient()
client.load_system_host_keys()
keys = ROOT / "build/ssh_known_hosts"
if keys.exists():
    client.load_host_keys(str(keys))
connect(client,a.host, username="root", password=os.environ["TM_SSH_PASSWORD"], timeout=10)

def command(text):
    return ssh_command(client,text)

def selected():
    return command("cat /tmp/CORENAME").strip()

def log():
    return command("cat /media/fat/logs/TIC-80/tic80.log 2>/dev/null || true")

def supervisor():
    # BusyBox pidof includes children exec'd from the same ELF. Only the
    # supervisor's comm remains TIC-80; VM children are named tic80-vm.
    return command('for p in $(pidof TIC-80); do '
                   'test "$(cat /proc/$p/comm 2>/dev/null)" = "TIC-80" && printf "%s " "$p"; '
                   'done; true').strip()
results = []
suffix = ("-" + a.label if a.label else "") + ("" if a.format == "native" else "-" + a.format)
try:
    expected = hashlib.sha256((ROOT / "build/arm/tic80-live").read_bytes()).hexdigest()
    installed = command("sha256sum /media/fat/games/TIC-80/TIC-80").split()[0]
    if installed != expected:
        raise RuntimeError("Installed player does not match the tested ARM build")
    rbf_hash = hashlib.sha256((ROOT / 'build/fpga/output_files/TIC80.rbf').read_bytes()).hexdigest()
    if command('sha256sum /media/fat/_Other/TIC80_20260930.rbf').split()[0] != rbf_hash:
        raise RuntimeError('Installed RBF does not match the qualified local build')
    if a.audio_check:
        monitor_hash = hashlib.sha256((ROOT / 'build/arm/tic80-runtime-monitor').read_bytes()).hexdigest()
        assert command('sha256sum /tmp/tic80-mister-dev/runtime-monitor').split()[0] == monitor_hash
    if a.resume:
        previous = json.loads((ROOT / 'build' / f'live-languages{suffix}.json').read_text())
        assert len(previous) < len(a.languages), 'Manifest is already complete'
        for index, record in enumerate(previous):
            assert record['language'] == a.languages[index], 'Manifest is not an ordered prefix'
            assert record['player_sha256'] == expected and record['rbf_sha256'] == rbf_hash
            assert (ROOT / 'build' / record['capture']).is_file()
            if a.audio_check:
                audio = record['audio']
                assert audio['underruns'] == 0 and audio['monitor_sha256'] == monitor_hash
                journal = ROOT / 'build' / f'hardware-language{suffix}-{record["language"]}-audio.jsonl'
                assert hashlib.sha256(journal.read_bytes()).hexdigest() == audio['samples_sha256']
        results.extend(previous)
    if selected() not in ("MENU", "TIC-80"):
        raise RuntimeError("Board is in use by another core")
    with client.open_sftp() as sftp:
        for index, language in enumerate(a.languages):
            if a.resume and index < len(previous):
                continue
            allowed = "MENU" if index == 0 and selected() == "MENU" else "TIC-80"
            if selected() != allowed:
                raise RuntimeError("Core changed before native transfer")
            filename = language + (".tic" if a.format == "native" else "-legacy.png" if a.format == "legacy" else ".png")
            cartridge = ROOT / "build" / ("languages" if a.format == "native" else "png-carts") / filename
            directory = "/tmp/tic80-mister-dev/languages" if a.format == "native" else "/tmp/tic80-mister-dev/png"
            remote = directory + f"/{language}{suffix}.mgl"
            sftp.put(str(cartridge), directory + "/" + filename)
            size = cartridge.stat().st_size
            file_index = 0 if a.format == 'native' else 0x40
            with sftp.open(remote, "w") as file:
                file.write('<mistergamedescription>\n <rbf>_Other/TIC80</rbf>\n'
                           f' <file delay="3" type="f" index="{file_index}" path="{directory}/{filename}"/>\n'
                           '</mistergamedescription>\n')
            marker = f"Cartridge loaded: {size} bytes; reset=0"
            # A repeated run must observe a fresh load, not a line retained
            # from an earlier transfer. The daemon may also rotate the log.
            previous_log = log()
            previous_pid = supervisor()
            command(f'test "$(cat /tmp/CORENAME)" = "{allowed}" || exit 30; '
                    f'printf "load_core {remote}\\n" > /dev/MiSTer_cmd')
            deadline = time.monotonic() + 40
            while True:
                core = selected()
                if core not in ("MENU", "TIC-80"):
                    raise RuntimeError("Another core selected during native transfer")
                if core == "TIC-80":
                    current_log = log()
                    current_pid = supervisor()
                    fresh = current_log.count(marker) > previous_log.count(marker)
                    if marker in current_log and (fresh or (current_pid and current_pid != previous_pid)):
                        break
                if time.monotonic() >= deadline:
                    raise RuntimeError(f"{language} native transfer failed: {log()}")
                time.sleep(.5)
            time.sleep(2)
            if selected() != "TIC-80":
                raise RuntimeError("Core changed after cartridge load")
            current_log = log()
            if any(text in current_log for text in ("Cartridge failed", "Cartridge rejected", "TIC-80:", "Save error")):
                raise RuntimeError(current_log)
            source = source_snapshot(command, directory + '/' + filename, size)
            folder = "/media/fat/screenshots/TIC-80"
            before = set(sftp.listdir(folder))
            command('test "$(cat /tmp/CORENAME)" = "TIC-80" || exit 31; printf "screenshot\\n" > /dev/MiSTer_cmd')
            deadline = time.monotonic() + 10
            while True:
                new = sorted(set(sftp.listdir(folder)) - before)
                new = [name for name in new if name.endswith(".png")]
                if new:
                    local = ROOT / "build" / f"hardware-language{suffix}-{language}.png"
                    sftp.get(folder + "/" + new[-1], str(local))
                    data = local.read_bytes()
                    # MiSTer creates the file before finishing the PNG write.
                    if data.endswith(b"\x00\x00\x00\x00IEND\xaeB\x60\x82"):
                        break
                if time.monotonic() >= deadline:
                    raise RuntimeError("MiSTer screenshot did not arrive")
                time.sleep(.2)
            header = data[:24]
            if header[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack(">II", header[16:24]) != (256,144):
                raise RuntimeError("Unexpected live capture geometry")
            audio = None
            if a.audio_check:
                samples = command('/tmp/tic80-mister-dev/runtime-monitor --seconds 2 --interval-ms 13')
                rows = [json.loads(line) for line in samples.splitlines()]
                assert len(rows) >= 100 and len({r['session'] for r in rows}) == 1
                assert all(r['underruns'] == 0 for r in rows), (language, rows)
                assert ((rows[-1]['played'] - rows[0]['played']) & 0xffffffff) > 90000
                journal = ROOT / 'build' / f'hardware-language{suffix}-{language}-audio.jsonl'
                journal.write_text(samples)
                audio = dict(samples=len(rows), underruns=0, monitor_sha256=monitor_hash,
                             samples_sha256=hashlib.sha256(journal.read_bytes()).hexdigest(),
                             queue_min=min((r['written']-r['played']) & 0xffffffff for r in rows),
                             queue_max=max((r['written']-r['played']) & 0xffffffff for r in rows))
            results.append({"language": language, "bytes": size, "capture": local.name,
                            "player_sha256": expected, "rbf_sha256": rbf_hash, "audio": audio,
                            "file_index": file_index, "source_transport": source})
            print(f"{language}: {a.format} load and live 256x144 capture passed", flush=True)
finally:
    (ROOT / "build" / f"live-languages{suffix}.json").write_text(json.dumps(results, indent=2) + "\n")
    client.close()
