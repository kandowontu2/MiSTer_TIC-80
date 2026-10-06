"""Stage a fresh installer on MiSTer and run its read-only --check once.

This does not run installation, switch cores, or replace installed payloads.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import paramiko
from hardware_ssh import connect, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--host", default="192.168.1.176")
    args = parser.parse_args()
    args.evidence.mkdir(parents=True)
    result = {"started_at": datetime.now(timezone.utc).isoformat(), "passed": False,
              "installation_run": False, "core_switched": False, "shared_main_replaced": False}
    client = paramiko.SSHClient(); client.load_system_host_keys()
    def run(text):
        return command(client, text, timeout=45)
    def save():
        (args.evidence / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    save()
    try:
        connect(client, args.host, username="root", password=os.environ["TM_SSH_PASSWORD"], timeout=15)
        core = run("cat /tmp/CORENAME").strip()
        assert core in ("MENU", "TIC-80"), "Another core owns this board: " + core
        protected = ("MiSTer", "MiSTer.ini", "MiSTer_Frontier/Master_Daemon.sh",
                     "_Other/TIC80_20261003.rbf", "games/TIC-80/TIC-80", "games/TIC-80/TIC-80-Studio",
                     "games/TIC-80/_handler.sh", "games/TIC-80/cacert.pem")
        def snapshot():
            return {p: run("sha256sum " + shlex.quote("/media/fat/" + p)).split()[0] for p in protected}
        result["before"] = snapshot(); save()
        script = "/media/fat/Scripts/Install_TIC80.sh"
        bundle = "/media/fat/Scripts/TIC80-install"
        run("test ! -e " + script + " && test ! -e " + bundle)
        # Existing payloads are copied into the installer bundle only after
        # matching the independently packaged checksum manifest.
        payloads = {}
        for line in (args.package / "Scripts/TIC80-install/files.sha256").read_text().splitlines():
            expected, path = line.split()
            assert result["before"][path] == expected, path
            payloads[path] = expected
        result["staging_dispatched"] = True; save()
        with client.open_sftp() as sftp:
            for file in sorted(args.package.rglob("*")):
                if not file.is_file():
                    continue
                rel = file.relative_to(args.package).as_posix()
                if not rel.startswith("Scripts/") or "/payload/" in rel:
                    continue
                target = "/media/fat/" + rel
                run("mkdir -p " + shlex.quote(target.rsplit("/", 1)[0]))
                sftp.put(str(file), target)
                assert run("sha256sum " + shlex.quote(target)).split()[0] == hashlib.sha256(file.read_bytes()).hexdigest()
        for path, expected in payloads.items():
            target = bundle + "/payload/" + path
            run("mkdir -p " + shlex.quote(target.rsplit("/", 1)[0]) + " && cp " + shlex.quote("/media/fat/" + path) + " " + shlex.quote(target))
            assert run("sha256sum " + shlex.quote(target)).split()[0] == expected
        run("chmod 755 " + script)
        result["check_dispatched"] = True; save()
        output = run("bash " + script + " --check")
        (args.evidence / "check.log").write_text(output)
        result["after"] = snapshot()
        assert result["before"] == result["after"]
        assert run("cat /tmp/CORENAME").strip() == core
        result["passed"] = True
        print(output.strip())
        print("Installer staged; read-only check passed on MiSTer. Existing payloads and shared Main unchanged.")
    finally:
        client.close(); save()


if __name__ == "__main__":
    main()
