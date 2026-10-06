"""Clock-drift negatives and wrapped coherent counters for the hardware gate."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from analyze_video_clock import analyze


def rows(skew=0):
    # Sample every 197 ms across ten minutes, start near both 32-bit wraps,
    # and use a physical playback rate 17 ppm below nominal.
    result = []
    for i in range(3047):
        slots = 111 + int(i * .197 * 47999.184)
        heartbeat = (slots + int(i * .197 * skew)) // 800
        result.append(dict(session=7, slots=(0xfffffe00 + slots) & 0xffffffff,
                           heartbeat=(0xfffffff0 + heartbeat) & 0xffffffff))
    return result


good = analyze(rows())
assert good['phase_error_span_samples'] <= 799
for skew in (-4.8, 4.8, 80):  # +/-100 ppm and the old carrier's ~0.1 Hz excess.
    try:
        analyze(rows(skew))
    except AssertionError:
        pass
    else:
        raise AssertionError('Clock drift was accepted: ' + str(skew))
changed = rows()
changed[-1]['session'] += 1
try:
    analyze(changed)
except AssertionError:
    pass
else:
    raise AssertionError('A reset session was accepted')
print('Shared-clock phase stays bounded across wraps; +/-100 ppm drift and reset sessions rejected')
