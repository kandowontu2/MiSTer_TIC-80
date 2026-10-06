"""Inspect, install or roll back a qualified development package on its board.

This installer requires the included Main to be already running. It preserves
cartridges, saves, controller maps, MiSTer.ini and the Frontier daemon.
"""
import argparse
from datetime import datetime, timezone
import errno
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import time

import paramiko
from hardware_ssh import command, connect
from candidate_components import DESTINATIONS, qualified_hashes

PRESERVED = ('MiSTer.ini', 'MiSTer_Frontier/Master_Daemon.sh',
             'games/TIC-80/frontend.txt', '_Other/TIC80_20260930.rbf')
SD = '/media/fat/'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_package(folder):
    manifest = json.loads((folder / 'manifest.json').read_text())
    assert manifest['kind'] == 'development-candidate'
    assert set(manifest['files']) == DESTINATIONS
    assert sha(folder / 'qualification.json') == manifest['qualification_sha256']
    qualification = json.loads((folder / 'qualification.json').read_text())
    assert qualification['bounded_studio_playback_checks_passed']
    assert qualification['fresh_hdmi_stereo_confirmation']
    qualified = qualified_hashes(qualification)
    for name, expected in qualified.items():
        assert manifest['files'][name]['sha256'] == expected, 'Unqualified component: ' + name
    for name, item in manifest['files'].items():
        assert not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts
        payload = folder / 'sd-card' / name
        assert sha(payload) == item['sha256'] and payload.stat().st_size == item['bytes'], name
    return manifest


