# FPGA clock-crossing review

October 4, 2026. This review applies to the frozen shared-reset seed-17 RBF
`ccc41285fac38df7d1e76051226bcaaf50dc7a567b92f499c0ee619fddad2884`.
It records bounded progress; complete CDC and external-I/O qualification are
still open. The installed package and candidate bitstream are unchanged.

## Configured DDR port and the timing model

The earlier inventory reports 693 incoming cut endpoints touching `emu`,
with one sampled path per endpoint. Of those, 555 start at two virtual HPS
registers: 481 at `f2sdram~FF_3805` and 74 at `f2sdram~FF_3781`. Their clock
labels initially suggest an audio-to-system crossing in the DDR reader.

The source wiring sends `clk_sys` through `DDRAM_CLK`, platform `ram_clk`,
`sysmem.ram1_clk` and `f2h_sdram1_clk`. This port uses command port 1 and
read/write FIFO 2. The platform audio DDR client uses FIFO 3 separately.
The Quartus 17 HPS generator in `ip/altera/hps/util/procedures.tcl` connects
the data FIFO clock and chooses `rd_valid_<read_port_number>` for the same
Avalon port. The generated `sysmem.sv` follows that mapping.

The fitted timing graph contains multiple virtual sources for each configurable
control output:

| Output | Modeled clock pins | Configured pins selected in this review |
| --- | --- | --- |
| `rd_valid_2` | `rd_clk_0`, `rd_clk_1`, `rd_clk_2`, `rd_clk_3` | `rd_clk_2` |
| `cmd_ready_1` | `cmd_port_clk_1`, `wr_clk_0` through `wr_clk_3` | `cmd_port_clk_1`, `wr_clk_2` |
| `rd_data_2[63:0]` | `rd_clk_2` | `rd_clk_2` |

`FF_3805` is the `rd_clk_3` model source for `rd_valid_2`; `FF_3781` is the
`wr_clk_3` model source for `cmd_ready_1`. Thus these sampled audio-clock
labels plausibly reflect the timing model's possible port mappings rather
than the configured functional mapping. This is an inference from wiring,
vendor generation code and fitted graph edges, not a captured waveform or a
vendor waiver. No timing arcs or constraints were removed.

Altera's [FPGA-to-HPS SDRAM documentation](https://docs.altera.com/r/docs/683360/18.0/an-796-cyclone-v-and-arria-v-soc-device-design-guidelines/fpga-to-hps-sdram-access?contentId=8kWLOyQcYS5O8tznOcLWdg)
describes separate command ports and 64-bit data ports, with wider interfaces
using multiple data ports. It supports the port architecture; it does not
itself explain or waive these Quartus model arcs.

## Four-corner configured-path check

`tools/inventory_hps_ddr_timing.tcl` selects 64 data and three configured
control model sources by their fitted pin/clock edges. It checks the worst
ordinary timed path per reader endpoint for both setup and hold. Every
reported launch and latch clock is the same 105 MHz system clock.

| Corner | Reader endpoints per analysis | Minimum setup slack | Minimum hold slack |
| --- | ---: | ---: | ---: |
| Slow −40 °C | 294 | 1.823 ns | 0.212 ns |
| Slow 100 °C | 294 | 1.777 ns | 0.296 ns |
| Fast −40 °C | 294 | 5.758 ns | 0.173 ns |
| Fast 100 °C | 294 | 5.151 ns | 0.208 ns |

Original STA session 99227 exited successfully. The first inventory attempt
failed its 67-source guard because the Tcl pattern missed literal brackets
in data-pin names; its script and failure log are preserved separately.
The corrected four-corner run is sealed with the model inspection, fitted
sources and provenance in
`build/shared-reset-configured-ddr-timing-v2-20261004/archive-manifest.json`.

This establishes positive timing margins for the configured model sources.
It does not establish complete coverage of all cut paths or qualify the
entire platform. Remaining work includes the other custom/platform crossings,
bundled-data handshake assumptions, reset/session release, and the three
unconstrained external input ports and 50 external output ports identified
by the earlier inventory.

## Independent-clock video ownership simulation

