"""Start remote commands once and drain both channels through bounded delays."""
import time
from hardware_access import require_access


def connect(client, host, *args, **kwargs):
    """Honor the device hold before opening an SSH connection."""
    require_access(host)
    return client.connect(host, *args, **kwargs)


def read_with_reconnect(read, reconnect, timeout=90, delay=1,
                        retry_errors=(OSError, EOFError), on_retry=None):
    """Retry an explicitly read-only observation, never a launch or mutation.

    Callbacks must use bounded I/O timeouts. This budget stops new attempts;
    an in-flight callback may finish after it. Failed reconnections are retried
    before another read. Remote exit failures and access holds propagate.
    """
    if timeout <= 0 or delay <= 0:
        raise ValueError('Read retry budget and delay must be positive')
    deadline = time.monotonic() + timeout
    phase, failure = 'read', None
    while True:
        if failure is not None and time.monotonic() >= deadline:
            raise TimeoutError('Read-only observation recovery budget expired; '
                               'no monitor or mutation was restarted') from failure
        try:
            if phase == 'reconnect':
                reconnect()
                phase = 'read'
                continue
            return read()
        except retry_errors as error:
            if isinstance(error, (FileNotFoundError, PermissionError)):
                raise  # Missing saves/files and denied access are not SSH loss.
            failure = error
            if on_retry is not None:
                on_retry(phase, error)
            phase = 'reconnect'
            remaining = deadline - time.monotonic()
            if remaining > 0:
                time.sleep(min(delay, remaining))


def command(client, text, timeout=45):
    transport = client.get_transport()
    if transport is None:
        raise ConnectionError('SSH transport is unavailable; command was not started or retried')
    peer = getattr(transport, 'getpeername', None)
    if peer is not None:
        require_access(peer()[0])
    transport.set_keepalive(5)
    # Channel negotiation also contends with Main and the VM on the board.
    # Give it the caller's bounded budget, without retrying command execution.
    deadline = time.monotonic() + timeout
    _, out, err = client.exec_command(text, timeout=timeout)
    channel = out.channel
    output, errors = bytearray(), bytearray()
    try:
        while True:
            if channel.recv_ready():
                output.extend(channel.recv(65536))
            if channel.recv_stderr_ready():
                errors.extend(channel.recv_stderr(65536))
            # SSH exit status can precede stdout EOF. Wait for EOF/close as well
            # so a delayed final data packet cannot turn a valid result empty.
            if (channel.eof_received or channel.closed) and channel.exit_status_ready() and not channel.recv_ready() and not channel.recv_stderr_ready():
                status = channel.recv_exit_status()
                if status == -1:
                    raise ConnectionError('SSH closed before reporting command exit status; execution was not retried')
                if status:
                    raise RuntimeError(errors.decode() or output.decode() or text)
                return output.decode()
            if time.monotonic() >= deadline:
                raise TimeoutError(dict(command=text, output=output.decode(), stderr=errors.decode(),
                                        seconds=timeout, note='Command was started once; it was not retried'))
            time.sleep(.01)
    finally:
        channel.close()
