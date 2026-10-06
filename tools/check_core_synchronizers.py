"""Require calculated MTBF and explicit conservative rates for all custom chains."""
from pathlib import Path
import csv, math, re, sys

folder = Path(sys.argv[1])
for corner in ('slow--40', 'slow-100', 'fast--40', 'fast-100'):
    rows = list(csv.DictReader((folder / f'core-sync-{corner}.tsv').open(), delimiter='\t'))
    assert len(rows) == 116 and len({r['first_stage'] for r in rows}) == 116
    text = (folder / f'core-sync-{corner}-metastability.rpt').read_text()
    blocks = re.split(r'Synchronizer Chain #\d+:', text)[1:]
    found = {}
    predictions = []
    for block in blocks:
        match = re.search(r'; Synchronization Node\s*;\s*([^;]+);', block)
        if match: found[match.group(1).strip()] = block
    for row in rows:
        name = row['first_stage']
        assert name in found, ('Missing fitted MTBF chain', name)
        block = found[name]
        assert float(row['setup_slack_ns']) > 0 and float(row['hold_slack_ns']) > 0, name
        included = re.search(r'; Included in Design MTBF\s*;\s*([^;]+);', block).group(1).strip()
        worst = re.search(r'; Worst-Case MTBF \(years\)\s*;\s*([^;]+);', block).group(1).strip()
        method = re.search(r'; Method of Synchronizer Identification\s*;\s*([^;]+);', block).group(1).strip()
        assert included == 'Yes' and worst != 'Not Calculated' and method != 'Automatic', (name, included, worst, method)
        # Quartus 17 caps large estimates using this literal rather than a
        # numeric value. Preserve its meaning as a lower bound, never as an
        # exact prediction or an excuse to accept an uncalculated chain.
        years = 1e9 if worst == 'Greater than 1 Billion' else float(worst)
        assert math.isfinite(years) and years > 0, (name, worst)
        predictions.append(years)
        rate = float(re.search(r'; Data Toggle Rate Used in MTBF Calculation[^;]*;\s*([^;]+);', block).group(1).strip())
        expected = 131.0 if row['label'] in ('write_pointer', 'prefill', 'frame', 'bank', 'valid', 'vblank', 'consumed') else 25.0
        assert rate == expected, (name, rate, expected)
        source_section = block.split('; Source Clock')[1].split('; Synchronization Clock')[0]
        clocks = [float(m) for m in re.findall(r';\s*([\d.]+) MHz\s*;', source_section)]
        # Some chains list both the data clock and an asynchronous reset
        # source clock. Bound their combined activity rather than the maximum.
        assert clocks and sum(clocks) < expected, (name, clocks, expected)
        assert row['second_stage'] in block, name
    print(f'CORE_MTBF_PASS corner={corner} first_stages=116 explicitly_identified=1 included_in_MTBF=1 conservative_rates=1 minimum_MTBF_lower_bound_years={min(predictions):g}')