`python3 tools/test_rtl.py --suite video-cdc` now exercises the production
`tic80_video_top` through an observation-only wrapper. The 105 MHz system
clock and shared 24.576 MHz video/audio clock have independent rising and
falling edges. Four starting phase offsets cover coincident and separated
edges. The actual host exchange API publishes patterned frames while the DDR
model stalls requests and delays the first frame burst by 800,000 system
cycles, approximately 7.6 ms. All completed copies finish during active raster.

The passing run checks 36 complete copies, 1,226,532 RGB pixels, 24 bank
changes during vertical blanking and 12 one-system-cycle session flushes.
It checks every sampled visible pixel against its complete frame generation
and x/y pattern, rejects writes into the active displayed bank, verifies held
DDR commands, and bounds forward progress. Session restarts occur both during
a frame burst and while a completed frame awaits scanout acknowledgement.
Restart waits follow the backend's contract: session acknowledgement and
cleared video/audio status are separate DDR writes.

Two isolated reader counterexamples are rejected: disconnecting the consumed
acknowledgement prevents the second publication from completing; selecting
the previously copied bank triggers the displayed-bank write assertion.
An earlier guard-bypass counterexample passed when the shorter copies finished
in blanking; that result is preserved and does not establish negative coverage.

The final positive run's original session 93507 exits 0. CMake registers
`rtl_video_cdc`; its configure session 37286 also exits 0. The test inputs,
logs, counterexamples, earlier harness attempts and candidate/source bindings
are sealed in `build/video-cdc-evidence-20261004/archive-manifest.json`.
No production RTL, fitted bitstream or installed payload changed.

This verifies bounded digital ownership behavior. It does not simulate analog
metastability, provide MTBF qualification, or replace physical capture. Audio
output is idle in this fixture; separate audio evidence remains separate.

## Explicit custom synchronizer settings

The four-corner topology audit checks 117 custom first-stage registers. Each
has exactly its expected second-stage fanout and positive setup/hold margins
in the frozen shared-reset fit. Its MTBF report still uses automatic
identification and excludes these chains from the calculated design estimate.

