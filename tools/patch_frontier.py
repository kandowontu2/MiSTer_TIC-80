"""Create the tested TIC-80 compatibility patch for a known Frontier daemon.

This only writes a local output; it never installs or restarts the daemon.
The preserve flag is for a controlled hot restart, avoiding boot-time .s0 cleanup.
"""
import argparse
import hashlib
from pathlib import Path

BASELINE = '36a7974999f0b327d0846e5369c9ee42b09b13025b3c7cfe2daf13cc3c241215'


def patch(payload):
    if hashlib.sha256(payload).hexdigest() != BASELINE:
        raise ValueError('Unrecognized Frontier daemon; review its source before patching')
    source = payload.decode('utf-8')
    marker = '    CUR=$(cat /tmp/CORENAME 2>/dev/null)\n'
    replacement = marker + '''    # Main truncates CORENAME before rewriting the same name on an RBF reload.
    # Give TIC-80's live supervisor the same bounded rewrite grace as its backend.
    if [ "$LAST" = "TIC-80" ] && [ -z "$CUR" ]; then
        sleep 0.05
        CUR=$(cat /tmp/CORENAME 2>/dev/null)
    fi
'''
    assert source.count(marker) == 1
    source = source.replace(marker, replacement)
    swap = '[ -n "$CUR_RBF" ] && [ -n "$LAST_RBF" ] && [ "$CUR_RBF" != "$LAST_RBF" ];'
    assert source.count(swap) == 1
    # TIC-80's supervisor owns same-console reloads. Main's argv can name MENU
    # before CORENAME updates; do not misclassify that interval as a sister core.
    source = source.replace(swap, '[ "$CUR" != "TIC-80" ] && ' + swap)
    sweep = 'discover_cores | while IFS= read -r core; do\n'
    sweep_end = 'done\n\n# ── Helper: aggressive child kill'
    assert source.count(sweep) == 2 and source.count(sweep_end) == 1
    source = source.replace(sweep, 'if [ "${TM_FRONTIER_PRESERVE_S0:-0}" != "1" ]; then\n' + sweep, 1)
    source = source.replace(sweep_end, 'done\nfi\n\n# ── Helper: aggressive child kill')
    start = 'for HANDLER_DIR in "$GAMES_ROOT"/*/_handler.sh; do\n'
    end = 'done\n\nLAST_RBF=""\n'
    assert source.count(start) == source.count(end) == 1
    source = source.replace(start, 'if [ "${TM_FRONTIER_PRESERVE_S0:-0}" != "1" ]; then\n' + start)
    source = source.replace(end, 'done\nfi\n\nLAST_RBF=""\n')
    return source.encode('utf-8')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('original', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    result = patch(a.original.read_bytes())
    a.output.write_bytes(result)
    print(hashlib.sha256(result).hexdigest())