class Installer:
    def __init__(self, client, package, plan_path, tag):
        self.client, self.package, self.plan_path = client, package, plan_path
        self.manifest = validate_package(package)
        self.stage = SD + 'games/TIC-80/.install-candidates/' + tag
        self.plan = None

    def run(self, text):
        return command(self.client, text)

    def selected(self):
        value = self.run('cat /tmp/CORENAME').strip()
        assert value in ('MENU', 'TIC-80'), 'Another core owns the board: ' + value
        return value

    def mutate_menu(self, text):
        assert self.selected() == 'MENU'
        return self.run('set -e; test "$(cat /tmp/CORENAME)" = MENU; ' + text)

    def snapshot(self, names):
        answer = {}
        with self.client.open_sftp() as sftp:
            for name in names:
                path = SD + name
                try:
                    stat = sftp.stat(path)
                except OSError as error:
                    if error.errno != errno.ENOENT:
                        raise
                    answer[name] = None
                    continue
                value = self.run('sha256sum -- ' + shlex.quote(path)).split()[0]
                answer[name] = dict(sha256=value, bytes=stat.st_size, mode=stat.st_mode & 0o777)
        return answer

    def running_main(self):
        expected = self.manifest['files']['MiSTer']['sha256']
        pids = self.run('pidof MiSTer').split()
        assert len(pids) == 1 and pids[0].isdigit()
        assert self.run('readlink /proc/' + pids[0] + '/exe').strip() == '/media/fat/MiSTer'
        assert self.run('sha256sum /proc/' + pids[0] + '/exe').split()[0] == expected
        assert self.run('sha256sum /media/fat/MiSTer').split()[0] == expected
        return pids[0]

    def save(self):
        self.plan['updated_at'] = datetime.now(timezone.utc).isoformat()
        self.plan_path.write_text(json.dumps(self.plan, indent=2) + '\n')

    def inspect(self, host):
        assert not self.plan_path.exists(), 'Preserve the previous installation plan'
        self.selected()
        pid = self.running_main()
        before = self.snapshot(sorted(DESTINATIONS | set(PRESERVED)))
        assert before['MiSTer_Frontier/Master_Daemon.sh'], 'Existing Frontier daemon required'
        # The package chooses Studio by default; preserve an explicit user choice.
        if before['games/TIC-80/frontend.txt']:
            value = self.run('cat /media/fat/games/TIC-80/frontend.txt').strip()
            assert value in ('studio', 'player'), value
        else:
            value = 'studio'
        self.plan = dict(created_at=datetime.now(timezone.utc).isoformat(), host=host,
            package=str(self.package), manifest_sha256=sha(self.package / 'manifest.json'),
            stage=self.stage, before=before, main_pid=pid, frontend=value,
            installation_complete=False, steps=[])
        self.save()
        print('Read-only installation plan recorded:', self.plan_path)

    def load_plan(self, host):
        self.plan = json.loads(self.plan_path.read_text())
        assert self.plan['host'] == host and self.plan['stage'] == self.stage
        assert self.plan['manifest_sha256'] == sha(self.package / 'manifest.json')

    def menu(self):
        selected = self.selected()
        if selected == 'TIC-80':
            self.run('test "$(cat /tmp/CORENAME)" = TIC-80 && '
                     'printf "load_core /media/fat/menu.rbf\\n" > /dev/MiSTer_cmd')
        deadline = time.monotonic() + 30
        while True:
            selected = self.selected()
            children = self.run("ps -eo comm | awk '$1 == \"TIC-80\" || $1 == \"TIC-80-Studio\" || $1 == \"tic80-studio\" {print}'").strip()
            if selected == 'MENU' and not children:
                return
            assert time.monotonic() < deadline, 'TIC-80 frontend did not depart'
            time.sleep(.25)

    def install(self, host):
        self.load_plan(host)
        assert not self.plan['steps'] and not self.plan['installation_complete'], 'Never retry an installation mutation'
        assert self.snapshot(self.plan['before']) == self.plan['before'], 'Destination or configuration changed since inspection'
        self.running_main()
        self.menu()
        self.mutate_menu('test ! -e ' + shlex.quote(self.stage) + '; mkdir -p ' + shlex.quote(self.stage))
        self.plan['steps'].append('private-stage-created')
        self.save()
        for name in sorted(DESTINATIONS - {'MiSTer'}):
            q = shlex.quote
            destination = SD + name
            staged = self.stage + '/new/' + name
            backup = self.stage + '/before/' + name
            before = self.plan['before'][name]
            self.mutate_menu('mkdir -p ' + q(str(PurePosixPath(staged).parent)) + ' ' + q(str(PurePosixPath(backup).parent)))
            if before:
                self.mutate_menu('cp -p -- ' + q(destination) + ' ' + q(backup) + '; sync')
                assert self.run('sha256sum -- ' + q(backup)).split()[0] == before['sha256']
            with self.client.open_sftp() as sftp:
                sftp.put(str(self.package / 'sd-card' / name), staged)
            expected = self.manifest['files'][name]
            assert self.run('sha256sum -- ' + q(staged)).split()[0] == expected['sha256']
            self.mutate_menu('chmod ' + ('755' if expected['executable'] else '644') + ' ' + q(staged) + '; sync')
        self.plan['steps'].append('payload-and-backups-verified')
        self.save()
        assert self.snapshot(self.plan['before']) == self.plan['before'], 'Destination changed during staging'
        # All files are staged before the first destination changes. Promote the
        # handler last; every mutation checks MENU, and the current Main is kept.
        names = sorted(DESTINATIONS - {'MiSTer', 'games/TIC-80/_handler.sh'}) + ['games/TIC-80/_handler.sh']
        for name in names:
            q = shlex.quote
            destination = SD + name
            staged = self.stage + '/new/' + name
            expected = self.manifest['files'][name]['sha256']
            temporary = self.stage + '/promote.tmp'
            self.plan['steps'].append('begin-promote:' + name)
            self.save()
            self.mutate_menu('mkdir -p ' + q(str(PurePosixPath(destination).parent)) + '; cp -p -- ' + q(staged) + ' ' + q(temporary) + '; sync')
            assert self.run('sha256sum -- ' + q(temporary)).split()[0] == expected
            self.mutate_menu('mv -f -- ' + q(temporary) + ' ' + q(destination) + '; sync')
            assert self.run('sha256sum -- ' + q(destination)).split()[0] == expected
            self.plan['steps'].append('promoted:' + name)
            self.save()
        self.running_main()
        after = self.snapshot(sorted(DESTINATIONS | set(PRESERVED)))
        for name in PRESERVED:
            assert after[name] == self.plan['before'][name], name
        for name in DESTINATIONS:
            assert after[name]['sha256'] == self.manifest['files'][name]['sha256'], name
        self.plan.update(installation_complete=True, after=after, core='MENU')
        self.save()
        print('Development package installed with verified backups; matching Main and user settings preserved')

    def rollback(self, host):
        self.load_plan(host)
        self.running_main()
        current = self.snapshot(sorted(DESTINATIONS | set(PRESERVED)))
        for name in PRESERVED:
            assert current[name] == self.plan['before'][name], name
        for name in DESTINATIONS:
            old = self.plan['before'][name]
            value = current[name]
            allowed = {old['sha256'] if old else None, self.manifest['files'][name]['sha256']}
            assert (value['sha256'] if value else None) in allowed, 'External change: ' + name
        self.menu()
        for name in sorted(DESTINATIONS - {'MiSTer'}):
            old = self.plan['before'][name]
            if current[name] == old:
                continue
            q = shlex.quote
            destination = SD + name
            if old:
                backup = self.stage + '/before/' + name
                assert self.run('sha256sum -- ' + q(backup)).split()[0] == old['sha256']
                temporary = self.stage + '/rollback.tmp'
                self.mutate_menu('cp -p -- ' + q(backup) + ' ' + q(temporary) + '; sync')
                assert self.run('sha256sum -- ' + q(temporary)).split()[0] == old['sha256']
                self.mutate_menu('mv -f -- ' + q(temporary) + ' ' + q(destination) + '; sync')
            else:
                self.mutate_menu('rm -f -- ' + q(destination) + '; sync')
        assert self.snapshot(self.plan['before']) == self.plan['before']
        self.plan.update(rolled_back=True, installation_complete=False, core='MENU')
        self.save()
        print('Verified package rollback complete; matching Main retained; board on MENU')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--package', required=True, type=Path)
    parser.add_argument('--plan', required=True, type=Path)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--mode', choices=('inspect', 'install', 'rollback'), required=True)
    args = parser.parse_args()
    assert re.fullmatch('[a-z0-9]+(?:-[a-z0-9]+)*', args.tag)
    client = paramiko.SSHClient()
    client.load_system_host_keys()
    connect(client, args.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=10)
    try:
        installer = Installer(client, args.package.resolve(), args.plan.resolve(), args.tag)
        getattr(installer, args.mode)(args.host)
    finally:
        client.close()


if __name__ == '__main__':
    main()
