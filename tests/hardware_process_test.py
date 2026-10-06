"""Reject incomplete SSH observations and retained descriptor growth."""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from hardware_process import arguments, descriptors

calls=[]
def argv_command(text):
    calls.append(text)
    return 'candidate --studio-worker ' if '/123/' in text else ''
assert arguments(argv_command,'123')=='candidate --studio-worker'
assert arguments(argv_command,'124') is None
assert '2>/dev/null < /proc/124/cmdline' in calls[-1]
before=len(calls)
try:arguments(argv_command,'123; kill 1')
except ValueError:pass
else:raise AssertionError('Invalid PID dispatched')
assert len(calls)==before


def observe(counts, diagnostic_error=None):
    remaining = iter(counts)
    last = counts[-1]

    def command(text):
        if text.startswith('ls -l '):
            if diagnostic_error is not None:
                raise diagnostic_error
            return 'held fd diagnostic'
        count = next(remaining, last)
        return '\n'.join(str(fd) for fd in range(count))

    return descriptors(command, '123', baseline=5)


transient = observe([7, 6, 5])
assert transient['fds'] == 5 and transient['fd_peak'] == 7
assert transient['fd_samples'] == [7, 6, 5] and transient['fd_details']
assert observe([5])['fds'] == 5
assert observe([6])['fds'] == 6
closed_fd = RuntimeError("ls: cannot access '/proc/123/fd/7': No such file or directory\n")
raced = observe([7, 6, 5], closed_fd)
assert raced['fds'] == 5 and raced['fd_samples'] == [7, 6, 5]
assert '/proc/123/fd/7' in raced['fd_details'][0]
multiple = RuntimeError("ls: cannot access '/proc/123/fd/7': No such file or directory\n"
    "ls: cannot access '/proc/123/fd/8': No such file or directory\n")
assert observe([7, 5, 5], multiple)['fds'] == 5
for error in (
    RuntimeError("ls: cannot access '/proc/123/fd/7': Permission denied\n"),
    RuntimeError("ls: cannot access '/proc/123/fd': No such file or directory\n"),
    RuntimeError("ls: cannot access '/proc/999/fd/7': No such file or directory\n"),
    RuntimeError(str(closed_fd) + 'ls: Input/output error\n'),
    RuntimeError(''),
    ConnectionError('SSH transport lost'),
):
    try: observe([7, 5, 5], error)
    except type(error) as observed:
        assert observed is error
    else: raise AssertionError(f'Unrelated diagnostic failure suppressed: {error}')
for invalid in (0, 1, 2):
    try:
        observe([invalid])
    except AssertionError as error:
        assert error.args[0]['invalid_descriptor_count'] == invalid
    else:
        raise AssertionError(f'Incomplete descriptor observation accepted: {invalid}')
started = time.monotonic()
try:
    observe([7])
except AssertionError as error:
    assert min(error.args[0]['samples']) == 7
else:
    raise AssertionError('Retained descriptor growth accepted')
assert .5 <= time.monotonic() - started < 1
try: observe([7], closed_fd)
except AssertionError as error:
    assert min(error.args[0]['samples']) == 7 and error.args[0]['baseline'] == 5
    assert '/proc/123/fd/7' in error.args[0]['details'][0]
else: raise AssertionError('Closed-descriptor diagnostic waived persistent growth')
print('Descriptor observer: transient closure, unchanged leak limit, incomplete output and unrelated-error rejection passed')
