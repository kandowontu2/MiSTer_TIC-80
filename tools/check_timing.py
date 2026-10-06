"""Fail the build when Quartus reports a timing violation, even if compile exits 0."""
import argparse
from pathlib import Path
import re
parser = argparse.ArgumentParser()
parser.add_argument("summary", type=Path)
args = parser.parse_args()
report = args.summary.read_text(encoding="utf-8-sig")
paths = re.findall(r"Type\s*:\s*([^\n]+)\s+Slack\s*:\s*([-+\d.]+)\s+TNS\s*:\s*([-+\d.]+)", report)
if not paths or not all(any(kind in name for name, _, _ in paths) for kind in ("Setup", "Hold", "Recovery", "Removal", "Minimum Pulse Width")):
    raise SystemExit("Timing report is missing required checks")
violations = [(name, slack, tns) for name, slack, tns in paths if float(slack) < 0 or float(tns) < 0]
if violations:
    for name, slack, tns in violations:
        print(f"VIOLATION: slack={slack}ns TNS={tns}ns {name}")
    raise SystemExit(1)
print(f"Passed {len(paths)} timing checks; minimum reported slack={min(float(s) for _, s, _ in paths):.3f}ns")
print("External I/O and CDC qualification must still be reviewed separately.")
