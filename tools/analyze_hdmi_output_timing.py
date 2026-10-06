"""Validate complete, branch-specific conditional HDMI timing tables.

Positive slacks here assume the receiver limits and zero skew used by the
overlay. They never establish physical board timing qualification.
"""
import argparse
import csv
import io
import json
import math
from pathlib import Path

MASTERS = {
    'hdmi': 'pll_hdmi|pll_hdmi_inst|altera_pll_i|cyclonev_pll|counter[0].output_counter|divclk',
    'direct': 'pll_audio|pll_audio_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk',
}
PORTS = {'HDMI_TX_HS', 'HDMI_TX_VS', 'HDMI_TX_DE'} | {f'HDMI_TX_D[{n}]' for n in range(24)}


def analyze(text):
    rows = list(csv.DictReader(io.StringIO(text), delimiter='\t'))
    expected = {(model, str(temp), branch, check, port)
                for model in ('slow', 'fast') for temp in (-40, 100)
                for branch in MASTERS for check in ('setup', 'hold') for port in PORTS}
    seen = set()
    for row in rows:
        key = tuple(row[name] for name in ('model', 'temperature', 'branch', 'check', 'endpoint'))
        if key not in expected or key in seen:
            raise ValueError(f'Unexpected or duplicate path: {key}')
        seen.add(key)
        branch = row['branch']
        if row['launch_clock'] != MASTERS[branch] or row['capture_clock'] != 'tm_hdmi_forwarded_' + branch:
            raise ValueError(f'Clock branch mismatch: {key}')
        if row['inverted'] != '1':
            raise ValueError(f'Forwarded clock inversion missing: {key}')
        for name in ('period', 'slack', 'relationship', 'launch_time', 'latch_time', 'arrival', 'required'):
            row[name] = float(row[name])
            if not math.isfinite(row[name]):
                raise ValueError(f'Non-finite timing value: {key}, {name}')
        if row['period'] <= 0:
            raise ValueError(f'Invalid clock period: {key}')
        waveform = [float(x) for x in row['waveform'].split()]
        if len(waveform) != 2 or not all(math.isfinite(x) for x in waveform) or abs(waveform[0] - row['period']/2) > .002 or abs(waveform[1] - row['period']) > .002:
            raise ValueError(f'Wrong forwarded clock waveform: {key}')
        expected_relationship = row['period']/2 if row['check'] == 'setup' else -row['period']/2
        if abs(row['relationship'] - expected_relationship) > .002 or abs(row['latch_time'] - row['launch_time'] - row['relationship']) > .002:
            raise ValueError(f'Wrong half-cycle clock relationship: {key}')
        calculated = row['required'] - row['arrival']
        if row['check'] == 'hold':
            calculated = -calculated
        if abs(calculated - row['slack']) > .002:
            raise ValueError(f'Slack disagrees with arrival/required times: {key}')
    if seen != expected:
        raise ValueError(f'Missing {len(expected - seen)} HDMI timing paths')
    groups = {}
    for branch in MASTERS:
        branch_rows = [row for row in rows if row['branch'] == branch]
        periods = {row['period'] for row in branch_rows}
        if len(periods) != 1:
            raise ValueError(f'Clock period changes between corners: {branch}')
        groups[branch] = dict(period_ns=periods.pop(), checks={})
        for check in ('setup', 'hold'):
            group = [row for row in branch_rows if row['check'] == check]
            worst = min(group, key=lambda row: row['slack'])
            groups[branch]['checks'][check] = dict(minimum_slack_ns=worst['slack'],
                model=worst['model'], temperature=int(worst['temperature']), endpoint=worst['endpoint'],
                paths=len(group), violated_paths=sum(row['slack'] < 0 for row in group))
        # Additional data-minus-clock delay reduces setup and increases hold.
        setup = groups[branch]['checks']['setup']['minimum_slack_ns']
        hold = groups[branch]['checks']['hold']['minimum_slack_ns']
        groups[branch]['conditional_additional_data_minus_clock_delay_ns'] = dict(minimum=-hold, maximum=setup)
    return dict(paths=len(rows), branches=groups,
                conditional_zero_skew_checks_passed=all(row['slack'] >= 0 for row in rows),
                external_qualified=False,
                assumptions=dict(receiver_setup_ns=1.8, receiver_hold_ns=1.3,
                                 board_skew_ns=0, extra_receiver_clock_delay_ns=0))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('table', type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(args.table.read_text()), indent=2))
