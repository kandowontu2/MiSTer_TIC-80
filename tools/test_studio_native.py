"""Run offscreen Studio contracts on MiSTer's ARM CPU without changing its core."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import sys
from datetime import datetime, timezone
import paramiko

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from hardware_access import require_access
from hardware_ssh import command, connect


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.1.176")
    args = parser.parse_args()
    require_access(args.host)
    fixture = ROOT / "build/arm/studio_test"
    fixture_hash = digest(fixture)
    expected = json.loads((ROOT / "build/video-clock-installed.json").read_text())
    sources = ["CMakeLists.txt", "cmake/studio.cmake", "src/studio_system.c", "src/studio_net.c", "src/studio_rom.c",
               "include/tic80_mister/studio.h", "tests/studio_test.c", "tools/bootstrap.py"]
    source_hashes = {path: digest(ROOT / path) for path in sources}
    remote = "/tmp/tic80-mister-dev/studio-test-" + fixture_hash[:12]
    client = paramiko.SSHClient()
    client.load_system_host_keys()
    connect(client,args.host, username="root", password=os.environ["TM_SSH_PASSWORD"], timeout=10)
    try:
        run = lambda text: command(client, text)
        supervisor = lambda: run('for p in $(pidof TIC-80); do test "$(cat /proc/$p/comm 2>/dev/null)" = TIC-80 && printf "%s " "$p"; done; true').strip()
        core = run("cat /tmp/CORENAME").strip()
        assert core in ("MENU", "TIC-80"), "Another core is selected"
        parent = supervisor()
        for path, want in expected.items():
            assert run("sha256sum " + path).split()[0] == want, path
        with client.open_sftp() as sftp:
            sftp.put(str(fixture), remote)
        assert run("sha256sum " + remote).split()[0] == fixture_hash
        # Low-priority CPU0 execution leaves the running player and Main alone.
        output = run('test "$(cat /tmp/CORENAME)" = ' + core +
                     " && chmod +x " + remote + " && nice -n 19 taskset 1 " + remote)
        result = re.search(r"native/PNG/project save-reload and run/escape passed \((\d+) ticks, 800 stereo frames per tick\)", output)
        assert result
        assert run("cat /tmp/CORENAME").strip() == core
        assert supervisor() == parent
        for path, want in expected.items():
            assert run("sha256sum " + path).split()[0] == want, path
        assert source_hashes == {path: digest(ROOT / path) for path in sources}
        record = dict(tested_at=datetime.now(timezone.utc).isoformat(),
                      fixture_sha256=fixture_hash, sources=source_hashes,
                      baseline_hashes=expected, core=core, player_pid=parent,
                      studio_ticks=int(result[1]), stereo_frames_per_tick=800, output=output,
                      scope="Offscreen Studio on native ARM: console/text/clipboard/mouse sprite edit, editor navigation, native/PNG/project save-load and run/escape. No physical input, DDR transport or displayed Studio qualification.")
        (ROOT / "build/studio-native-arm.json").write_text(json.dumps(record, indent=2) + "\n")
        (ROOT / "build/studio-native-arm.log").write_text(output)
        print("Native ARM Studio contract passed; selected core and player PID preserved")
    finally:
        client.close()


if __name__ == "__main__":
    main()
