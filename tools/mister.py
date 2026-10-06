"""Run development commands/transfers over SSH without storing credentials.

Requires paramiko. Password is read from TM_SSH_PASSWORD or a hidden prompt.
First-use host keys are saved under ignored build/ssh_known_hosts; subsequent
connections reject changed keys. This tool does not select an FPGA core.
"""
import argparse
import getpass
import os
from pathlib import Path
import sys
import paramiko
from hardware_access import require_access
from hardware_ssh import connect

parser = argparse.ArgumentParser()
parser.add_argument("--host", required=True)
parser.add_argument("--user", default="root")
parser.add_argument("--upload", nargs=2, action="append", default=[], metavar=("LOCAL", "REMOTE"))
parser.add_argument("--download", nargs=2, action="append", default=[], metavar=("REMOTE", "LOCAL"))
parser.add_argument("--command")
args = parser.parse_args()
require_access(args.host)
keys = Path(__file__).resolve().parents[1] / "build" / "ssh_known_hosts"
keys.parent.mkdir(parents=True, exist_ok=True)

class FirstUseKey(paramiko.MissingHostKeyPolicy):
    def missing_host_key(self, client, hostname, key):
        client.get_host_keys().add(hostname, key.get_name(), key)
        client.save_host_keys(str(keys))
        print(f"Saved SSH host key for {hostname}")

client = paramiko.SSHClient()
client.load_system_host_keys()
if keys.exists():
    client.load_host_keys(str(keys))
client.set_missing_host_key_policy(FirstUseKey())
try:
    connect(client,args.host, username=args.user,
                   password=os.environ.get("TM_SSH_PASSWORD") or getpass.getpass("SSH password: "),
                   timeout=10)
    with client.open_sftp() as sftp:
        for local, remote in args.upload:
            sftp.put(local, remote)
        for remote, local in args.download:
            sftp.get(remote, local)
    if args.command:
        _, stdout, stderr = client.exec_command(args.command, timeout=120)
        # Small development commands only; consume both channels to avoid a
        # full remote stderr pipe blocking while stdout is read.
        channel = stdout.channel
        import select
        while not channel.exit_status_ready() or channel.recv_ready() or channel.recv_stderr_ready():
            if channel.recv_ready():
                sys.stdout.buffer.write(channel.recv(65536)); sys.stdout.buffer.flush()
            if channel.recv_stderr_ready():
                sys.stderr.buffer.write(channel.recv_stderr(65536)); sys.stderr.buffer.flush()
            if not channel.recv_ready() and not channel.recv_stderr_ready():
                select.select([channel], [], [], 0.1)
        sys.exit(channel.recv_exit_status())
finally:
    client.close()