The explicit-settings revision adds `fpga/cdc.tcl`. It identifies each chain
head and its remaining stages. Intel documents both identification options
and per-register transition rates in its
[Standard Edition design recommendations](https://www.intel.com/programmable/technical-pdfs/683323.pdf).
The Quartus settings preflight exports exactly 117 unique heads without
wildcard targets: 103 use 25 million transitions/second; 14 use 131 million.
The latter bound exceeds the combined 105.01 MHz system and 24.58 MHz audio
source clocks reported on paths involving reset. This is an activity
assumption for analysis, not physical metastability qualification.

The first explicit-settings fit, seed 17, is rejected for HDMI-domain setup
slacks of −0.090 ns and −0.203 ns. Its standalone synchronizer audit finds
117 correctly timed stage paths but reports calculated MTBF for only 116.
`played_sync1[0]` is absent from the MTBF report despite a fitted audio-to-system
path and sole fanout to `played_sync2[0]`. Its estimates also do not satisfy
the activity-rate gate. The first corner preserves vector wildcard rates;
later corners revert to defaults, and fresh standalone reports also revert
those rates. Those reports and their exact rates are preserved.

The subsequent source correction enumerates exact vector bits, forces each
first stage, raises all 14 mixed-source bounds, and uses a fresh STA process
for each corner. The validator accepts Quartus's literal “Greater than 1
Billion” as a lower bound; it still rejects missing/uncalculated chains,
automatic identification, insufficient rates and nonpositive timing. All 11
build-record regression tests pass. Seeds 18 and 19 were deliberately stopped
when further rate/reporting gaps were found, before qualification or deployment.

The seed-20 fit in `build/fpga-explicit-sync-seed20-20261004`, original
session 2229, exits 1 after the timing guard rejects hot/cold HDMI setup
slacks of −0.209/−0.354 ns. Its standalone four-corner audit reports 116
calculated chains with the required explicit rates, but still omits
`played_sync1[0]`. Changing second-stage identification can make Quartus
report that head as a one-register chain; this does not meet the two-stage
gate and was not accepted. The settings experiments do not regenerate an
RBF and leave the staged QSF unchanged.

Evidence for the earlier rejected fit,
diagnostics, source correction and cancelled attempts is sealed in
`build/explicit-sync-seed17-rejection-evidence-20261004/archive-manifest.json`.
The installed payloads and frozen shared-reset candidate are unchanged.

## Reuse the equivalent low audio Gray bit

The FIFO read-word counter and played-word counter reset together and advance
on exactly the same audio edge. Their low eight binary bits match even at
FIFO pointer wrap, so their lowest Gray bits match. The new audio revision
reuses `read_sync2[0]` for played-counter decoding and removes the redundant
`played_sync1[0]`/`played_sync2[0]` crossing. The other 30 played Gray bits
still have independent two-stage synchronizers. No first-stage signal feeds
the decoder.

A comparison fixture runs the original and revised production audio modules
with independent clocks, stalled DDR, complete 4096-sample clips and a reset
during a pending response. All seven outputs match at every evaluated event
for four clock phases. It also observes the original source, first-stage,
second-stage and low-binary-bit equivalence invariants. A wrong-bit
counterexample is rejected by the read-counter comparison. Existing audio
and integration suites pass, including short-clip startup, FIFO full,
stalled transport, stereo order and session resets. These digital tests do
not simulate analog metastability.

The source settings preflight now exports 116 explicit heads: 102 at
25 million transitions/second and 14 at 131 million. Seed 21's fitted audit
passes all 116 heads at each of four corners: exclusive expected
second-stage fanout, positive stage setup/hold, explicit identification,
inclusion in calculated MTBF, and conservative activity bounds. Each custom
chain's reported MTBF lower bound is one billion years; this is a vendor
model estimate and reporting cap, not a measured physical reliability result.

Seed 21, original session 12600, exits 1 because HDMI setup at −40 °C remains
−0.174 ns. The slow 100 °C HDMI setup is +0.030 ns. Disabling synchronous
clear/load mapping on the scaler vertical counter removes that counter from
the ten worst paths. The remaining violated paths run from `o_hacc` to the
horizontal divider's first-stage `o_div`/`o_dir` registers. The design is
still rejected for full timing and has not been deployed.

These results, source bindings, fit reports and original terminal sessions
are sealed in `build/shared-gray-seed21-rejection-evidence-20261004/archive-manifest.json`.
Fresh hardware qualification is required after a fully passing fit. The
[HDMI external timing investigation](hdmi-external-timing-review.md) records
the receiver requirements and active configuration without closing that gate.

## Parallel first-stage horizontal divider

The local `fpga/sys/ascal.vhd` override retains the pinned TEMLIB header and
changes only two temporary declarations and the first horizontal divider
stage. The original remainder is `A*256 - S*256`, followed by subtraction
or addition of `S*128` according to its sign. The new implementation computes
`A*256 - S*384` and `A*256 - S*128` in parallel, then chooses with the original
sign. All operations retain 21-bit modulo arithmetic. The register assignments,
later divider stages and pipeline latency are unchanged.

`tools/test_scaler_divider.py` first checks that the entire source outside
this region matches the pinned file. It analyzes the complete modified VHDL,
synthesizes the actual old/new arithmetic regions with GHDL, and compares them
using Verilator. All 16,384 accumulator values crossed with all 4,096 sizes
match in both the 21-bit remainder and 12-bit direction outputs. This includes
the larger generic configurations and conversion wrap. An isolated wrong
coefficient is rejected at accumulator 1, size 1. CTest registers this check
where GHDL and Verilator are available.

The new fit keeps placement seed 21. Original session 73522 exits 0; all
140 internal setup/hold/recovery/removal/pulse-width checks pass, followed by
the source-bound routing, clock, DDR/shared-reset and four-corner synchronizer
audits. The resulting RBF SHA-256 is
`32daea8fb10b4fec85459a4b958faaba1ed265a79bd4069fef19633d8da37ff2`.
The smallest slack is only 0.002 ns, on the second horizontal divider stage
at the slow −40 °C corner. The detailed worst paths no longer originate in
the modified first-stage input arithmetic. This small margin and external
timing remain open concerns; a local pass does not establish a finished core.

The frozen candidate is `build/parallel-divider-candidate-seed21-20261004`.
Its Main and ARM frontends match the shared-reset candidate, and its manifest
requires fresh hardware qualification for the new RBF. The first private
read-timeout run, original session 39303, exits 1 after its observer fails to
capture enough pending PCM/picture samples. Worker cancellation and explicit
retry succeed, but the full suite remains failed. Its trace and error are
sealed in `build/parallel-divider-run-read-native-e9a567db/archive-manifest.json`.
Canonical installation and Tetris are restored. Local fit/test evidence is
sealed in `build/parallel-divider-fit-evidence-20261004/archive-manifest.json`.

## Revised short-window hardware observation

The failed original read trace remains sealed. A six-second comparison with
the original observer finds RAM storage alone insufficient for the unchanged
30 ms maximum-gap calibration guard. Moving that observer to the runtime CPU
produces only 194 coherent samples, below the required count. These failed
experiments are preserved separately; neither is counted as a pass.

The final observer changes only its sleep from 10 to 5 ms. All complete-frame,
PCM, publication/session and worker-liveness observations remain. Its ARMv7
hard-float ABI passes against the retained native provider snapshot. The new
fixture inherits exactly the same cartridge, keyboard helper and read injector.
On the canonical installation, this observer's six-second SD/RAM comparison
has maximum gaps of 67.9/20.4 ms. The RAM journal lives on verified `/tmp` tmpfs.

The new private run retains the frozen parallel-divider RBF and matched Main/
runtime binaries. Original session 37808 exits 0. The original guarded
50–200 ms interval contains ten coherent silent PCM snapshots and four
coherent complete-frame snapshots with an identical hash. The cancelled save
is unchanged, the same supervisor recovers once, and an explicit retry reaches
BOOT 10. All 168 completed audio observations have zero underruns. The HDMI
clock measures 74.25 MHz before and after the candidate test. Canonical Main,
handler, payloads, settings, Tetris, HDMI and audio are restored and verified.

The liveness trace brackets worker disappearance at 244.75–263.69 ms after
the read marker. The unchanged driver's one-second bound distinguishes the
short RUN path from the console timeout; it does not prove an exact 250 ms
cancellation instant. This limitation is retained in the qualification scope.
The original five-PCM/three-picture requirements and read behavior were not
relaxed. Evidence is sealed in
`build/parallel-divider-fast-observer-native-6fbe2f48/archive-manifest.json`.
This closes the specific pending PCM/picture observation gap for the new RBF,
while human display/audio and broader release gates remain open.

## Fresh sustained playback on the parallel-divider RBF

The frozen seed-21 candidate completes independent 600-second Studio and player
music tests in original session 94467, which exits 0. Each monitor is launched
once, collected after its original terminal status, and not restarted.
Studio/player produce 3,045/3,047 samples, zero underruns, approximately 60 Hz
frame/completion rates and 48 kHz audio. Fitted queue changes are 0.029/0.329 ms.
Both have valid CRC-protected saves, one stable worker, flat sampled parent and
worker RSS after 60 seconds, and clean departure logs. HDMI measures 74.25 MHz
before and after each run. Canonical Main, launcher, payloads, settings, Tetris,
HDMI and audio are verified restored.

The receipt is `build/parallel-divider-sustained-playback-progress-20261004.json`;
raw evidence is sealed in
`build/parallel-divider-sustain-a0b79136/archive-manifest.json`.
The physical picture/stereo question is bound to this actual Studio run and
RBF; the earlier incoming confirmation lacks that binding and is preserved
separately. This bounded pass does not resolve the 0.002 ns timing headroom,
external I/O timing or other release gates. A source-bound review identifies
two sequential carry chains in the next horizontal divider stage as a possible
future optimization; no further production HDL change or fit is asserted here.

## Parallel second horizontal divider stage

The subsequent local override also parallelizes cycle 2. For registered input
remainder R and size S, it computes R−64S and R+64S for the first sign decision,
and R−96S, R−32S, R+32S and R+96S for the possible final remainders. Both original
sign decisions and direction bits remain. All arithmetic retains 21-bit modulo
width; register assignments, later stages and pipeline latency are unchanged.

`tools/scaler_divider_regions.py` guards the pinned source outside the two
arithmetic regions and requires the original register writes. The unchanged
first stage again passes all 67,108,864 input pairs. For cycle 2,
`tools/test_scaler_divider_formal.py` synthesizes the actual pinned and modified
VHDL with GHDL. Yosys proves equality of all 21 remainder and 12 direction output
bits for every defined combination of 21 incoming remainder, 12 size and 12
direction bits, without assumptions. A wrong coefficient yields a concrete
counterexample; an unintended later-pipeline change fails the source guard.
CMake registers the formal test when GHDL and Yosys are available.

Original fit session 45878 exits 0 with seed 21 and the same timing constraints.
All 140 internal checks and all five post-fit audits pass, including the 116
custom synchronizer chains at four corners. Cold HDMI setup improves from
0.002 to 0.211 ns; the worst cold setup path is now in the horizontal polyphase
pixel calculation. The overall minimum of 0.110 ns is a fast-corner HDMI hold
check. Synthesized register, block-memory-bit and DSP counts remain unchanged.
These are internal timing results; external I/O remains unqualified.

The frozen candidate is
`build/second-stage-divider-candidate-seed21-20261004`; its RBF SHA-256 is
`d08bb729516adeb26233460e2fde55cea5d1cbd8b76bd6c788a810f295aedb3d`.
Formal, counterexample, fit and routing evidence is sealed in
`build/second-stage-divider-fit-evidence-20261004/archive-manifest.json`.

Fresh native session 54305 exits 0. The guarded read interval contains nine
silent PCM snapshots and seven complete frames with an unchanged hash. The
cancelled save is preserved, the supervisor recovers once, and explicit retry
reaches BOOT 10. All 167 completed audio observations have zero underruns;
HDMI measures 74.25 MHz before and after. Canonical Main, launcher, payloads,
settings, Tetris, HDMI and audio are restored and verified. The sampled worker
disappearance interval is 242.106–258.011 ms; it does not identify an exact
cancellation timestamp or prove a less-than-250-ms reap. A separate corrected
receipt fixes stale explanatory prose without rewriting sealed evidence.

Native evidence is sealed in
`build/second-stage-divider-fast-observer-native-bd57eebb/archive-manifest.json`.
The current receipt is
`build/second-stage-divider-native-corrected-progress-20261004.json`.
At this native-test stage, the new RBF still required fresh sustained and
physical HDMI/stereo tests; subsequent sustained results are recorded below.
The previous first-stage-only RBF's soaks do not qualify it. Broader release
gates remain open.

## Sustained traces and interrupted host observation

The latest RBF's original Studio/player monitors produce 3,043/3,047 samples
over approximately 600 seconds each, with zero underruns, valid saves,
approximately 60 Hz completion and 48 kHz audio. Both monitors are dispatched
once and exit with status 0. The original host coordinator loses SSH during
player observation. A separate coordinator collects that same original monitor;
it does not repeat activation, cartridge load or monitor start. Both continuous
traces remain complete. Studio has 20 memory observations, while player's six
observations include a 470.632-second gap. Flat sampled RSS does not establish
uninterrupted checkpoint coverage across that gap.

The resumed coordinator restores the files and launches confirmed Tetris, but
its final process check races a disappearing `/proc` entry. Separate read-only
verification proves canonical Main, launcher, payloads, settings, Studio/Tetris,
74.25 MHz HDMI and zero-underrun audio restored. Both original observer errors
remain preserved. Evidence is sealed in
`build/second-stage-divider-sustain-b9534bd9/archive-manifest.json`; the receipt
is `build/second-stage-divider-sustained-recovery-progress-20261004.json`.

Four-corner raw external clock-to-output delays and source-derived steady-state
I²S edge relationships are now sealed separately in
`build/external-fitted-datasheet-20261004/archive-manifest.json`. These are inputs
to the remaining external timing model, not setup/hold qualification. The player
checkpoint gap is closed by the separate fresh run below. Physical picture/stereo
confirmation and broader release gates remain incomplete.

## Fresh player memory checkpoint coverage

The same frozen second-stage divider candidate completes a separate 600.062-second
player soak with 3,047 samples, zero underruns, approximately 48 kHz audio and
60 Hz completion, valid saves, and −0.243 ms fitted queue change. Its 21 memory
checkpoints have maximum monitor/wall-clock gaps of 30.338/30.483 seconds. Sampled
parent and worker RSS ranges are both zero KiB, descriptor counts remain stable,
and the worker identity is unchanged. This closes the bounded sampling gap for
this fixture; it does not reconstruct the interrupted earlier memory record or
establish stability for every workload.

The observer retries failed read-only connections within a bounded budget while
launches and core switches retain single dispatch. Tests preserve journal offsets
across reconnects and reject duplicate launch attempts. A separate live observer
recovers the same advancing monitor after closing only its own SSH transport;
no remote mutation is issued. The main coordinator and original monitor both
exit 0, and canonical payloads/settings, Tetris, HDMI and audio are restored.
The receipt is `build/second-stage-divider-player-checkpoints-progress-20261004.json`;
evidence is sealed in
`build/second-stage-divider-player-checkpoints-5a9cb224/archive-manifest.json`.
This is runtime observation evidence, separate from external setup/hold and
physical CDC qualification.

## Platform I2S reset phase

Full pinned platform audio simulation confirms steady 48/96 kHz edge spacing,
but finds 768 exercised mid-session resets that force LRCLK and rising BCLK
together. The four-corner raw pin-delay inventory does not establish a safe
receiver aperture for those same-base-edge transitions.

The new `fpga/sys/i2s.v` override keeps BCLK running through reset and latches
requests until the regular data-update phase. The full pipeline passes 4,096
reset/rate-change cases, 636,672 word checks, and BCLK pulse-width/silence/service
checks. The original module and a lost-request mutation are rejected. Digital
edge-phase assertions also pass temporal induction for arbitrary reset and
sample inputs with nonadjacent enables. These checks do not qualify asynchronous
reset crossing, physical startup, receiver timing or every audio workload.

The previous frozen RBF and its internal/CDC reports remain unchanged. The
phase-reset seed-21 build fails hold timing by 0.044 ns on HPS read-data to
`boot_session`. A fresh fit with an additional 0.150 ns global core hold margin
also fails (worst reported slack -0.282 ns). Neither is accepted or loaded.
That unsuccessful extra-margin experiment is removed without adding any timing
exemption; the original complete timing requirements still apply.

An independent Quartus compiler probe and the first rejected fit's functional
netlist reproduce a low LRCLK startup value despite its ANSI output initializer.
The current serializer uses initialized module-scope state registers and output
aliases. Its actual isolated Quartus compile starts SCLK/LRCLK/data at 0/1/0;
the explicit output-cone checker rejects the old full-core netlist. Full-pipeline
simulation and the six-assertion edge-bound proof pass for this revised source.
Its fresh full fit in
`build/fpga-i2s-module-init-seed22-20261004` exits 0 and passes all 140 timing
checks plus five source-bound fitted audits. The overall minimum is 0.031 ns
on core/DDR hold; 116 custom synchronizer chains pass the four-corner audit.
The full-core compiler functional netlist also retains output startup values
0/1/0. A further seven-assertion proof includes the exact platform enable
divider, allows arbitrary defined divider startup states, and has no input
assumptions. The original serializer is rejected under that same wrapper.
These results are bound to the frozen `deb79999` RBF; fresh private hardware
music, read recovery, lifecycle and Studio worker-fault suites pass. Full platform/reset CDC and physical external timing remain
open. Earlier simulation evidence is sealed in
`build/platform-i2s-phase-reset-review-20261004/archive-manifest.json`.

The fresh lifecycle suite checks four cold entries, twelve selections and
sixty-four FPGA reloads with BOOT counts exactly 1–76, zero audio underruns,
retained supervisor identity within each cycle and bounded sampled memory.
The separate Studio suite exercises rejected/missing/oversized cartridges,
script hangs and an abrupt worker kill; completed saves survive and explicit
RUN retries succeed. Both original coordinators exit 0 and verify restoration.
Their receipts are `build/i2s-module-init-lifecycle-progress-20261004.json` and
`build/i2s-module-init-studio-fault-progress-20261004.json`. These finite runtime
checks do not establish reset-domain safety.

The preceding frozen fitted source still connects `sys_top`'s `reset | areset` directly to
`audio_out.reset`. `areset` is assigned in the `clk_sys` command process, while
the consumers run on `clk_audio`; the filter-enable process has asynchronous
assertion and unsynchronized release. The I2S edge proof accepts digital reset
changes but does not model metastability or qualify this platform crossing.
Reset assertion width, release synchronization and all downstream consumers
remain a concrete follow-up review; no platform reset constraint is waived.

The working `fpga/sys/audio_out.v` now adds one shared reset chain with
asynchronous assertion, three rising audio edges before release and an asserted
initial state. The I2S serializer, SPDIF, both sigma-delta DACs, IIR and filter
enable logic consume its final stage. A strict pinned-source comparison limits
the override to this chain and seven reset-reference replacements; six
downstream reset blocks are affected. The enable divider and other arithmetic
remain unchanged.

An unbounded digital proof checks release using async2sync abstraction.
Separate simulation covers 450 phase/pulse/retrigger/stopped-clock cases,
including 578 pulses and between-edge assertion. Three isolated controls reject
two-edge release, a missing asynchronous assertion event and raw-reset bypass.
The actual revised audio pipeline passes all 4,096 cases and 636,544 complete
channel words with zero reset LRCLK/rising-BCLK coincidences. The preceding
pipeline regression remains positive, and the exact-enable edge proof binds the
new audio source. These checks do not model physical metastability or minimum
electrical reset pulse widths.

The full seed-22 compile succeeds in
`build/fpga-platform-audio-reset-seed22-20261004`, original session 36325,
with all 140 internal checks positive and minimum slack 0.118 ns. The original
coordinator exits 1 during its final audit. Preserved diagnostics identify a
Tcl global-variable collision followed by a fitted register-name lookup error.
The corrected audit uses local variables and fitted node IDs directly. Fresh
audits run on a copied fitted project without restarting the compile; all
446 original project files remain unchanged.
A new mandatory fitted audit requires three retained stages, exclusive
first/second-stage fanout, positive audio-domain stage timing, complete
setup/hold or recovery/removal pairs for every discovered final-stage consumer,
and no direct `areset` path to those consumers. The build record requires all
four distinct corners and fresh evidence. All six audits now pass on RBF
`fd673c65`. The new reset audit covers 398 consumers and 796 timing checks per
corner; its minimum consumer margin across corners is 0.494 ns. The recorder
hashes the four tables and verifies paired modes, clock identity, complete
inventory and positive finite margins. Fresh functional-netlist checks retain
SCLK/LRCLK/data startup at 0/1/0. This does not prove electrical startup or
physical metastability behavior. Source evidence is sealed in
`build/platform-audio-reset-source-review-20261004/archive-manifest.json` and
`build/platform-audio-reset-source-progress-20261004.json`. Installed payloads
and the preceding frozen RBF remain unchanged; this working revision is not
accepted for release. Fitted recovery evidence is recorded in
`build/platform-audio-reset-fit-progress-20261004.json`. Fresh conditional
HDMI analysis checks 432 paths with setup/hold minima 0.198/1.221 ns, and
source-bound I2S interval checks give 114.923/35.785 ns under the same stated
zero-skew and fixed-clock assumptions. This does not qualify physical external
timing. The exact new candidate is frozen. Original coordinator 85132 finishes
with Studio's bounded soak passing and player's memory-check coverage failing
after a reconnect gap of 51.220 seconds. Both audio monitors complete with zero
underruns, and the canonical installation/Tetris are restored. The new player
phase in that original suite remains unqualified; this failure does not waive
its 40-second coverage limit. See
`build/platform-audio-reset-sustain-failure-progress-20261004.json`.
A fresh player-only run uses a detached remote CLOCK_MONOTONIC process/save
sampler. Original coordinator 41400 and both original remote monitors exit 0.
It passes 600.062 seconds with 3,047 audio samples, zero underruns, valid saves
and flat sampled RSS/descriptors. Its 21 remote checkpoints have a maximum
capture gap of 30.106 seconds even across a deliberate observer SSH close.
This qualifies the bounded replacement player phase; it does not claim a
MiSTer network outage or physical timing qualification. The earlier Studio
phase remains valid and linked separately. Completed evidence is recorded in
`build/platform-audio-reset-player-remote-progress-20261004.json`; canonical
installation restoration is verified at the player suite's end.
