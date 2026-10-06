"""Honor a local, user-authorized hold on the shared MiSTer device."""
import json
from pathlib import Path

HOLD_FILE = Path(__file__).resolve().parents[1] / 'build/hardware-access-hold.json'


def require_access(host):
    try:
        hold = json.loads(HOLD_FILE.read_text())
    except FileNotFoundError:
        return
    if hold.get('active') and hold.get('host') == host:
        raise RuntimeError('MiSTer access is held: ' + hold['instruction'])
