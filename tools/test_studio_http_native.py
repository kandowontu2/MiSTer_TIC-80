"""Exercise Studio HTTP on MiSTer's curl, including HTTPS catalogue/cart reads."""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import select
import socket
import sys
import threading
import paramiko

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools")); sys.path.insert(0,str(ROOT/"tests"))
from hardware_access import require_access
from hardware_ssh import command, connect
from studio_net_test import server

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    host="192.168.1.176"; require_access(host)
    fixture=ROOT/"build/arm/studio_net_test"; digest=sha(fixture)
    expected=json.loads((ROOT/"build/video-clock-installed.json").read_text())
    paths=["CMakeLists.txt","cmake/studio.cmake","src/studio_net.c","src/studio_rom.c",
           "src/studio_system.c","include/tic80_mister/studio.h","tests/studio_net_test.c","tests/studio_net_test.py","assets/cacert.pem"]
    sources={p:sha(ROOT/p) for p in paths}
    client=paramiko.SSHClient(); client.load_system_host_keys()
    connect(client,host,username="root",password=os.environ["TM_SSH_PASSWORD"],timeout=10)
    http=server(); threads=[]; port=None
    def forward(channel, origin, destination):
        def bridge():
            sock=None
            try:
                sock=socket.create_connection(("127.0.0.1",http.server_port),timeout=5)
                while True:
                    readable,_,_=select.select([channel,sock],[],[],5)
                    if not readable: continue
                    for stream in readable:
                        data=stream.recv(65536)
                        if not data: return
                        (sock if stream is channel else channel).sendall(data)
            except (OSError,EOFError,ValueError): pass
            finally:
                channel.close()
                if sock: sock.close()
        thread=threading.Thread(target=bridge,daemon=True); threads.append(thread); thread.start()
    try:
        run=lambda text:command(client,text,timeout=55)
        supervisor=lambda:run('for p in $(pidof TIC-80); do test "$(cat /proc/$p/comm 2>/dev/null)" = TIC-80 && printf "%s " "$p"; done; true').strip()
        core=run("cat /tmp/CORENAME").strip(); assert core in ("MENU","TIC-80")
        parent=supervisor()
        for path,want in expected.items(): assert run("sha256sum "+path).split()[0]==want
        remote="/tmp/tic80-mister-dev/studio-http-"+digest[:12]
        ca=remote+"-cacert.pem"
        with client.open_sftp() as sftp:
            sftp.put(str(fixture),remote)
            sftp.put(str(ROOT/"assets/cacert.pem"),ca)
        assert run("sha256sum "+remote).split()[0]==digest
        assert run("sha256sum "+ca).split()[0]==sources["assets/cacert.pem"]
        run('test "$(cat /tmp/CORENAME)" = '+core+" && chmod +x "+remote)
        port=client.get_transport().request_port_forward("127.0.0.1",0,handler=forward)
        prefix='test "$(cat /tmp/CORENAME)" = '+core+" && nice -n 19 env NO_PROXY=127.0.0.1 no_proxy=127.0.0.1 TIC80_CA_BUNDLE="+ca+" taskset 1 "+remote
        local=run(prefix+f" http://127.0.0.1:{port}")
        assert "descriptor isolation and cleanup passed" in local
        version_output=run(prefix+" https://dev.tic80.com '/json?fn=version'")
        version=json.loads(version_output.split("\n",1)[1]); assert version["major"]==1
        directory_output=run(prefix+" https://dev.tic80.com '/json?fn=dir&path='")
        directory=json.loads(directory_output.split("\n",1)[1]); assert directory["folders"] and directory["files"]
        cart=next(entry for entry in directory["files"] if entry["filename"]=="bouncy_ball.tic")
        # Catalogue strings are untrusted; permit only the known fixture filename
        # and a hexadecimal hash before forming the shell's literal URL path.
        assert cart["hash"] and all(ch in "0123456789abcdef" for ch in cart["hash"])
        path="/cart/"+cart["hash"]+"/bouncy_ball.tic"; downloaded=remote+".tic"
        cart_output=run(prefix+" https://dev.tic80.com "+path+" "+downloaded)
        assert "Downloaded cartridge passes bounded decoding" in cart_output
        cart_hash=run("sha256sum "+downloaded).split()[0]
        assert run("cat /tmp/CORENAME").strip()==core and supervisor()==parent
        for path,want in expected.items(): assert run("sha256sum "+path).split()[0]==want
        assert sources=={p:sha(ROOT/p) for p in paths}
        record=dict(tested_at=datetime.now(timezone.utc).isoformat(),fixture_sha256=digest,sources=sources,
                    baseline_hashes=expected,core=core,player_pid=parent,contract_output=local,
                    public_version=version,catalogue=directory,downloaded_cart=cart,
                    downloaded_sha256=cart_hash,download_output=cart_output,
                    scope="Offscreen native ARM asynchronous HTTP/FS failure and cleanup tests plus real HTTPS version/catalogue/cart reads. Selected core and player preserved; no displayed SURF or execution of downloaded public cartridge.")
        (ROOT/"build/studio-http-native-arm.json").write_text(json.dumps(record,indent=2)+"\n")
        (ROOT/"build/studio-http-native-arm.log").write_text(local+version_output+directory_output+cart_output)
        print("Native HTTP/FS contract and public HTTPS catalogue/cart download passed; selected core and player preserved")
    finally:
        if port is not None: client.get_transport().cancel_port_forward("127.0.0.1",port)
        client.close(); http.shutdown(); http.server_close()
        for thread in threads: thread.join(timeout=.1)

if __name__=="__main__": main()
