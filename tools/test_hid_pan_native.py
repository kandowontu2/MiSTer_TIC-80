"""Run the reader-only UHID test using installed TIC-80 and untouched stock Main.

Switching an occupied core requires a user-authorized test window. This tool
only permits the explicitly named initial core, records single-dispatch jobs,
restores its original Main RBF/MGL argument, and verifies protected files.
It never installs candidate payloads. A native reader pass does not qualify
the current FPGA gate or end-to-end frontend mouse API.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shlex
import struct
import sys
import time
import uuid
import zipfile

import paramiko
from hardware_ssh import connect, command
from prepare_hid_native_candidate import RELEASE, RELEASE_SHA

ROOT = Path(__file__).resolve().parents[1]
STOCK = "9f6e5a237c36be6404ab4823d804821491db4bf125827f84aca2a1ca31f0a8a6"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--probe-build", type=Path, required=True)
    parser.add_argument("--initial-core", required=True)
    parser.add_argument("--host", default="192.168.1.176")
    args = parser.parse_args()
    assert args.initial_core in ("MENU", "PICO-8", "TIC-80")
    args.evidence.mkdir()
    receipt = json.loads((args.probe_build / "result.json").read_text())
    assert receipt["passed"]
    probe = args.probe_build / "arm/tic80-hid-pan-probe"
    sha = lambda data: hashlib.sha256(data).hexdigest()
    assert sha(probe.read_bytes()) == receipt["ARM_probe_sha256"]
    required_sources = {"tools/hid_pan_probe.c", "src/hid_wheel.c", "src/hid_pan.c",
                        "include/tic80_mister/hid_wheel.h", "include/tic80_mister/hid_pan.h"}
    assert required_sources <= receipt["source_sha256"].keys(), "Incomplete probe source binding"
    for name, digest in receipt["source_sha256"].items():
        assert sha((ROOT / name).read_bytes()) == digest, name
    ident = uuid.uuid4().hex
    remote = "/tmp/tic80-hid-native-" + ident
    quiet = ("-- script: lua\n-- saveid: tic80-hid-native-" + ident + "\n"
             "function TIC() cls(0); print('TIC-80 native HID reader test',12,24,12) end\n").encode()
    cart = bytes([5]) + struct.pack("<H", len(quiet)) + b"\0" + quiet
    r = {"started_at": datetime.now(timezone.utc).isoformat(), "passed": False,
         "installed_payloads_replaced": False, "shared_Main_replaced": False,
         "scope": "Reader-only real UHID/HIDraw path with stock Main evdev grab; FPGA/frontend API separate",
         "probe_sha256": receipt["ARM_probe_sha256"], "remote": remote,
         "dispatches": [], "observations": []}
    def save():
        (args.evidence / "result.json").write_text(json.dumps(r, indent=2) + "\n")
    client = paramiko.SSHClient()
    client.load_system_host_keys()
    connected = switched = launched = False
    restore_path = None
    def run(text, timeout=45):
        return command(client, text, timeout=timeout)
    def core():
        return run("cat /tmp/CORENAME").strip()
    def main_process():
        pids = run("pidof MiSTer").split()
        assert len(pids) == 1 and pids[0].isdigit(), pids
        assert run("sha256sum /proc/" + pids[0] + "/exe").split()[0] == STOCK
        assert run("sha256sum /media/fat/MiSTer").split()[0] == STOCK
        return pids[0]
    def dispatch(label, text):
        r["dispatches"].append({"name": label, "command": text, "time": time.time()})
        save()  # A lost reply never authorizes retrying this mutation.
        return run(text)
    def load(path, selected, label):
        assert core() == selected
        main_process()
        dispatch(label, "test \"$(cat /tmp/CORENAME)\" = " + shlex.quote(selected) +
                 " && printf %s " + shlex.quote("load_core " + path + "\n") + " > /dev/MiSTer_cmd")
    def wait_core(expected, seconds=40):
        deadline = time.monotonic() + seconds
        while True:
            selected = core()
            assert selected in ("", args.initial_core, "MENU", "TIC-80"), selected
            if selected == expected:
                return
            assert time.monotonic() < deadline, {"expected": expected, "actual": selected}
            time.sleep(.2)
    archive = ROOT / "releases" / f"TIC80-Frontier-{RELEASE}.zip"
    assert sha(archive.read_bytes()) == RELEASE_SHA, "Published rollback archive changed"
    with zipfile.ZipFile(archive) as published:
        expected = {line.split()[1]: line.split()[0] for line in
                    published.read("Scripts/TIC80-install/files.sha256").decode().splitlines()}
    assert len(expected) == 5 and "MiSTer" not in expected
    r["published_archive_sha256"] = RELEASE_SHA
    protected = list(expected) + ["MiSTer", "MiSTer.ini", "MiSTer_Frontier/Master_Daemon.sh",
                                  "linux/user-startup.sh"]
    def snapshot():
        return {p: run("sha256sum -- " + shlex.quote("/media/fat/" + p)).split()[0]
                for p in protected}
    def cart_words():
        assert core() == "TIC-80"
        text = run('test "$(cat /tmp/CORENAME)" = TIC-80 && '
                   'for a in 0x3A000004 0x3A000070 0x3A000074 0x3A000038; do devmem "$a" 32 || exit 32; done')
        return [int(x, 16) for x in text.split()]
    try:
        save()
        connect(client, args.host, username="root", password=os.environ["TM_SSH_PASSWORD"], timeout=20)
        connected = True
        assert core() == args.initial_core
        pid = main_process()
        with client.open_sftp() as sftp:
            with sftp.open("/proc/" + pid + "/cmdline", "rb") as stream:
                argv = [v.decode() for v in stream.read().split(b"\0") if v]
        assert len(argv) >= 2, argv
        restore_path = argv[1]
        assert (PurePosixPath(restore_path).is_absolute() and restore_path.startswith("/media/fat/")
                and ".." not in PurePosixPath(restore_path).parts
                and not any(ord(char) < 32 for char in restore_path)
                and PurePosixPath(restore_path).suffix.lower() in (".rbf", ".mgl")), argv
        run("test -f " + shlex.quote(restore_path))
        r["initial_main_argv"] = argv
        r["restore_path"] = restore_path
        r["restore_file_sha256"] = run("sha256sum " + shlex.quote(restore_path)).split()[0]
        r["before"] = snapshot()
        assert all(r["before"][p] == digest for p, digest in expected.items())
        r["carts_before"] = run("find /media/fat/games/TIC-80/Carts -exec stat -c '%d:%i:%s:%Y:%Z:%F %n' {} +")
        save()
        if args.initial_core != "MENU":
            switched = True
            load("/media/fat/menu.rbf", args.initial_core, "enter-menu")
            wait_core("MENU")
        assert core() == "MENU"
        dispatch("stage-private-test-directory", "test ! -e " + shlex.quote(remote) +
                 " && mkdir " + shlex.quote(remote))
        with client.open_sftp() as sftp:
            sftp.put(str(probe), remote + "/probe")
            with sftp.open(remote + "/quiet.tic", "wb") as stream:
                stream.write(cart)
            mgl = ("<mistergamedescription>\n <rbf>_Other/TIC80</rbf>\n"
                   f' <file delay="3" type="f" index="0" path="{remote}/quiet.tic"/>\n'
                   "</mistergamedescription>\n")
            with sftp.open(remote + "/quiet.mgl", "wb") as stream:
                stream.write(mgl.encode())
        dispatch("make-test-probe-executable", "chmod 755 " + shlex.quote(remote + "/probe"))
        assert run("sha256sum " + shlex.quote(remote + "/probe")).split()[0] == receipt["ARM_probe_sha256"]
        providers = run("/lib/ld-linux-armhf.so.3 --list " + shlex.quote(remote + "/probe"))
        assert "not found" not in providers
        (args.evidence / "native-libraries.log").write_text(providers)
        switched = True
        load(remote + "/quiet.mgl", "MENU", "load-published-TIC80")
        wait_core("TIC-80")
        deadline = time.monotonic() + 40
        while True:
            words = cart_words()
            if len(words) == 4 and words[0] == 0x314e5354 and words[1] == words[3] and words[2] == len(cart):
                break
            assert time.monotonic() < deadline, words
            time.sleep(.2)
        r["cart_before_probe"] = words
        pid = main_process()
        r["probe_main_pid"] = pid
        script = ('printf "%s\\n" "$$" > ' + remote + '/job.pid\n'
                  'taskset 1 nice -n 19 ' + remote + '/probe --native ' + pid +
                  ' > ' + remote + '/native.jsonl 2> ' + remote + '/native.stderr\n'
                  'status=$?\nprintf "%s\\n" "$status" > ' + remote + '/status.tmp\n'
                  'mv ' + remote + '/status.tmp ' + remote + '/status\n')
        with client.open_sftp() as sftp:
            with sftp.open(remote + "/run.sh", "wb") as stream:
                stream.write(script.encode())
        launched = True
        dispatch("native-probe-original-launch", "nohup sh " + shlex.quote(remote + "/run.sh") +
                 " > " + shlex.quote(remote + "/coordinator.log") + " 2>&1 < /dev/null &")
        deadline = time.monotonic() + 90
        while True:
            text = run("if test -f " + remote + "/status; then cat " + remote + "/status; fi").strip()
            if text:
                r["original_probe_exit"] = int(text)
                break
            r["original_job_pid"] = run("cat " + remote + "/job.pid").strip()
            assert r["original_job_pid"].isdigit()
            live = run("kill -0 " + r["original_job_pid"] + " 2>/dev/null && echo live || true").strip()
            r["original_job_confirmed_live"] = live == "live"
            save()
            assert live == "live", "Original job vanished without a terminal journal"
            assert time.monotonic() < deadline, "Observation budget expired; original job was not restarted"
            time.sleep(.5)
        stdout = run("cat " + remote + "/native.jsonl")
        stderr = run("cat " + remote + "/native.stderr")
        (args.evidence / "native.jsonl").write_text(stdout)
        (args.evidence / "native.stderr").write_text(stderr)
        r["cart_after_probe"] = cart_words()
        assert r["cart_before_probe"] == r["cart_after_probe"], "Cartridge changed during probe"
        assert main_process() == pid
        rows = [json.loads(line) for line in stdout.splitlines()]
        assert r["original_probe_exit"] == 0, stderr
        assert len(rows) == 5 and all(row.get("main_fd_verified") and row.get("evdev_grab_busy")
                                    and row.get("only_main_and_probe_evdev_fds") for row in rows[:4])
        assert rows[-1].get("native_reader_passed") is True and rows[-1]["total"] == 4
        r["reader_path_passed"] = True
        save()
    except BaseException as error:
        r["error"] = type(error).__name__ + ": " + str(error)
        save()
        raise
    finally:
        try:
            if connected and "before" in r:
                if switched:
                    selected = core()
                    assert selected in (args.initial_core, "MENU", "TIC-80"), "Another core selected; do not overwrite user selection"
                    if selected == "TIC-80":
                        if "cart_before_probe" in r:
                            assert cart_words() == r["cart_before_probe"], "Cartridge changed; do not overwrite user selection"
                        load("/media/fat/menu.rbf", "TIC-80", "restoration-enter-menu")
                        wait_core("MENU")
                    if launched and "original_probe_exit" not in r:
                        # Core guard halts injection. Collect the same journal;
                        # never relaunch a failed/unobserved probe.
                        deadline = time.monotonic() + 20
                        while time.monotonic() < deadline:
                            text = run("if test -f " + remote + "/status; then cat " + remote + "/status; fi").strip()
                            if text:
                                r["original_probe_exit"] = int(text)
                                break
                            time.sleep(.2)
                        assert "original_probe_exit" in r, "Original probe still needs collection"
                    if core() == "MENU":
                        assert run("sha256sum " + shlex.quote(restore_path)).split()[0] == r["restore_file_sha256"], "Original RBF/MGL changed during test"
                        load(restore_path, "MENU", "restore-original-Main-core-argument")
                        wait_core(args.initial_core)
                r["after"] = snapshot()
                assert r["before"] == r["after"], "Protected installation/configuration changed"
                if "carts_before" in r:
                    r["carts_after"] = run("find /media/fat/games/TIC-80/Carts -exec stat -c '%d:%i:%s:%Y:%Z:%F %n' {} +")
                    assert r["carts_before"] == r["carts_after"], "Cartridge tree changed"
                main_process()
                r["final_core"] = core()
                assert r["final_core"] == args.initial_core, "Core changed before final restoration verification"
                r["restored_verified"] = True
                r["passed"] = bool(r.get("reader_path_passed"))
        except BaseException as error:
            r["restoration_error"] = type(error).__name__ + ": " + str(error)
            save()
            raise
        finally:
            client.close()
            save()
    print(json.dumps({"passed": r["passed"], "original_probe_exit": r["original_probe_exit"],
                      "restored_core": args.initial_core, "installed_payloads_replaced": False}))


if __name__ == "__main__":
    main()
