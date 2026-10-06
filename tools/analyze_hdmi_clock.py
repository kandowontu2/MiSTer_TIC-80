"""Check ADV7513 automatic CTS readback against the requested HDMI clock.

The programming guide defines F_TMDS = 128 * Fs * CTS / N. MiSTer's normal
24-bit RGB output has no pixel repetition, so TMDS and pixel clock match.
VIC detection and PLL lock alone cannot establish the requested refresh rate.
"""

def analyze(rows, pixel_clock_hz=74250000, audio_frequency_hz=48000, vic=4):
    assert len(rows) >= 2, 'Need repeated transmitter measurements'
    clocks = []
    for row in rows:
        regs = {key.lower(): int(value, 16) for key, value in row['registers'].items()}
        assert regs['0x9e'] & 16, 'HDMI transmitter PLL is not locked'
        assert regs['0x3e'] >> 2 == vic, 'Transmitter detects different video geometry'
        assert not regs['0x0a'] & 128, 'Automatic CTS is not selected'
        assert not regs['0x9d'] & 12, 'Input pixel clock division is enabled'
        n = ((regs['0x01'] & 15) << 16) | (regs['0x02'] << 8) | regs['0x03']
        cts = ((regs['0x04'] & 15) << 16) | (regs['0x05'] << 8) | regs['0x06']
        assert n and cts, 'Missing audio clock regeneration counters'
        clocks.append(128 * audio_frequency_hz * cts / n)
    result = dict(samples=len(rows), requested_pixel_clock_hz=pixel_clock_hz,
                  inferred_pixel_clock_min_hz=min(clocks), inferred_pixel_clock_max_hz=max(clocks),
                  maximum_error_ppm=max(abs(clock / pixel_clock_hz - 1) * 1e6 for clock in clocks))
    # Board oscillators and sequential byte reads can introduce small errors.
    # A 0.1% bound still rejects the observed 120 Hz / 148.5 MHz failure.
    assert result['maximum_error_ppm'] <= 1000, result
    return result


if __name__ == '__main__':
    import argparse
    import json
    from pathlib import Path
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('comparison', type=Path)
    parser.add_argument('--stage', required=True)
    args = parser.parse_args()
    print(json.dumps(analyze(json.loads(args.comparison.read_text())[args.stage]['rows']), indent=2))
