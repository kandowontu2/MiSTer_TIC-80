"""Preserve a coherent current Studio cart before a hardware test changes cores."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import time
import paramiko
from hardware_ssh import connect, command
from hardware_process import arguments


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", required=True)
    p.add_argument("--exporter", type=Path, required=True)
    p.add_argument("--evidence", type=Path, required=True)
    p.add_argument("--tag", required=True)
    a = p.parse_args()
    assert a.tag.replace("-", "").isalnum()
    a.evidence.mkdir(parents=True)
    remote = "/media/fat/games/TIC-80/.recovery/" + a.tag
    r = dict(started_at=datetime.now(timezone.utc).isoformat(), passed=False, remote=remote, samples=[])
    client = paramiko.SSHClient(); client.load_system_host_keys()
    stopped = []
    run = lambda text: command(client, text, timeout=45)
    def save(): (a.evidence / "result.json").write_text(json.dumps(r, indent=2) + "\n")
    try:
        save(); connect(client, a.host, username="root", password=os.environ["TM_SSH_PASSWORD"], timeout=15)
        assert run("cat /tmp/CORENAME").strip() == "TIC-80"
        parents = []
        workers = []
        for pid in run("pidof TIC-80-Studio").split():
            argv = arguments(run, pid)
            (workers if "--studio-worker" in argv else parents).append(pid)
            assert run("sha256sum /proc/" + pid + "/exe").split()[0] == "9123c34a0b6bbe412082c862022285c3934923c6ef6a727f61deb700f84afe59"
        assert len(parents) == len(workers) == 1
        parent = parents[0]; worker = workers[0]
        assert arguments(run, worker).endswith("--studio-worker " + parent)
        r.update(parent=parent, worker=worker)
        r["original_cart_sha256"] = run("sha256sum /media/fat/games/TIC-80/Carts/tetris.tic").split()[0]
        r["staging_dispatched"] = True; save()
        run("test ! -e " + remote + " && mkdir -p " + remote)
        with client.open_sftp() as sf:
            sf.put(str(a.exporter), remote + "/export")
        run("chmod 755 " + remote + "/export")
        links = run("for f in /proc/" + parent + "/fd/*; do printf '%s\\t' \"$f\"; readlink \"$f\"; done")
        ipc = [line.split("\t")[0] for line in links.splitlines() if line.endswith("/memfd:tic80-studio (deleted)")]
        history = [line.split("\t")[0] for line in links.splitlines() if line.endswith("/memfd:tic80-editor-history (deleted)")]
        assert len(ipc) == len(history) == 1
        for sample in range(5):
            raw = remote + "/ipc-" + str(sample) + ".bin"
            r["stop_dispatched"] = [parent, worker]; save()
            stopped = [parent, worker]
            try:
                run('test "$(cat /tmp/CORENAME)" = TIC-80 && kill -STOP ' + parent + " " + worker)
                run("cat " + ipc[0] + " > " + raw + " && cat " + history[0] + " > " + remote + "/history-" + str(sample) + ".bin")
                with client.open_sftp() as sf:
                    with sf.open(raw, "rb") as f:
                        magic, sequence, acknowledged = struct.unpack("<III", f.read(12))
                assert magic == 0x53544943
                r["samples"].append(dict(file=raw, sequence=sequence, acknowledged=acknowledged)); save()
            finally:
                run("kill -CONT " + parent + " " + worker); stopped=[]
            if sequence == acknowledged:
                break
            time.sleep(.1)
        assert sequence == acknowledged, "No coherent checkpoint; core test must not proceed"
        r["ipc_sha256"] = run("sha256sum " + raw).split()[0]
        cart = remote + "/unsaved-studio-cart.tic"
        r["export_dispatched"] = True; save()
        r["export"] = run(remote + "/export " + raw + " " + cart).strip()
        r["cart_sha256"] = run("sha256sum " + cart).split()[0]
        with client.open_sftp() as sf:
            sf.get(cart, str(a.evidence / "unsaved-studio-cart.tic"))
            sf.get(raw, str(a.evidence / "ipc.bin"))
        assert hashlib.sha256((a.evidence / "unsaved-studio-cart.tic").read_bytes()).hexdigest() == r["cart_sha256"]
        assert run("sha256sum /media/fat/games/TIC-80/Carts/tetris.tic").split()[0] == r["original_cart_sha256"]
        assert run("cat /tmp/CORENAME").strip() == "TIC-80"
        r["passed"] = True; save()
        print("Current Studio cartridge exported with exact full-cart round-trip; original cartridge unchanged.")
    finally:
        try:
            if stopped:
                run("kill -CONT " + " ".join(stopped)); r["emergency_resume"] = True
        finally:
            client.close(); save()


if __name__ == "__main__":
    main()
