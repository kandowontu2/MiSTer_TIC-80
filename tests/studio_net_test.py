"""Deterministic HTTP fixtures for the Studio backend, including ARM QEMU."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
import subprocess
import sys
import threading
import time
import json
import struct

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        try:
            if self.path.startswith("/json?fn=dir"):
                data=json.dumps(dict(folders=[dict(name="games")],files=[dict(name="Network fixture",hash="test",filename="fixture.tic",id=7)])).encode()
                self.send_response(200); self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)
                return
            if self.path.startswith("/cart/"):
                if self.path in ("/cart/test/fixture.tic","/cart/broken/malformed.tic"):
                    code=b"function TIC() cls(6) end\n"
                    data=bytes([5])+struct.pack("<H",len(code))+b"\0"+code if self.path=="/cart/test/fixture.tic" else b"broken"
                    self.send_response(200); self.send_header("Content-Length",str(len(data))); self.end_headers(); self.wfile.write(data)
                else:
                    self.send_response(404); self.end_headers()
                return
            if self.path.startswith("/name") and self.path != "/name%20space/%23%25%C3%A9.tic":
                self.send_response(400)
                self.end_headers()
                return
            if self.path == "/slow":
                time.sleep(2)
            if self.path == "/timeout":
                time.sleep(17)
            if self.path in ("/redirect", "/file-redirect"):
                self.send_response(302)
                self.send_header("Location", "/binary" if self.path == "/redirect" else "file:///etc/passwd")
                self.end_headers()
                return
            if self.path == "/missing":
                self.send_response(404)
                self.end_headers()
                return
            self.send_response(200)
            if self.path == "/oversize":
                self.send_header("Content-Length", str(8*1024*1024+1))
                self.end_headers()
                return
            if self.path == "/truncated":
                self.send_header("Content-Length", "100")
                self.end_headers()
                self.wfile.write(b"short")
                return
            if self.path == "/stream-oversize":
                self.end_headers()
                for _ in range(129):
                    self.wfile.write(b"a"*65536)
                return
            data = b"" if self.path == "/empty" else b"TIC\0\n200\xff"
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

def server():
    http = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=http.serve_forever, daemon=True)
    thread.start()
    return http

if __name__ == "__main__":
    http = server()
    try:
        env = os.environ.copy()
        env["NO_PROXY"] = env["no_proxy"] = "127.0.0.1"
        subprocess.run([*sys.argv[1:], f"http://127.0.0.1:{http.server_port}"], check=True, env=env, timeout=45)
    finally:
        http.shutdown()
        http.server_close()
