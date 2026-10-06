# HDMI external timing investigation

October 4, 2026. This records receiver requirements and the active configuration;
it does not qualify external I/O timing.

The read-only board observations in
`build/hdmi-board-io-readonly-20261004.json` identify Main's open I2C handles on
bus 1. Reading the two configuration registers at ADV7513 address `0x39`
returns `0x0B=0x0E` and `0xBA=0x60`. The before/after core is TIC-80 and Main's
SHA-256 remains `13d91917cc10f65b75743d1946255fa222a5ced0d6819b63623cabec28700e5b`.
No configuration registers or installed files were written. The separate
register receipt is `build/hdmi-active-registers-readonly-20261004.json`.

These readings match `reference/main/video.cpp`, lines 1434 and 1590.
Register `0x0B[6]=0` selects rising-edge I2S capture, as shown in Figure 3 of
the [ADV7513 hardware guide](https://www.analog.com/media/en/technical-documentation/user-guides/ADV7513_Hardware_User_Guide.pdf).
Main interprets `0xBA[7:5]=011` as no input clock delay. The older guide's
video setup/hold values were measured with the default `000` delay setting;
Analog Devices documents that distinction in its
[timing clarification](https://ez.analog.com/video/f/q-a/10535/adv7513-input-video-data-timings).

Use the [Rev. B data sheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ADV7513.pdf),
pages 3–4, for receiver requirements: video setup 1.8 ns and hold 1.3 ns;
I2S and LRCLK setup/hold 2 ns; SDA setup/hold 100 ns at up to 400 kHz SCL.
The data sheet specifies the voltage threshold and programmable clock/data
relationship. These are chip requirements; they do not supply board trace skew.

The staged platform source `sys/sys_top.v`, lines 1263–1291, selects the
HDMI PLL or direct-video clock and forwards an inverted clock through
`altddio_out` (`datain_h=0`, `datain_l=1`). HDMI video outputs register on the
internal rising edge, so the receiver's forwarded rising edge corresponds to
the internal falling edge. A timing model must preserve that relationship
and account for both clock selections. Constraining only the observed 720p
clock would not qualify other supported modes.

The I2S implementation changes data/LRCLK when its old `msclk` is high and
registers `sclk` from that signal on each audio clock. Its edge relationship
must be modeled from the actual implementation rather than treating SCLK as
an unrelated ideal clock. HDMI MCLK is the shared audio clock.

Still required: receiver timing and board skew for each applicable output,
correct generated-clock modeling through the clock switch and forwarded
clock, four-corner setup/hold analysis, and review of the other unconstrained
I2C, SD, LED and user I/O endpoints. Production timing constraints remain
unchanged. The conditional copied-project overlay below adds output delays only
to an in-memory analysis netlist. Human picture/audio confirmation remains
useful behavioral evidence, separate from external timing qualification.

## Fitted package-pin inventory

The latest second-stage divider RBF is now bound to four fresh `report_datasheet`
reports at slow/fast −40/100 °C. The inventory retains maximum and minimum
clock-to-output values for all 24 HDMI data pins, control pins, forwarded clock
and audio pins. Video rows contain both the HDMI-PLL and direct-video clock
branches; they must remain associated with their own clock reference. The
forwarded clock's rising transition is reported under the internal falling
edge, consistent with `datain_h=0`, `datain_l=1`. A blank opposite-polarity field
does not mean the clock path is absent.

Raw TCO is not external setup/hold slack. Clock phase, active branch, receiver
configuration and board clock/data skew still need a verified model. No output
delays, false paths or generated clocks were added during this extraction.
Evidence is sealed in
`build/external-fitted-datasheet-20261004/archive-manifest.json`; the source-bound
inventory is `summary.json` in that directory.

The staged, pinned `audio_out.v` and `i2s.v` also give a nominal steady-state
edge relationship at a 24.576 MHz base clock. Data/LRCLK update when the old
`msclk` is high; SCLK falls one base cycle later. After the next enable raises
`msclk`, SCLK rises one further cycle later.

| Sample rate | I²S enable interval, base cycles | SCLK period, base cycles | Data change to next SCLK rise | Previous SCLK rise to data change |
| --- | --- | --- | --- | --- |
| 48 kHz | 8 | 16 | 9 cycles | 7 cycles |
| 96 kHz | 4 | 8 | 5 cycles | 3 cycles |

This was initially source-derived steady-state spacing. The executable checks
below now verify it for both rates and the exercised reset/rate phases. It is
still not a physical 96 kHz test or package-pin timing pass. The original review is preserved as
`i2s-edge-relationship-review-20261004.json` in the sealed inventory directory.

## Conditional forwarded-clock analysis

`tools/audit_hdmi_output_timing.tcl` now models the inverted forwarded clock
explicitly at `HDMI_TX_CLK`, with separate generated clocks for the HDMI PLL and
direct-video audio PLL. It applies nominal receiver setup/hold requirements of
1.8/1.3 ns from the linked Rev. B data sheet. The overlay assumes zero PCB skew
and no additional receiver clock delay. These are analysis inputs, not verified
properties of the connected board.

The tool runs on a byte-verified copy of the existing fitted project, without a
new fit, hardware access or production SDC changes. All 27 video/control outputs
have setup and hold paths in both branches at all four corners: 432 checks.
Full clock paths include the clock switch, inverted DDIO cell and output buffer.
Clock uncertainty and common-path pessimism removal are included by TimeQuest.

| Fitted clock branch | Period | Minimum conditional setup margin | Minimum conditional hold margin |
| --- | --- | --- | --- |
| HDMI PLL | 6.732 ns (about 148.54 MHz) | 0.199 ns, slow −40 °C, D[7] | 1.220 ns, slow 100 °C, DE |
| Direct-video audio PLL | 40.682 ns | 17.159 ns, slow −40 °C, D[7] | 18.209 ns, slow 100 °C, DE |

An additional data-minus-clock delay reduces setup margin and increases hold
margin. Under these assumptions, the HDMI branch permits an additional delay
between −1.220 and +0.199 ns across all reported paths. This interval is not a
measured PCB bound. The actual 720p mode runs at 74.25 MHz; the fitted nominal
clock analysis does not independently qualify every reconfigured PLL mode.
Receiver clock-delay programming, voltage thresholds, loading and physical
signal integrity still require verification. Audio and the other external I/O
endpoints remain separate.

`analyze_hdmi_output_timing.py` requires complete pin/corner coverage, exact
launch/capture branch names, inverted waveforms, signed half-cycle relationships
and consistent arrival/required/slack values. Its regressions reject missing or
duplicate pins, wrong clock branches, missing inversion, non-finite values and
incorrect edges. Negative slacks remain failures; positive results always retain
`external_qualified=false`.

The original extraction used clock node IDs that TimeQuest warned were unmatched
filters. Although observed path names matched the intended branches, that run is
rejected and preserved. A corrected run uses explicit `get_clocks` collections;
its only 16 warnings are the existing disabled-YC platform filters. A preparation
and a corrected-launch path error are likewise preserved. The build record's
earlier timing-report digest differs from the sealed post-audit report; both are
retained. The copied report matches the sealed fit evidence, and all 444 original
project files remain byte-identical after analysis.

Evidence is sealed in
`build/hdmi-output-overlay-20261004/archive-manifest.json`; the receipt is
`build/hdmi-conditional-output-timing-progress-20261004.json`. External timing
qualification remains open.

## Executable audio checks and reset correction

The full pinned `audio_out.v` pipeline and its actual enable generator pass
3,072 reset/rate-change scenarios and 492,529 channel-word comparisons. Tests
cover both rates, 512 phases and reset widths of 1, 2 and 7 base cycles. Normal
data/LRCLK edge spacing matches the table above. However, 768 mid-session reset
events force LRCLK and rising BCLK together. Reset is reachable through both
the platform reset and the audio-filter command (`sys_top.v` command `0x39`).

Independent fitted min/max pin-delay intervals cannot guarantee that this
same-base-edge LRCLK transition lies outside the receiver's 2 ns setup/hold
aperture at any of the four corners. These raw intervals omit correlation,
uncertainty and board skew; they identify missing timing assurance, not a
measured receiver failure. They do not justify declaring reset edges safe.

The local `fpga/sys/i2s.v` override keeps BCLK running and services reset only at
the normal data-update phase, one base cycle before BCLK falls. A pending bit
retains even a one-cycle reset request. Reset clears both latched words and
serial data; playback resumes with the original channel ordering and one-bit
I2S channel delay. Module-scope initialized state registers request silent
startup with the initial LRCLK high. An isolated Quartus compile verifies these
values in the compiler functional netlist; electrical startup remains unmeasured.

The revised full audio pipeline passes 4,096 cases and 636,672 channel-word
comparisons across 111,222,532 base cycles. Reset widths now include 33 cycles,
covering held reset as well as short requests. Checks verify unchanged BCLK
high/low widths, reset service within 16 base cycles, silence after service,
data/LRCLK edge spacing through active reset, both rates and subsequent word
serialization. No simultaneous LRCLK/rising-BCLK transition occurs. The same
test rejects the original reset behavior and a deliberate lost-request mutation.

Yosys also proves the digital data-update phase assertions by temporal induction
for unrestricted reset sequences and stereo word values, assuming clock enables
are never adjacent. The platform enable divider supplies that spacing. An
eight-step counterexample is retained for the original serializer. This proof
does not establish physical setup/hold, electrical initialization or reset CDC.
Formal evidence is sealed in
`build/platform-i2s-reset-formal-review-20261004/archive-manifest.json`.

Verilator 5.020 requires a simulator-only conversion of five static local zero
initializers in the pinned `audio_out.v`. They move to module scope with scoped
renaming; clocked assignments and expressions remain unchanged. Exact conversion
checks and actual mixer/mute-counter progress checks pass. Production vendor
audio files remain unchanged. Original compiler failures, an invalid warning-
suppression experiment and a failed Yosys full-pipeline probe remain preserved.

Baseline and corrected simulation evidence are sealed respectively in
`build/platform-i2s-baseline-review-20261004/archive-manifest.json` and
`build/platform-i2s-phase-reset-review-20261004/archive-manifest.json`.
The first phase-reset seed-21 fit fails a core/DDR hold check by 0.044 ns. A
separate experimental fit with additional global hold margin also fails. Both
remain rejected. The former fit's functional netlist starts LRCLK low despite
the high ANSI output initializer; an independent compiler probe reproduces
that difference and shows that module-scope register initialization is retained.
The serializer now uses the latter form. Its isolated actual Quartus compile
passes output startup checks, while the original full-core netlist is rejected.
The checker traces output aliases, optional inversion, output-buffer enable and
explicit register power-up values, and refuses unknown cones. It verifies a
compiler model before clock edges with global reset released, not PCB timing.

The revised source repeats all 4,096 full-pipeline scenarios successfully and
passes the six-assertion unbounded proof, including minimum digital edge bounds
of three base cycles from data/LRCLK change to the next BCLK rise and one base
cycle from BCLK rise to a possible change, with nonadjacent enables assumed.
The revised source's full build in
`build/fpga-i2s-module-init-seed22-20261004` passes 140 internal checks and all
five source-bound fitted audits, with a minimum core/DDR hold margin of 0.031 ns.
Its full-core functional netlist retains output startup values 0/1/0. A stronger
seven-assertion unbounded proof also includes the exact pinned enable divider
with arbitrary defined startup states. It requires no enable-spacing assumption;
the original serializer fails the same generated wrapper.

The exact RBF `deb79999` is frozen and passes fresh private ten-minute Studio
and player music/save/clock/memory tests with zero underruns. The coordinator
verifies canonical Main/handler/payloads/settings and Tetris restored. Human
picture/stereo confirmation for this exact artifact remains pending. The
preceding RBF's conditional HDMI margins, pin delays and hardware results do not
qualify this changed layout. Electrical startup, receiver setup/hold and reset
CDC remain unqualified. Private hardware tests use temporary activation and
restore the confirmed installation; no permanent installation is accepted.

## Conditional output checks for the startup-corrected fit

A fresh copied-project analysis for `deb79999` passes all 432 HDMI checks.
The HDMI branch's setup/hold minima are 0.198/1.218 ns under the earlier stated
nominal receiver/zero-board-skew assumptions. All 446 original fitted-project
files remain unchanged; copied-project analysis does not alter the tested RBF.

For I2S, fresh four-corner datasheet reports give minimum/maximum rising and
falling output delay intervals from the same base clock for SCLK, data and LRCLK.
The exact-enable formal proof supplies three base cycles from a data/LRCLK
change to the next rising capture and one base cycle from capture to a possible
change. With base period T, the conservative intervals are
`setup = 3*T + min(SCLK rise) - max(data transition) - 2 ns` and
`hold = T + min(data transition) - max(SCLK rise) - 2 ns`.
Both data polarities are included; capture uses only SCLK's rising output edge.
The 2 ns requirements come from
[ADV7513 Rev. B, audio AC timing](https://www.analog.com/media/en/technical-documentation/data-sheets/ADV7513.pdf).

All eight pin/corner comparisons are positive: minimum setup/hold margins are
115.207/34.786 ns. The calculation assumes the fixed fitted 40.682 ns base
period, zero PCB data-minus-clock skew and no additional relative edge
uncertainty. A read-only observation during the candidate player test returns
0x0B=0x0E, selecting rising capture. Regression controls reject missing/duplicate
pins or clocks, incorrect launch edges, non-finite/reversed intervals and an
absent digital proof; negative margins, including tiny negatives, remain failures.
This conditional interval model excludes electrical startup, duty cycle, actual
clock jitter/skew, analog signal integrity and reset CDC. Physical qualification
remains open despite its positive nominal margins.
