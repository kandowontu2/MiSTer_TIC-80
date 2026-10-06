"""Capture an unmodified Studio startup with a modeled DDR/clock transport.

This is an actual frontend render, not an FPGA or physical HDMI capture.
"""
import argparse
import ctypes
import hashlib
import json
import mmap
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from audio_fixture import AudioClock


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--frontend", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()
    protocol = json.loads((ROOT / "protocol/memory_map.json").read_text())
    offsets = protocol["offsets"]
    with tempfile.TemporaryDirectory(prefix="tic80-console-capture-") as d:
        folder = Path(d)
        core = folder / "CORENAME"; core.write_text("TIC-80\n")
        memory = folder / "ddr"
        with memory.open("w+b") as f:
            f.truncate(protocol["region_bytes"])
            with mmap.mmap(f.fileno(), protocol["region_bytes"]) as shared:
                def get(name): return struct.unpack_from("<I", shared, offsets[name])[0]
                def put(name, value): ctypes.c_uint32.from_buffer(shared, offsets[name]).value = value & 0xffffffff
                put("IDENTITY", protocol["magic"]); put("GEOMETRY", protocol["width"] | protocol["height"] << 16)
                (folder / "studio").mkdir(); (folder / "saves").mkdir()
                process = subprocess.Popen([str(a.frontend), "--folder", str(folder / "studio"),
                    "--saves", str(folder / "saves"), "--memory", str(memory), "--core-name", str(core)],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                clock = AudioClock(); session = presented = count = 0
                try:
                    end = time.monotonic() + 15
                    while count < 60:
                        assert process.poll() is None, "Frontend exited before capture"
                        assert time.monotonic() < end, "Capture timed out"
                        put("HEARTBEAT", get("HEARTBEAT") + 1)
                        request = get("SESSION_REQUEST")
                        if request != session:
                            session = request; presented = 0; clock.reset()
                            put("VIDEO_PRESENTED", 0); put("AUDIO_READ", 0); put("SESSION_ACK", session)
                        put("AUDIO_READ", clock.sample(get("AUDIO_WRITE")))
                        pub = get("VIDEO_PUBLISH")
                        if session and pub & 2 and pub != presented:
                            at = offsets["BUFFER1"] if pub & 1 else offsets["BUFFER0"]
                            picture = bytes(shared[at:at + protocol["frame_bytes"]])
                            assert all(x == 255 for x in picture[3::4])
                            put("VIDEO_PRESENTED", pub); presented = pub; count += 1
                        time.sleep(.001)
                    Image.frombytes("RGBA", (256, 144), picture).save(a.output)
                    core.write_text("MENU\n")
                    output = process.communicate(timeout=10)[0].decode()
                    assert process.returncode == 0 and "departed=1 error=0" in output, output
                    print(json.dumps({"source": "Actual desktop Studio render with modeled DDR transport",
                        "frontend_sha256": hashlib.sha256(a.frontend.read_bytes()).hexdigest(),
                        "capture_sha256": hashlib.sha256(a.output.read_bytes()).hexdigest(), "frames": count}))
                finally:
                    if process.poll() is None:
                        process.kill(); process.communicate()


if __name__ == "__main__":
    main()
