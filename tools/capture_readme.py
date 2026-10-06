"""Capture real MiSTer Studio views, then return to RUN; no project edits."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import time
import paramiko
from hardware_ssh import connect, command


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", required=True)
    p.add_argument("--helper", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--refresh", action="store_true", help="Capture console help and the top of the code editor")
    a = p.parse_args(); a.output.mkdir(parents=True, exist_ok=True)
    manifest = dict(captured_at=datetime.now(timezone.utc).isoformat(),
                    source="MiSTer native screenshot command; actual FPGA video pipeline",
                    stock_Main_qualification=False, screenshots=[])
    c = paramiko.SSHClient(); c.load_system_host_keys()
    changed = False
    remote = "/tmp/tic80-readme-capture-20261005"
    run = lambda text: command(c, text, timeout=45)
    def selected():
        assert run("cat /tmp/CORENAME").strip() == "TIC-80", "Another core selected"
    def key(action):
        selected(); run(remote + " " + action)
    def capture(name, view):
        selected()
        folder = "/media/fat/screenshots/TIC-80"
        with c.open_sftp() as sf:
            before = set(sf.listdir(folder))
            run('test "$(cat /tmp/CORENAME)" = TIC-80 && printf "screenshot\\n" > /dev/MiSTer_cmd')
            end = time.monotonic() + 25
            local = a.output / name
            assert not local.exists()
            while True:
                fresh = sorted(x for x in set(sf.listdir(folder)) - before if x.endswith(".png"))
                if fresh:
                    sf.get(folder + "/" + fresh[-1], str(local))
                    if local.read_bytes().endswith(b"\x00\x00\x00\x00IEND\xaeB\x60\x82"):
                        break
                assert time.monotonic() < end, "Screenshot did not complete"
                time.sleep(.25)
        from PIL import Image
        with Image.open(local) as image:
            assert image.size == (256, 144), image.size
            image.verify()
        manifest["screenshots"].append(dict(file=name, view=view, width=256, height=144,
            sha256=hashlib.sha256(local.read_bytes()).hexdigest()))
        (a.output / "capture-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print("Captured " + view, flush=True)
    try:
        connect(c, a.host, username="root", password=os.environ["TM_SSH_PASSWORD"], timeout=15)
        selected()
        manifest["Main_sha256"] = run("sha256sum /media/fat/MiSTer").split()[0]
        manifest["Studio_sha256"] = run("sha256sum /media/fat/games/TIC-80/TIC-80-Studio").split()[0]
        assert manifest["Studio_sha256"] == "9123c34a0b6bbe412082c862022285c3934923c6ef6a727f61deb700f84afe59"
        with c.open_sftp() as sf:
            sf.put(str(a.helper), remote)
        run("chmod 755 " + remote)
        if not a.refresh:
            capture("initial.png", "Initial Studio view; label after visual inspection")
        changed = True
        key("console"); key("code")
        if a.refresh:
            key("top")
        capture("studio-code.png", "Studio code editor")
        key("console")
        if a.refresh:
            key("clear")
            key("help")
        capture("studio-console.png", "Studio console")
        if not a.refresh:
            key("sprite")
            capture("studio-sprites.png", "Studio sprite editor")
            key("music")
            capture("studio-music.png", "Studio music editor")
    finally:
        try:
            if changed:
                key("run")
                manifest["run_shortcut_delivered"] = True
            (a.output / "capture-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        finally:
            c.close()


if __name__ == "__main__":
    main()
