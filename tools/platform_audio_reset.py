"""Keep the audio override limited to a shared reset-release synchronizer."""
import re
import csv
import math

HELPER = '''// Shared platform audio reset: asynchronous assertion, three-edge release.
// Initial assertion also holds the downstream logic reset during startup.
module tic80_audio_reset (
    input clk, reset_async,
    output reset_audio
);
(* async_reg = "true", preserve *) reg [2:0] release_pipe = 3'b111;
always @(posedge clk or posedge reset_async) begin
    if (reset_async) release_pipe <= 3'b111;
    else release_pipe <= {release_pipe[1:0], 1'b0};
end
assign reset_audio = release_pipe[2];
endmodule
'''


def expected_override(pinned):
    anchor = 'localparam AUDIO_RATE = 48000;'
    if pinned.count(anchor) != 1:
        raise ValueError('Pinned audio anchor changed')
    modified = pinned.replace(anchor, '''wire reset_audio;
tic80_audio_reset reset_sync (
    .clk(clk), .reset_async(reset), .reset_audio(reset_audio)
);

''' + anchor)
    for before, after, count in (
        ('.reset(reset)', '.reset(reset_audio)', 2),
        ('.rst_i(reset)', '.rst_i(reset_audio)', 1),
        ('.RESET(reset)', '.RESET(reset_audio)', 2),
        ('always @(posedge clk, posedge reset)', 'always @(posedge clk, posedge reset_audio)', 1),
        ('if(reset) begin', 'if(reset_audio) begin', 1),
    ):
        if modified.count(before) != count:
            raise ValueError('Pinned reset consumer changed: ' + before)
        modified = modified.replace(before, after)
    return HELPER + modified


def check_override(pinned, actual):
    if actual != expected_override(pinned):
        raise ValueError('Audio override differs from the reviewed reset-only changes')
    return dict(reset_consumers=6, reset_reference_replacements=7, release_edges=3,
                shared_reset=True, unrelated_logic_unchanged=True)


def extract_helper(source):
    matches = re.findall(r'^module tic80_audio_reset\b.*?^endmodule\s*', source, re.M | re.S)
    if len(matches) != 1:
        raise ValueError('Expected one actual reset synchronizer module')
    return matches[0]


def check_consumer_table(path, expected_count, artifact_mtime_ns):
    """Require complete paired checks for every fitted final-stage endpoint."""
    if path.stat().st_mtime_ns < artifact_mtime_ns:
        raise ValueError('Stale audio reset consumer table')
    with path.open(encoding='utf-8', newline='') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        if reader.fieldnames != ['sink', 'check', 'launch_clock', 'latch_clock', 'slack_ns']:
            raise ValueError('Invalid audio reset consumer table header')
        sinks = {}
        margins = []
        clock = 'pll_audio|pll_audio_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk'
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError('Malformed audio reset consumer row')
            modes = sinks.setdefault(row['sink'], set())
            mode = row['check']
            if not row['sink'] or mode not in {'setup', 'hold', 'recovery', 'removal'} or mode in modes:
                raise ValueError('Invalid or duplicate audio reset consumer check')
            if row['launch_clock'] != clock or row['latch_clock'] != clock:
                raise ValueError('Audio reset consumer clock mismatch')
            margin = float(row['slack_ns'])
            if not math.isfinite(margin) or margin <= 0:
                raise ValueError('Nonpositive or nonfinite audio reset consumer margin')
            modes.add(mode)
            margins.append(margin)
    if len(sinks) != expected_count:
        raise ValueError('Incomplete audio reset consumer inventory')
    pairs = ({'setup', 'hold'}, {'recovery', 'removal'}, {'setup', 'hold', 'recovery', 'removal'})
    if any(modes not in pairs for modes in sinks.values()):
        raise ValueError('Unpaired audio reset consumer timing checks')
    return dict(consumers=len(sinks), checks=len(margins), minimum_slack_ns=min(margins))
