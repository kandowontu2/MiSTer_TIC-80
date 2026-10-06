"""Measure buffered audio and clock drift from coherent hardware samples."""
import argparse
import hashlib
import json
from pathlib import Path

def analyze(payload):
    rows = [json.loads(line) for line in payload.decode().splitlines() if line.strip()]
    assert len(rows) >= 2 and len({r['session'] for r in rows}) == 1
    times = [(r['elapsed_ns'] - rows[0]['elapsed_ns']) / 1e9 for r in rows]
    assert all(b > a for a, b in zip(times, times[1:]))
    queued = [(r['written'] - r['played']) & 0xffffffff for r in rows]
    assert max(queued) <= 4096 + 256
    mean_time = sum(times) / len(times)
    mean_queue = sum(queued) / len(queued)
    slope = sum((t - mean_time) * (q - mean_queue) for t, q in zip(times, queued))
    slope /= sum((t - mean_time) ** 2 for t in times)
    duration = times[-1]
    clock = ((rows[-1]['slots'] - rows[0]['slots']) & 0xffffffff) / duration
    return dict(samples_sha256=hashlib.sha256(payload).hexdigest(), samples=len(rows),
                session=rows[0]['session'], seconds=duration, audio_clock_hz=clock,
                queued_frames_min=min(queued), queued_frames_max=max(queued),
                queued_frames_mean=mean_queue, queue_slope_frames_per_second=slope,
                fitted_queue_change_ms=slope * duration / clock * 1000)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('samples', type=Path)
    p.add_argument('--output', type=Path)
    a = p.parse_args()
    text = json.dumps(analyze(a.samples.read_bytes()), indent=2) + '\n'
    if a.output:
        a.output.write_text(text)
    print(text, end='')
