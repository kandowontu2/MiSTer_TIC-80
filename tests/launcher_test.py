"""Run the actual launch handler in an isolated SD layout with spaces."""
import json,os,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tic80-launcher-') as temp:
    sd=Path(temp)/'SD card'; game=sd/'games/TIC-80'; game.mkdir(parents=True)
    handler=game/'_handler.sh'; shutil.copyfile(ROOT/'games/TIC-80/_handler.sh',handler)
    capture=sd/'argv'; env=dict(os.environ,TM_TEST_CAPTURE=str(capture))
    for name in ('TIC-80-Studio','TIC-80'):
        path=game/name
        path.write_text('#!/bin/sh\nprintf "%s\\0" "$0" "$@" > "$TM_TEST_CAPTURE"\nprintf "frontend:%s\\n" "$0"\nexit "${TM_TEST_STATUS:-0}"\n')
        path.chmod(0o755)
    subprocess.run(['sh','-n',str(handler)],check=True)
    def run(expected):
        p=subprocess.run(['sh',str(handler)],env=env,capture_output=True,text=True)
        assert p.returncode==expected,(p.stdout,p.stderr,p.returncode)
        return p
    run(0)
    args=capture.read_bytes().rstrip(b'\0').decode().split('\0')
    assert args==[str(game/'TIC-80-Studio'),'--folder',str(game/'Carts'),'--saves',str(sd/'saves/TIC-80')],args
    first=(sd/'logs/TIC-80/tic80.log').read_bytes()
    (game/'frontend.txt').write_text('player\n')
    run(0)
    assert (sd/'logs/TIC-80/tic80.prev.log').read_bytes()==first
    args=capture.read_bytes().rstrip(b'\0').decode().split('\0')
    assert args==[str(game/'TIC-80'),'--serve',str(sd/'saves/TIC-80')],args
    env['TM_TEST_STATUS']='7'; run(7)
    old=capture.read_bytes(); log=(sd/'logs/TIC-80/tic80.log').read_bytes()
    (game/'frontend.txt').write_text('unknown\n')
    assert 'studio or player' in run(2).stderr
    assert capture.read_bytes()==old and (sd/'logs/TIC-80/tic80.log').read_bytes()==log
    (game/'frontend.txt').write_text('studio\n'); (game/'TIC-80-Studio').unlink()
    assert 'missing executable' in run(1).stderr
    assert capture.read_bytes()==old
    print('Actual launcher: full console default, explicit player mode, quoted SD paths, log rotation, exit status and invalid/missing frontend handling pass')
