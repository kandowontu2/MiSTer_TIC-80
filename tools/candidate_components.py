"""Exact component hashes shared by development packaging and installation."""
import re

DESTINATIONS = {
    'MiSTer', '_Other/TIC80_20261003.rbf', 'games/TIC-80/TIC-80',
    'games/TIC-80/TIC-80-Studio', 'games/TIC-80/_handler.sh', 'games/TIC-80/cacert.pem',
}
LEGACY_PLAYER = 'acbd070c681f041680a158d5ad11224742f7ef6a26b793a70ac35d51c7359d02'
LEGACY_CA = 'a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505'


def qualified_hashes(receipt):
    explicit = {'player_sha256', 'cacert_sha256'} & receipt.keys()
    if explicit:
        assert explicit == {'player_sha256', 'cacert_sha256'}, 'Player and CA qualification hashes must be supplied together'
        assert receipt.get('bounded_player_playback_checks_passed') is True, 'Explicit player requires its own playback qualification'
        player, ca = receipt['player_sha256'], receipt['cacert_sha256']
    else:
        # Earlier receipts bind these two components to the dated SDK bundle.
        player, ca = LEGACY_PLAYER, LEGACY_CA
    answer = {
        'MiSTer': receipt['main_sha256'],
        '_Other/TIC80_20261003.rbf': receipt['fpga_sha256'],
        'games/TIC-80/TIC-80-Studio': receipt['studio_sha256'],
        'games/TIC-80/_handler.sh': receipt['handler_sha256'],
        'games/TIC-80/TIC-80': player,
        'games/TIC-80/cacert.pem': ca,
    }
    for name, digest in answer.items():
        assert isinstance(digest, str) and re.fullmatch(r'[0-9a-f]{64}', digest), 'Invalid qualified hash: ' + name
    return answer
