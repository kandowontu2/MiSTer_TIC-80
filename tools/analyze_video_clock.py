"""Check raster/playback alignment independently of the monitor's wall clock."""
import json


def analyze(rows):
    assert len(rows) >= 2 and len({r['session'] for r in rows}) == 1
    first = rows[0]
    errors = []
    for row in rows:
        frames = (row['heartbeat'] - first['heartbeat']) & 0xffffffff
        slots = (row['slots'] - first['slots']) & 0xffffffff
        errors.append(frames * 800 - slots)
    # At an arbitrary endpoint, up to 799 samples can remain before the next
    # raster boundary. Two extra slots cover the monitor's synchronized audio
    # counter latency. A constant phase offset cancels against the first row.
    result = dict(phase_error_min_samples=min(errors),
                  phase_error_max_samples=max(errors),
                  phase_error_span_samples=max(errors) - min(errors),
                  final_phase_error_samples=errors[-1])
    assert max(abs(value) for value in errors) <= 802, result
    assert result['phase_error_span_samples'] <= 802, result
    return result


if __name__ == '__main__':
    import argparse
    from pathlib import Path
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('samples', type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze([json.loads(line) for line in args.samples.read_text().splitlines()]), indent=2))
