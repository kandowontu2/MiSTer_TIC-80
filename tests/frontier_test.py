"""Exercise actual patched dispatch reads, including persistent loss and MENU."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time

p = argparse.ArgumentParser()
p.add_argument('--daemon', type=Path, required=True)
a = p.parse_args()
source = a.daemon.read_text()
subprocess.run(['bash', '-n', str(a.daemon)], check=True)
read = source.split('while true; do\n', 1)[1].split('    # Also track', 1)[0]
dispatch = source.split('    # Core changed OR',1)[1].split('        # Kill the previous',1)[0]
dispatch = dispatch[dispatch.index('    if '):] + 'echo dispatch; fi\n'
for current,last,new,old,expected in [
    ('TIC-80','TIC-80','menu.rbf','TIC80.rbf',''),
    ('TIC-80','TIC-80','TIC80-new.rbf','TIC80.rbf',''),
    ('MENU','TIC-80','menu.rbf','TIC80.rbf','dispatch'),
    ('TIC-80','MENU','TIC80.rbf','menu.rbf','dispatch'),
    ('OpenBOR','OpenBOR','OpenBOR_7533.rbf','OpenBOR_4086.rbf','dispatch'),
]:
    result=subprocess.run(['bash','-c',dispatch],env=dict(os.environ,CUR=current,LAST=last,CUR_RBF=new,LAST_RBF=old),capture_output=True,text=True,check=True)
    assert result.stdout.strip()==expected,(current,last,new,old,result.stdout)
cleanup = 'if [ "${TM_FRONTIER_PRESERVE_S0:-0}"' + source.rsplit('if [ "${TM_FRONTIER_PRESERVE_S0:-0}"', 1)[1].split('LAST_RBF=""', 1)[0]
startup = 'if [ "${TM_FRONTIER_PRESERVE_S0:-0}"' + source.split('if [ "${TM_FRONTIER_PRESERVE_S0:-0}"', 1)[1].split('# ── Helper: aggressive child kill', 1)[0]
with tempfile.TemporaryDirectory(prefix='tic80-frontier-') as directory:
    root = Path(directory)
    name = root/'CORENAME'
    script = read.replace('/tmp/CORENAME', str(name)) + '\nprintf "[%s]" "$CUR"\n'
    for last, initial, rewritten, expected in [
        ('TIC-80', '', 'TIC-80', '[TIC-80]'),
        ('TIC-80', '', 'MENU', '[MENU]'),
        ('TIC-80', '', None, '[]'),
        ('TIC-80', 'MENU', None, '[MENU]'),
        ('Mugen', '', 'TIC-80', '[]'),
        ('TIC-80', 'TIC-80', None, '[TIC-80]'),
    ]:
        name.write_text(initial)
        # First cat deliberately sees the requested input; rewrite only after
        # that read completes, without depending on subprocess startup timing.
        mock = 'n=0; cat() { n=$((n+1)); command cat "$@"; touch "'+str(root/'read')+'"; }\n'
        # Command substitution isolates n, but the marker file is shared.
        marker = root/'read'
        marker.unlink(missing_ok=True)
        proc = subprocess.Popen(['bash','-c',mock+script], env=dict(os.environ,LAST=last),stdout=subprocess.PIPE)
        def rewrite():
            while not marker.exists() and proc.poll() is None:time.sleep(.001)
            time.sleep(.005)
            name.write_text(rewritten)
        writer = threading.Thread(target=rewrite) if rewritten is not None else None
        if writer:writer.start()
        out,_ = proc.communicate(timeout=1)
        if writer:writer.join()
        assert proc.returncode == 0 and out.decode() == expected, (last,initial,rewritten,out)
        print(last,repr(initial),'->',repr(rewritten),out.decode())
    games = root/'games'; config = root/'config'
    (games/'MACPLUS').mkdir(parents=True);config.mkdir()
    (games/'MACPLUS/_handler.sh').touch()
    saved = config/'MACPLUS.s0';saved.write_bytes(b'preserve existing mount')
    env=dict(os.environ,GAMES_ROOT=str(games),CONFIG_ROOT=str(config),TM_FRONTIER_PRESERVE_S0='1')
    swept = root/'swept'
    functions = 'log() { :; }; discover_cores() { echo called >> "'+str(swept)+'"; };\n'
    subprocess.run(['bash','-c',functions+startup],env=env,check=True)
    assert not swept.exists(), 'Hot restart must skip startup chmod/zombie sweeps'
    subprocess.run(['bash','-c','log() { :; };\n'+cleanup],env=env,check=True)
    assert saved.read_bytes()==b'preserve existing mount'
    env['TM_FRONTIER_PRESERVE_S0']='0'
    subprocess.run(['bash','-c',functions+startup],env=env,check=True)
    assert len(swept.read_text().splitlines())==3  # two sweeps plus their log enumeration
    subprocess.run(['bash','-c','log() { :; };\n'+cleanup],env=env,check=True)
    assert not saved.exists()
print('Frontier: transient rewrite, persistent loss, MENU, RBF/name skew, other-core behavior and hot-restart mount preservation passed')
