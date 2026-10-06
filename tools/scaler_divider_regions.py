"""Extract actual scaler arithmetic while guarding all surrounding VHDL."""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT/'reference/pico8/fpga/sys/ascal.vhd'
BASELINE_SHA256 = '3f74c7b52d749cf523b11cbd06a9269f831ad86967ee983b4a0d1f5792b645f6'
DECLARATIONS = (
    '\t\tVARIABLE div_positive_v, div_negative_v : unsigned(20 DOWNTO 0);\n',
    '\t\tVARIABLE div_pp_v, div_pn_v, div_np_v, div_nn_v : unsigned(20 DOWNTO 0);\n',
)
MARKERS = (
    '\t\t\t-- Pipelined 8 bits non-restoring divider. Cycle 1\n',
    '\t\t\t-- Cycle 2\n',
    '\t\t\t-- Cycle 3\n',
)

def load_regions(candidate_path):
    assert hashlib.sha256(BASELINE.read_bytes()).hexdigest() == BASELINE_SHA256
    baseline = BASELINE.read_text()
    candidate = candidate_path.read_text()

    def split(text):
        for marker in MARKERS:
            assert text.count(marker) == 1, 'Divider region marker changed'
        prefix, rest = text.split(MARKERS[0])
        first, rest = rest.split(MARKERS[1])
        second, suffix = rest.split(MARKERS[2])
        return prefix, (first, second), suffix

    old_prefix, old_bodies, old_suffix = split(baseline)
    new_prefix, new_bodies, new_suffix = split(candidate)
    for declaration in DECLARATIONS:
        assert new_prefix.count(declaration) == 1, 'Temporary declaration changed'
        new_prefix = new_prefix.replace(declaration, '')
    assert old_prefix == new_prefix and old_suffix == new_suffix, \
        'Unexpected change outside first two divider stages'
    for bodies in (old_bodies, new_bodies):
        for stage, body in enumerate(bodies):
            writes = f'o_div({stage})<=div_v;\n\t\t\to_dir({stage})<=dir_v;'
            assert body.rstrip().endswith(writes), 'Pipeline register writes changed'
            assert body.count(f'o_div({stage})<=div_v;') == 1
            assert body.count(f'o_dir({stage})<=dir_v;') == 1
    return old_bodies, new_bodies

def stage_entity(name, body, stage, extra=''):
    body = body.replace(f'o_div({stage})<=div_v;', 'remainder <= div_v;')
    body = body.replace(f'o_dir({stage})<=dir_v;', 'direction <= dir_v;')
    if stage:
        body = body.replace('o_div(0)', 'remainder_in').replace('o_dir(0)', 'direction_in')
        inputs = 'remainder_in : in unsigned(20 downto 0);\n     direction_in : in unsigned(11 downto 0);'
    else:
        inputs = 'o_hacc : in natural range 0 to 16383;'
    assert 'o_div(' not in body and 'o_dir(' not in body, 'Unexpected register reference'
    return f'''library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity {name} is
port({inputs}
     o_hsize : in natural range 0 to 4095;
     remainder : out unsigned(20 downto 0);
     direction : out unsigned(11 downto 0));
end entity;
architecture rtl of {name} is begin
process(all)
variable div_v : unsigned(20 downto 0);
variable dir_v : unsigned(11 downto 0);
{extra}
begin
{body}
end process;
end architecture;
'''
