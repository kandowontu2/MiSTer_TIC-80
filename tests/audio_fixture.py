"""Model the FPGA's 48 kHz DAC instead of instantly draining the audio ring."""
import time


class AudioClock:
    def __init__(self):
        self.reset()

    def reset(self):
        self.played = 0
        self.phase = 0
        self.first = None
        self.running = False
        self.slots = 0
        self.underruns = 0
        self.last = time.monotonic_ns()

    def sample(self, written):
        now = time.monotonic_ns()
        queued = (written - self.played) & 0xffffffff
        if self.first is None and queued:
            self.first = now
        if not self.running:
            self.running = queued >= 1600 or (self.first is not None and now - self.first >= 50000000)
        else:
            self.phase += (now - self.last) * 48000
            slots, self.phase = divmod(self.phase, 1000000000)
            self.slots += slots
            self.underruns += max(0, slots - queued)
            self.played = (self.played + min(slots, queued)) & 0xffffffff
        self.last = now
        return self.played
