"""Check delayed replies, stderr failures and no duplicate command starts."""
import sys
import time
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from hardware_ssh import command, read_with_reconnect

class Channel:
    def __init__(self,mode):
        self.mode=mode;self.started=time.monotonic();self.closed=False
        self.output=[b'first ',b'second'] if mode=='good' else []
        self.errors=[b'failure details'] if mode=='error' else []
    @property
    def eof_received(self):
        return self.mode!='stall' and not self.output and not self.errors
    def recv_ready(self):return bool(self.output) and time.monotonic()-self.started>.02
    def recv_stderr_ready(self):return bool(self.errors)
    def recv(self,n):return self.output.pop(0)
    def recv_stderr(self,n):return self.errors.pop(0)
    def exit_status_ready(self):return self.mode!='stall'  # status precedes delayed output
    def recv_exit_status(self):return -1 if self.mode=='disconnect' else 2 if self.mode=='error' else 0
    def close(self):self.closed=True

class Client:
    def __init__(self,mode):self.calls=0;self.channel=Channel(mode)
    def get_transport(self):return self
    def set_keepalive(self,n):assert n==5
    def exec_command(self,text,timeout):
        self.open_timeout=timeout
        self.calls+=1;self.channel.channel=self.channel
        return None,self.channel,self.channel

c=Client('good');assert command(c,'read-only')=='first second'
assert c.calls==1 and c.channel.closed
assert c.open_timeout==45
c=Client('error')
try:command(c,'single mutation')
except RuntimeError as error:assert str(error)=='failure details'
else:raise AssertionError('Remote command failure accepted')
assert c.calls==1 and c.channel.closed
c=Client('stall')
try:command(c,'single mutation',timeout=.03)
except TimeoutError as error:assert 'not retried' in str(error)
else:raise AssertionError('Stalled reply accepted')
assert c.calls==1 and c.channel.closed
assert c.open_timeout==.03
c=Client('disconnect')
try:command(c,'single mutation')
except ConnectionError as error:assert 'not retried' in str(error)
else:raise AssertionError('Missing exit status was accepted')
assert c.calls==1 and c.channel.closed
c=Client('good')
c.get_transport=lambda:None
try:command(c,'single mutation')
except ConnectionError as error:assert 'not started or retried' in str(error)
else:raise AssertionError('Unavailable SSH transport was accepted')
assert c.calls==0

# A lost read followed by two failed connections must not dispatch reads on
# the disconnected client. The original command API still never retries.
events=[]
def read():
    events.append('read')
    if events.count('read')==1:raise ConnectionResetError('lost reply')
    return 'original monitor trace'
def reconnect():
    events.append('connect')
    if events.count('connect')<3:raise TimeoutError('network unavailable')
with patch('hardware_ssh.time.sleep'):
    result=read_with_reconnect(read,reconnect)
assert result=='original monitor trace'
assert events==['read','connect','connect','connect','read'],events

# Remote failures and holds are terminal, not reasons to redispatch a read.
for stage in ('read','connect'):
    events=[]
    def read():
        events.append('read')
        if stage=='read':raise RuntimeError('remote failure')
        raise ConnectionError('disconnect')
    def reconnect():
        events.append('connect');raise RuntimeError('access held')
    with patch('hardware_ssh.time.sleep'):
        try:read_with_reconnect(read,reconnect)
        except RuntimeError:pass
        else:raise AssertionError('Terminal failure was retried')
    assert events==(['read'] if stage=='read' else ['read','connect'])

# Stop opening connections when the budget expires; never restart work.
events=[];tick=[0]
def read():events.append('read');raise EOFError('lost channel')
def reconnect():events.append('connect');raise OSError('offline')
def sleep(seconds):tick[0]+=seconds
with patch('hardware_ssh.time.monotonic',side_effect=lambda:tick[0]),patch('hardware_ssh.time.sleep',side_effect=sleep):
    try:read_with_reconnect(read,reconnect,timeout=3,delay=1)
    except TimeoutError as error:assert 'no monitor or mutation was restarted' in str(error)
    else:raise AssertionError('Unbounded recovery accepted')
assert events==['read','connect','connect'],events
for timeout,delay in ((0,1),(1,0)):
    try:read_with_reconnect(read,reconnect,timeout=timeout,delay=delay)
    except ValueError:pass
    else:raise AssertionError('Invalid retry budget accepted')
for error in (FileNotFoundError('missing save'),PermissionError('access denied')):
    events=[]
    def read():events.append('read');raise error
    try:read_with_reconnect(read,reconnect)
    except type(error):pass
    else:raise AssertionError('File failure was retried')
    assert events==['read']
print('Hardware SSH: single dispatch, delayed replies, bounded read recovery and terminal-failure propagation passed')
