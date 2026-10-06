"""Conditional I2S pin timing from proven edge spacing and fitted delay bounds.

The ADV7513 Rev. B requires 2 ns setup/hold for I2S and LRCLK. This conservative
interval calculation assumes rising-edge capture, the fitted clock period,
zero PCB skew and zero additional relative edge uncertainty. It is not physical
qualification, nor a replacement for reset CDC or signal-integrity checks.
"""
import argparse
import csv
import io
import json
import math
from pathlib import Path

AUDIO_CLOCK = 'pll_audio|pll_audio_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk'
PINS = ('HDMI_SCLK', 'HDMI_I2S', 'HDMI_LRCLK')
CORNERS = {(model, temperature) for model in ('slow', 'fast') for temperature in (-40, 100)}


def finite(value):
    number = float(value)
    if not math.isfinite(number):
        raise ValueError('Non-finite timing value')
    return number


def pin_bounds(text):
    section = None
    bounds = {}
    for line in text.splitlines():
        heading = line.strip().strip(';').strip()
        if heading in ('Clock to Output Times', 'Minimum Clock to Output Times'):
            section = 'maximum' if heading == 'Clock to Output Times' else 'minimum'
            continue
        if heading in ('Setup Times', 'Hold Times'):
            section = None
        fields = [field.strip() for field in line.split(';')[1:-1]]
        if len(fields) != 6 or fields[0] not in PINS:
            continue
        pin, port, rise, fall, edge, clock = fields
        if section is None or port != 'FPGA_CLK3_50' or edge != 'Rise' or clock != AUDIO_CLOCK:
            raise ValueError('Unexpected serializer launch clock or pin table: ' + pin)
        key = (pin, section)
        if key in bounds:
            raise ValueError('Duplicate serializer delay bound: ' + str(key))
        bounds[key] = dict(rise_ns=finite(rise), fall_ns=finite(fall))
    if set(bounds) != {(pin, section) for pin in PINS for section in ('minimum', 'maximum')}:
        raise ValueError('Incomplete serializer pin-delay bounds')
    for pin in PINS:
        for polarity in ('rise_ns', 'fall_ns'):
            if bounds[pin, 'minimum'][polarity] > bounds[pin, 'maximum'][polarity]:
                raise ValueError('Minimum delay exceeds maximum: ' + pin)
    return bounds


def analyze(reports, clock_table, proof):
    if set(reports) != CORNERS:
        raise ValueError('Missing or unexpected FPGA corners')
    if not (proof.get('passed') and proof.get('unbounded_proof') and proof.get('platform_enable_included')):
        raise ValueError('Exact platform-enable unbounded proof required')
    setup_cycles = proof.get('minimum_change_to_bclk_rise_base_cycles')
    hold_cycles = proof.get('minimum_bclk_rise_to_change_base_cycles')
    if (setup_cycles, hold_cycles) != (3, 1):
        raise ValueError('Unexpected digital edge-bound proof contract')
    periods = {}
    for row in csv.DictReader(io.StringIO(clock_table), delimiter='\t'):
        corner = row['model'], int(row['temperature'])
        if corner not in CORNERS or corner in periods or row['clock'] != AUDIO_CLOCK:
            raise ValueError('Unexpected or duplicate serializer clock corner')
        periods[corner] = finite(row['period_ns'])
        if periods[corner] <= 0:
            raise ValueError('Invalid serializer clock period')
    if set(periods) != CORNERS or len(set(periods.values())) != 1:
        raise ValueError('Incomplete or inconsistent serializer clock periods')
    checks = []
    for corner in sorted(CORNERS):
        bounds = pin_bounds(reports[corner])
        clock_min = bounds['HDMI_SCLK', 'minimum']['rise_ns']
        clock_max = bounds['HDMI_SCLK', 'maximum']['rise_ns']
        for pin in ('HDMI_I2S', 'HDMI_LRCLK'):
            data_min = min(bounds[pin, 'minimum'].values())
            data_max = max(bounds[pin, 'maximum'].values())
            # Latest possible prior data transition vs earliest capture edge.
            setup = setup_cycles * periods[corner] + clock_min - data_max - 2.0
            # Earliest possible next data transition vs latest capture edge.
            hold = hold_cycles * periods[corner] + data_min - clock_max - 2.0
            checks.append(dict(model=corner[0], temperature=corner[1], pin=pin,
                period_ns=periods[corner], clock_rise_min_ns=clock_min, clock_rise_max_ns=clock_max,
                data_transition_min_ns=data_min, data_transition_max_ns=data_max,
                setup_slack_ns=setup, hold_slack_ns=hold))
    return dict(checks=checks, conditional_pin_bound_analysis_passed=all(
        row['setup_slack_ns'] >= 0 and row['hold_slack_ns'] >= 0 for row in checks),
        minimum_setup_slack_ns=min(row['setup_slack_ns'] for row in checks),
        minimum_hold_slack_ns=min(row['hold_slack_ns'] for row in checks),
        external_qualified=False, hardware_accessed=False,
        assumptions=dict(capture_edge='rising', receiver_setup_ns=2.0, receiver_hold_ns=2.0,
            board_data_minus_clock_skew_ns=0, additional_relative_edge_uncertainty_ns=0,
            clock_period='Fixed period from this fitted project; physical drift/jitter unqualified'),
        scope='Data/LRCLK setup and hold intervals; excludes electrical startup, duty cycle and reset CDC',
        receiver_source='https://www.analog.com/media/en/technical-documentation/data-sheets/ADV7513.pdf')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reports', type=Path)
    parser.add_argument('--proof', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(analyze({corner: (args.reports / f'rawio-{corner[0]}-{corner[1]}.rpt').read_text()
        for corner in CORNERS}, (args.reports / 'clock-periods.tsv').read_text(),
        json.loads(args.proof.read_text())), indent=2))
