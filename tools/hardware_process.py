"""Observe settled descriptor counts without treating transient I/O as leaks."""
import re
import time


def arguments(command, pid):
    """Return argv, or None if this enumerated process has already departed.

    An empty /proc read must never be classified as a frontend parent simply
    because it lacks the worker flag. No process is started or signalled.
    """
    if not str(pid).isdigit():
        raise ValueError('Expected a numeric process ID')
    text = command(f'tr "\\0" " " 2>/dev/null < /proc/{pid}/cmdline || true').strip()
    return text or None


def descriptors(command, pid, baseline=None):
    assert str(pid).isdigit()
    samples = []
    details = []
    deadline = time.monotonic() + .5
    while True:
        count = len(command(f'ls /proc/{pid}/fd').split())
        assert count >= 3, dict(pid=pid, invalid_descriptor_count=count)
        samples.append(count)
        if baseline is not None and count > baseline + 1 and not details:
            try:
                details.append(command(f'ls -l /proc/{pid}/fd'))
            except RuntimeError as error:
                # ls enumerates then resolves fd links. A handle can close in
                # between; that optional diagnostic must not abort the actual
                # count/settling check. Accept only this PID's vanished numeric
                # links. Missing processes, denied access and mixed errors still
                # propagate, and the unchanged count bound below still applies.
                message = str(error).strip()
                vanished = rf"ls: cannot access '/proc/{re.escape(str(pid))}/fd/[0-9]+': No such file or directory"
                if not message or not all(re.fullmatch(vanished, line) for line in message.splitlines()):
                    raise
                details.append('Descriptor closed during diagnostic observation: ' + message)
        if len(samples) >= 3 and (baseline is None or min(samples) <= baseline + 1):
            break
        if time.monotonic() >= deadline:
            break
        time.sleep(.01)
    settled = min(samples)
    if baseline is not None:
        assert settled <= baseline + 1, dict(pid=pid, baseline=baseline,
                                           samples=samples, details=details)
    return dict(fds=settled, fd_peak=max(samples), fd_samples=samples, fd_details=details)
