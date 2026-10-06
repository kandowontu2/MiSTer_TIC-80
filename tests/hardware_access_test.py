"""A device hold must prevent remote commands, including automatic restoration."""
import json
import runpy
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import hardware_access
from hardware_ssh import command, connect


class Client:
    def __init__(self, host):
        self.host, self.calls, self.connections = host, 0, []
    def connect(self, *args, **kwargs):
        self.connections.append((args, kwargs))
        return self
    def get_transport(self): return self
    def getpeername(self): return self.host, 22
    def set_keepalive(self, value): pass
    def exec_command(self, text, timeout):
        self.calls += 1
        raise LookupError('Reached the remote command boundary')


with tempfile.TemporaryDirectory() as folder:
    gate = Path(folder) / 'hold.json'
    hold = dict(active=True, host='192.168.1.176', instruction='Leave MENU untouched until the user resumes TIC-80')
    gate.write_text(json.dumps(hold))
    with patch.object(hardware_access, 'HOLD_FILE', gate):
        client = Client('192.168.1.176')
        try: command(client, 'load_core TIC-80')
        except RuntimeError as error: assert 'Leave MENU untouched' in str(error)
        else: raise AssertionError('Held core restoration was accepted')
        assert client.calls == 0
        try: connect(client, client.host, 22, username='root', timeout=10)
        except RuntimeError as error: assert 'Leave MENU untouched' in str(error)
        else: raise AssertionError('Held SSH connection was accepted')
        assert not client.connections
        # A held CLI must fail before asking for a password or creating a client.
        with patch.object(sys, 'argv', ['mister.py', '--host', client.host]), \
                patch.dict(sys.modules, paramiko=SimpleNamespace()), \
                patch('getpass.getpass', side_effect=AssertionError('Password prompt reached')):
            try: runpy.run_path(str(Path(__file__).resolve().parents[1] / 'tools/mister.py'), run_name='__main__')
            except RuntimeError as error: assert 'Leave MENU untouched' in str(error)
            else: raise AssertionError('Held CLI was accepted')
        for host, active in [('192.168.1.177', True), ('192.168.1.176', False)]:
            hold['active'] = active
            gate.write_text(json.dumps(hold))
            client = Client(host)
            try: command(client, 'probe')
            except LookupError: pass
            else: raise AssertionError('Unheld command did not reach the boundary')
            assert client.calls == 1
            assert connect(client, host, 22, username='root', timeout=10) is client
            assert client.connections == [((host, 22), dict(username='root', timeout=10))]
        client = Client('192.168.1.176')
        with patch.object(client, 'connect', side_effect=ConnectionError('Unavailable')) as attempt:
            try: connect(client, client.host, timeout=10)
            except ConnectionError as error: assert str(error) == 'Unavailable'
            else: raise AssertionError('Connection failure was swallowed')
            attempt.assert_called_once_with(client.host, timeout=10)
print('Held device: zero SSH connections/commands; other hosts and explicit release pass through; failures are not retried')
