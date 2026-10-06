"""Compare every live pixel with a full-color, all-levels raster fixture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import time
import paramiko
from hardware_ssh import connect
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--host', required=True)
p.add_argument('--format', choices=('native','png'), default='native')
a = p.parse_args()
c = paramiko.SSHClient()
c.load_system_host_keys()
if (ROOT/'build/ssh_known_hosts').exists():
    c.load_host_keys(str(ROOT/'build/ssh_known_hosts'))
connect(c,a.host,username='root',password=os.environ['TM_SSH_PASSWORD'],timeout=10)


def command(text):
    _,out,err=c.exec_command(text,timeout=15)
    output,errors=out.read().decode(),err.read().decode()
    if out.channel.recv_exit_status(): raise RuntimeError(errors or output or text)
    return output


def guard():
    if command('cat /tmp/CORENAME').strip()!='TIC-80': raise RuntimeError('Another core selected')


try:
    guard()
    hashes={}
    for name,local,remote in [('player','build/arm/tic80-live','/media/fat/games/TIC-80/TIC-80'),
                             ('rbf','build/fpga/output_files/TIC80.rbf','/media/fat/_Other/TIC80_20260930.rbf')]:
        hashes[name]=hashlib.sha256((ROOT/local).read_bytes()).hexdigest()
        assert command('sha256sum '+remote).split()[0]==hashes[name]
    folder=ROOT/'build/color'
    raw=(folder/'precision.rgba').read_bytes()
    assert len(raw)==256*144*4
    expected=Image.frombytes('RGBA',(256,144),raw).convert('RGB').tobytes()
    assert all(set(expected[channel::3])==set(range(256)) for channel in range(3))
    # Confirm that this fixture can distinguish the former quantized pipeline.
    quantized=bytearray()
    for offset in range(0,len(expected),3):
        r,g,b=expected[offset:offset+3]
        quantized.extend(((r&248)|(r>>5),(g&252)|(g>>6),(b&248)|(b>>5)))
    old_mismatches=sum(x!=y for x,y in zip(expected,quantized))
    assert old_mismatches>10000
    extension='.tic' if a.format=='native' else '.png'
    cart=folder/('precision'+extension)
    marker=f'Cartridge loaded: {cart.stat().st_size} bytes; reset=0'
    remote='/tmp/tic80-mister-dev/color/precision'
    command('mkdir -p /tmp/tic80-mister-dev/color')
    with c.open_sftp() as sftp:
        sftp.put(str(cart),remote+extension)
        with sftp.open(remote+'.mgl','w') as file:
            file.write('<mistergamedescription>\n <rbf>_Other/TIC80</rbf>\n'
                       f' <file delay="3" type="f" index="0" path="{remote}{extension}"/>\n'
                       '</mistergamedescription>\n')
        before=command('cat /media/fat/logs/TIC-80/tic80.log')
        guard()
        command('test "$(cat /tmp/CORENAME)" = TIC-80 || exit 30; '
                f'printf "load_core {remote}.mgl\\n" > /dev/MiSTer_cmd')
        deadline=time.monotonic()+35
        while True:
            guard()
            log=command('cat /media/fat/logs/TIC-80/tic80.log')
            if log.count(marker)>before.count(marker): break
            if time.monotonic()>deadline: raise RuntimeError(log)
            time.sleep(.2)
        time.sleep(2)
        guard()
        log=command('cat /media/fat/logs/TIC-80/tic80.log')
        assert all(word not in log for word in ('Cartridge failed','Cartridge rejected','TIC-80:','Save error')),log
        screenshots='/media/fat/screenshots/TIC-80'
        before=set(sftp.listdir(screenshots))
        command('test "$(cat /tmp/CORENAME)" = TIC-80 || exit 30; printf "screenshot\\n" > /dev/MiSTer_cmd')
        deadline=time.monotonic()+10
        capture=folder/('hardware-'+a.format+'.png')
        while True:
            guard()
            new=sorted(name for name in set(sftp.listdir(screenshots))-before if name.endswith('.png'))
            if new:
                sftp.get(screenshots+'/'+new[-1],str(capture))
                if capture.read_bytes().endswith(b'\x00\x00\x00\x00IEND\xaeB\x60\x82'): break
            if time.monotonic()>deadline: raise RuntimeError('Screenshot did not finish')
            time.sleep(.2)
    image=Image.open(capture).convert('RGB')
    assert image.size==(256,144)
    actual=image.tobytes()
    mismatches=sum(x!=y for x,y in zip(expected,actual))
    result=dict(format=a.format,player_sha256=hashes['player'],rbf_sha256=hashes['rbf'],
                mismatching_channels=mismatches,former_rgb565_mismatches=old_mismatches,
                channel_levels=[len(set(actual[channel::3])) for channel in range(3)],
                capture=str(capture.relative_to(ROOT)))
    (folder/('result-'+a.format+'.json')).write_text(json.dumps(result,indent=2)+'\n')
    assert mismatches==0,result
    print('Every live RGB byte matches the runtime raster: '+json.dumps(result))
finally:
    c.close()
