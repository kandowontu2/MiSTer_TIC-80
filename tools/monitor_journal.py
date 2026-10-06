"""Collect a bounded hardware monitor without tying it to a long SSH stream."""
import json
import shlex
import time
import uuid


class MonitorJournal:
    def __init__(self, command, seconds, interval_ms, read_command=None):
        assert 10 <= seconds <= 3600 and 10 <= interval_ms <= 250
        self.command, self.seconds = command, seconds
        self.read_command = read_command or command
        self.directory = '/tmp/tic80-mister-dev/monitor-' + uuid.uuid4().hex
        script = (f'/tmp/tic80-mister-dev/runtime-monitor --seconds {seconds} '
                  f'--interval-ms {interval_ms} > {self.directory}/samples '
                  f'2> {self.directory}/stderr; status=$?; '
                  f'printf "%s\\n" "$status" > {self.directory}/status.tmp; '
                  f'mv {self.directory}/status.tmp {self.directory}/status')
        # Start once. Reconnection is permitted only for subsequent reads.
        command('set -e; test "$(cat /tmp/CORENAME)" = TIC-80; mkdir ' + self.directory + '; '
                'nohup sh -c ' + shlex.quote(script) + ' < /dev/null > /dev/null 2>&1 & '
                'echo $! > ' + self.directory + '/pid')

    def rows(self):
        position = 0
        deadline = time.monotonic() + self.seconds + 120
        while time.monotonic() < deadline:
            # Read status first: observing completion guarantees that the
            # following tail includes the final append. Reading status after
            # tail could otherwise accept completion while omitting last rows.
            raw = self.read_command('printf "__TM_MONITOR_EXIT__"; '
                               f'if test -f {self.directory}/status; then cat {self.directory}/status; '
                               'else printf "pending\\n"; fi; '
                               f'tail -c +{position + 1} {self.directory}/samples 2>/dev/null || true')
            status, data = raw.split('\n', 1)
            assert status.startswith('__TM_MONITOR_EXIT__'), 'Missing monitor status header'
            status = status[len('__TM_MONITOR_EXIT__'):]
            status = status.strip()
            # An append may be visible before its newline. Read that row again
            # on the next poll rather than losing or accepting a partial sample.
            complete = data[:data.rfind('\n') + 1]
            position += len(complete.encode())
            for line in complete.splitlines():
                yield json.loads(line)
            if status != 'pending':
                assert status == '0', self.read_command(f'cat {self.directory}/stderr')
                assert complete == data, 'Monitor exited with a partial sample'
                return
            time.sleep(1)
        raise TimeoutError('Bounded monitor journal did not complete: ' + self.directory)
