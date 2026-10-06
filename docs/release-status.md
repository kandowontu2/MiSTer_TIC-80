# Release status

Updated October 6, 2026 UTC. The published
[Frontier preview](https://github.com/kandowontu2/MiSTer_TIC-80/releases/tag/v0.1.0-dev.20261005)
is now installed through its own script. The user confirmed the installation
worked after the clean-install preparation. Independent read-only SSH checks
verify all five installed payload hashes and unchanged official stock Main
`9f6e5a23` (MiSTer_20260912). Games and saves were preserved during preparation.
The check leaves PICO-8 selected throughout; it does not test TIC-80 gameplay.
Evidence is in `build/user-install-readonly-20261005/result.json`.

The preview omits a replacement Main. Its RBF is `fd673c65`, Studio `9123c34a`,
player `dfe36c88`, handler `edbf962c` and CA bundle `a41b5d35`. Earlier extensive
prototype qualification used patched Main and does not establish complete
stock-Main startup, source-save or input behavior. Those runtime gates remain
open; see [standard-Main compatibility](stock-main.md).

The newer per-core HID horizontal-scroll implementation is a local development
candidate. Optimized host ASan/UBSan and ARM/QEMU tests pass with stalled
descriptor/feature probes still attached, while healthy devices continue.
Both production frontends are rebuilt with unchanged, hash-bound SDK language
libraries; target ABI, backend/input tests and helper dispatch pass. These
changes have not replaced the published release or the installed binaries.

The current ARM binaries additionally pass modeled player held-transfer and
delayed-initialization checks and Studio anonymous selection/cancellation.
All nine Studio reload scenarios and an execution-oracle negative control pass
under ARM/QEMU. The original workflow run failed an asynchronous disk-flush
comparison and remains preserved; actual TIC tracing establishes the corrected
oracle. See `build/studio-reload-oracle-v5-20261006/result.json` and
[standard-Main compatibility](stock-main.md) for its scope. This is local
runtime evidence, with no MiSTer access or installed/published payload changes.

Later private native checks pass on stock Main for candidate RBF `5595ba32`
and Studio `baf2b1fc`: actual horizontal `mouse()` API/OSD behavior and a
ten-minute automated music/resource soak. Player `930e7531` adds bounded
reconfiguration recovery. Both default frontends now pass four actual RBF
reloads, native/modern/legacy PNG MGL transfers and active audio after each;
Studio also passes private working-copy and established-file editing/Save.
The original jobs and independent restoration review are recorded in
`build/private-lifecycle-native-v7-20261006` and
`build/private-lifecycle-native-review-v7-20261006`.
These candidate payloads remain private. The earlier intermittent OSD failure
and a later statistics-marker rejection remain preserved and unreproduced;
four fresh native mouse/OSD cycles and a ten-minute music/resource soak
now pass for the new player, with zero measured underruns and BOOT=1.
Human picture/stereo confirmation of this exact player is still pending. See [standard-Main compatibility](stock-main.md) for exact binary
hashes, test scope and original failures. Historical prototype confirmations
below do not count as human confirmation of this candidate.

## Historical prototype qualification

Matched build `18de1e89` was previously installed in the normal SD-card
locations with Studio as the default. That prototype used Main `81253dc2`,
RBF `fd673c65`, Studio `9123c34a`, player
`dfe36c88` and handler `edbf962c`. It supersedes the earlier Studio `609fcfae`
installation. This is a qualified development package; broader release gates
remain open.

Following the user's concern about replacing the shared MiSTer executable,
standard Main compatibility is the default release target. The installed
prototype was qualified only with its patched Main. A new digital transport
check passes eight standard native/PNG-index transfers using unmodified Main
helpers, without extension metadata. This does not yet qualify a standard-Main
hardware package. See `stock-main.md` for the remaining per-core integration work.

Both frontends pass fresh ten-minute music/save/clock/memory tests with zero
measured underruns, valid saves and flat sampled RSS. The user confirms stable
720p/60 Samsung HDMI picture and clear stereo audio for both. Native ARM
contracts and all 21 production-worker declaration/replacement cases pass.
The native production-worker matrix passes all 84 language/format/frontend
cases, 168 full RGBA frames and 5,040 PCM tick buffers.

All 84 Main/FPGA cartridge cases also pass source acknowledgement, full RGB
comparison and bounded audio checks. Original coordinators 49327 and 77266
remain recorded as failures: the first rejected an empty MENU transition;
the second used a Studio reference containing a cursor for the player. Review
retains 43 valid raw observations without ignoring pixels. Coordinator 21969
checks the remaining 41 player cases against frozen player references and exits
0. Neither verification repair changes the production binaries.

Physical Xbox and mouse diagnostics pass all eight gamepad actions, separate
WASD/Enter/Esc/Q/E, both mouse axes, three buttons, both wheel directions and
released input. Moving the mouse to another USB port resolves the observed
kernel descriptor error `-32`. The paired and trusted Anker A7726 Bluetooth
keyboard passes 22 keys, press/release edges, Shift+Z / Ctrl+Q / Alt+E, held-W
repeat, all eight default gamepad actions and released state. The user confirms
all labels. After a later disconnected observation, waking it reconnects and
Linux enumerates the HID keyboard. This bounded observation does not qualify
Bluetooth power-cycle behavior or all reconnect scenarios.

The six-file package and its ZIP are hash-verified. Installation coordinator
37417 exits 0; independent review verifies the exact installed payload, Tetris
source acknowledgement, 74.25 MHz HDMI with zero measured clock error, 272 audio
samples with zero underruns, preserved settings and a connected Bluetooth
keyboard. The installed pointer advances only after that review. The earlier
two installation failures are preserved with verified full payload/Main rollback:
one used a CRLF-sensitive MGL comparison; one required an idle keyboard to stay
connected. The final installer checks the persistent pairing/trust instead.
Existing cartridges and saves are retained; verified backup paths are recorded
in the installation receipt.

Current receipts in `build`:

- `wasm-import-limits-native-progress-20261005.json`
- `wasm-import-workers-native-v2-progress-20261005.json`
- `runtime-wasm-import-integration-soak-progress-20261005.json`
- `wasm-import-runtime-matrix-native-v2-progress-20261005.json`
- `runtime-wasm-import-matrix-complete-progress-20261005.json`
- `final-input-wasm-import-progress-20261005.json`
- `physical-keyboard-wasm-import-progress-20261005.json`
- `studio-import-listening-v2-progress-20261005.json`
- `wasm-import-final-qualification-20261005.json`
- `wasm-import-package-progress-20261005.json`
- `wasm-import-installation-v3-progress-20261005.json`

Download: `releases/TIC80-MiSTer-18de1e89-20261005.zip` (16,995,093 bytes).
SHA-256: `3a824eeb9235d80536128176894db7cf0dbb20a5fae6814f136f109b741d940a`.
The remaining qualification scope appears in the gate table below and in the
package's `qualification.json`. The following paragraphs retain earlier
candidate history; their pending checks do not supersede the current receipts.

The preceding I2S reset/startup candidate,
RBF `deb79999`, passes the complete internal timing gate, separate ten-minute
Studio/player music soaks, a delayed native RUN-read recovery test and four
cold-entry/cartridge-selection/reload cycles. Its native Studio cartridge and
worker fault recovery suite also passes. Each completed suite verifies
restoration of the canonical installation. Physical
display/audio confirmation remains incomplete for this exact candidate.

A subsequent working-source change adds shared asynchronous assertion and
three-audio-edge reset release to the platform `audio_out` module. Its digital
proof, 450 pulse/release cases, 4,096 full I2S pipeline cases and three negative
controls pass. The seed-22 full compile and new fitted reset consumer audit
now pass on the seed-22 fit. All 140 internal checks are positive (minimum
0.118 ns), and all six fitted audits pass. The reset audit covers 398 consumers
at each of four corners. Fresh functional-netlist checks retain I2S startup
values of 0/1/0. This RBF, `fd673c65`, also passes fresh conditional HDMI and
I2S output analyses and is frozen for private hardware tests. Its Studio
ten-minute music phase passes. A separate player ten-minute run now also
passes with 3,047 audio samples, zero underruns and 21 remote memory/save
checkpoints. Their maximum capture gap is 30.106 seconds, within the unchanged
40-second limit, even across a deliberate 35-second observer SSH disconnect.
Both original player monitors exit 0; canonical payloads/settings and Tetris
are restored at that suite's end. Studio parent TERM/KILL recovery also
passes. The first player's failed checkpoint coverage remains preserved.
The fresh delayed native RUN-read test also passes: ten silent PCM snapshots,
nine unchanged complete-frame snapshots, the cancelled save preserved and
an explicit retry to BOOT 10. Observed worker departure is bracketed at
235.577–254.448 ms. Lifecycle coordinator 19791 stops after 27 completed steps
when diagnostic `ls -l` races a closing descriptor. It exits 1 and verifies
canonical restoration; this does not qualify the full suite. The failure and
original observer are sealed in
`build/platform-audio-reset-lifecycle-failure-progress-20261004.json`.
The corrected observer accepts only this PID's vanished numeric-fd diagnostic;
the unchanged descriptor-count limit still rejects persistent growth. Its
regression and registered CTest pass. Fresh coordinator 23309 completes the
same four-cycle suite with source-bound observer bytes and native status 0.
All four cold entries, twelve cartridge selections and sixty-four raw reloads
pass with BOOT counts 1–76, CRC-valid saves, retained same-core supervisors,
unchanged memory/descriptor limits and zero observed audio underruns. All
original monitor jobs are collected with status 0. Canonical binaries/settings,
Tetris, HDMI and audio are verified restored at that suite's end. Evidence is
recorded in `build/platform-audio-reset-lifecycle-fd-observer-progress-20261004.json`.
Human picture/stereo
confirmation remains pending for this RBF. It is not an installed
or accepted release package.

Separate player-parent TERM/KILL cases now pass on the same candidate. Original
coordinator 48067 exits 0; the installed daemon automatically relaunches the
player, the old observed VM departs, CRC-valid saved counters/sentinel are
retained, and an explicit cartridge retry runs under the new parent. Settled
audio checks and HDMI pass; canonical payloads/settings, Tetris and output
are verified restored. Evidence is sealed in
`build/platform-audio-reset-player-exit-progress-20261004.json`. This does not
claim continuous audio during the dead-parent interval or physical power-loss
durability. Four Main TERM/KILL cases also pass for Studio and player.
Original coordinator 11040 exits 0 and verifies canonical restoration. Healthy
playback continues while Main is absent; each explicit restart against the
loaded FPGA produces one initialization BOOT and retains the supervisor and
CRC-valid save/sentinel. Actual BOOT counts are 1–8. This does not claim an
automatic system Main restart or a kill during SPI/cart transfer. Evidence is
recorded in `build/platform-audio-reset-main-exit-progress-20261004.json`.
The rollback can start a missing test-owned Main before using the command FIFO;
its local guard checks reject unrelated ownership, changed payloads and
duplicate launch, including after a lost launch reply.

Fresh Studio fault coordinator 11607 completes the predecessor suite against
shared-reset RBF `fd673c65` with native status 0. Main and both frontend binary hashes
are unchanged from that predecessor; the fixture, thresholds and corrected
MGL delays are retained. The new driver binds the corrected observer source
and creates a fresh private journal. All 16 checkpoints pass: corrupt/oversized
rejection, missing and superseded transfers, the extracted 4 MiB boundary,
first/late script hangs, abrupt worker death and retained-cartridge retry.
Three actual Main keyboard RUN retries and three worker recoveries complete;
the supervisor remains. Canonical binaries/settings, Tetris, HDMI and audio
are verified restored. The original jobs are collected with status 0. Evidence
is sealed in `build/platform-audio-reset-studio-fault-progress-20261004.json`.
This finite fault suite does not close broader workload, physical or CDC gates.

The digital Main/HPS transport suite now interrupts execution before every GPIO
write or read in a metadata/cartridge transaction. All 858 cuts pass, including
846 with chip select asserted and 360 with the strobe asserted. The model retains
the GPIO value at departure, then uses Main's initialization write and core-ID
exchange for retry. Unfinished prefixes never publish as successful cartridges;
the replacement payload and source path match. A loader mutant that accepts
changed-index bytes fails the publication check. All ten registered transport
regressions and the separate final-DDR-drain scenario pass. Main source/build
and the loader are bound to the frozen candidate; production binaries are
unchanged. Evidence is sealed in `build/hps-abrupt-restart-progress-20261004.json`.
This models transport restart with a bounded consumer ACK, not full Main
initialization or a physical Main kill during transfer.

A static ARM transfer-signal instrument is prepared for that physical test.
It maps DDR read-only, verifies the selected core, identity/geometry and coherent
session, binds Main by PID/start time/executable path/SHA-256 and a pidfd, and
requires a receiving ticket within explicit byte bounds. It sends at most one
TERM or KILL; a pass also requires the same transfer still incomplete after
target exit. Thirteen Linux integration tests against owned local children
pass, including ignored TERM without retry, executable replacement after
arming, cancellation, and rejection when the transfer completes before exit.
The same source builds statically for ARM, and registered CTest passes. Evidence
is sealed in `build/main-transfer-signal-tool-progress-20261004.json`. This is
tested preparation only; it has not run against physical Main or MiSTer DDR.

The eight-case physical driver and its raw 4 MiB fixture are now prepared and
locally checked. The fixture preserves the known native cartridge and adds 64
ignored dummy chunks; the actual native envelope validator accepts it and rejects
a truncated copy. Each planned case starts the known cartridge, waits for its
save, then arms for the delayed second transfer. TERM/KILL and early/late byte
windows cover both Studio and player. Seven local driver review tests pass:
incomplete/failed or tampered soak evidence is rejected, signal records must bind
the target and an incomplete transfer, and rollback collects original signal
jobs before restarting Main or using its FIFO. The actual prepared CLI exits
before SSH access when its required release receipt is absent. It requires a
positive raw-reviewed, restored one-hour soak, with archived native inputs and
the installed state bound by hashes. The qualifying soak receipt has now been
issued. Original coordinator 45056 exits 1 after all four Studio TERM/KILL
early/late cases pass actual receiving-state departure, explicit Main restart,
retained supervisor/save and valid cartridge retry. The first player TERM
early case dispatches inside the window at 3,514 bytes, but its departure
sample is 66,431 bytes, 895 beyond the unchanged 65,536-byte limit. Its ticket
still indicates the same pending transfer. The probe's old `still_receiving`
field combines pending state with the byte-window test; false here does not
indicate transfer completion. All five original jobs are collected (four exit
0, one exits 1), and the driver verifies canonical payload/settings, Tetris,
HDMI and audio restored. The full suite remains unqualified. Original failure
and raw traces are sealed in
`build/main-transfer-window-failure-progress-20261005.json`. Preparation is sealed in
`build/main-transfer-preparation-progress-20261004.json`.

The probe source now sends its one pidfd signal before writing the dispatch
record, avoiding SD/pipe backpressure between the qualifying snapshot and
signal syscall. It records syscall return time/result and separates pending
transfer state from the unchanged byte-window predicate. All fifteen owned-child
guards and the registered CTest pass; the original probe fails the new blocked
log-pipe test because the target remains alive before its log can be written.
A pending transfer beyond the byte window still fails qualification. The first
new harness run's extra open pipe writer caused an EOF timeout; that failure
is retained and its harness corrected before the passing fresh build.
ARM probe `5823cd25` is prepared without changing Main, FPGA or either runtime.
A fresh suite runs under original coordinator 23002 with the probe on CPU 1
at nice 0, avoiding the player's CPU 0 at nice -10. All original byte windows
and recovery assertions remain. Source/dispatch guards and six invalid raw
record counterexamples pass before launch. This addresses observed probe risks;
it does not prove the original board delay's root cause. Original coordinator
23002 now exits 0 after all eight physical cases pass. Its original signal
jobs are each collected with status 0. Independent raw review verifies the
unchanged early/late byte windows, successful single signal calls, same pending
transfer/session at departure, actual save CRCs and retained sentinel, exact
BOOT counts 1–24, retained supervisors, valid source-ticket retries and HDMI.
All 4,960 sampled case audio records have zero underruns. Snapshot-to-signal
return intervals are 33,051–66,591 ns; they bound the observed syscall interval,
not the exact SPI edge or physical signal boundary. Canonical Main/handler,
payload/settings, Tetris, audio and HDMI are verified restored. Full raw review
is sealed in `build/main-transfer-probe-v4-progress-20261005.json`.
This does not qualify every SPI edge, automatic Main respawn or physical power loss.

Original coordinator 13780 completes one-hour Studio and one-hour player music
phases on the same frozen RBF. Both memory/save and audio monitors run directly
on the MiSTer; a deliberate observer SSH disconnect does not restart them.
The original Studio jobs complete with status 0. An independent raw review
passes 3,600.167 seconds and 18,267 audio samples with zero underruns, 121
memory/save checkpoints, a maximum capture gap of 30.100 seconds and flat
sampled steady parent/worker RSS. Audio measures 47,999.225 Hz and game timing
59.99888 Hz; fitted queue change is 0.008585 ms. The actual final save is
CRC-valid with one BOOT and a largest recorded game gap of 31 ms. These are
bounded Studio results. Player raw review also passes 3,599.983 seconds,
18,275 audio samples with zero underruns, 121 memory/save checkpoints and
a maximum capture gap of 30.075 seconds. Sampled steady RSS ranges remain
zero for parent and worker; audio measures 47,999.233 Hz, game timing
59.99890 Hz and fitted queue change is -0.070895 ms. Its final CRC-valid
save has one BOOT and a largest game gap of 31 ms. All four original remote
jobs are collected with status 0. The original coordinator exits 0 (terminal
tool chunk `8e64ff`), and canonical Main/handler/payload/settings, Tetris,
HDMI and audio are verified restored. Independent raw review is sealed in
`build/platform-audio-reset-hour-soak-progress-20261005.json`; its bound
transfer-test release is issued. This does not qualify broader workloads,
physical signal continuity or a board network outage.

Independent offline input coverage now finds defects in the frozen runtime
bindings: Wren `btnp()` aliases controller IDs 16–31 to 0–15; Ruby rejects
Escape/function/keypad IDs and returns a truthy numeric zero for default
`keyp()`; Scheme and Janet default keyboard queries use byte 255 instead of
the core's any-key sentinel. Working-source repairs retain the pinned checkout.
The expanded contract passes all fourteen runtimes in direct and supervised
players, with two interleaved peers, all 32 gamepad bits, ten keyboard IDs,
press/repeat/release, any-key queries and replacement during held input. Its
registered CTest passes. Eight private reverted repairs fail their independent
input expectations. A generated-source review proves exactly nine changed
binding lines and no other generated binding changes.

Fresh ARM products `1fd795b5` (player) and `49667c08` (Studio), plus input test
`4101f59c`, are prepared using four copied, repaired binding archives. Relinking
the original products first reproduces both frozen binary hashes exactly;
all original SDK objects/archives and frozen products remain byte-identical.
The prepared products pass inspection against the recorded MiSTer provider
ABI. The standalone input contract now executes on MiSTer with all fourteen
repaired runtimes passing. The new production player/Studio products have
not replaced installed payloads; combined integration remains pending. The
first Wren test failure, Ruby range failure, Forth fixture's use of standard
`KEY` instead of TIC's `KEYPRESSED`, configure dispatch errors and compiler
lookup failures remain preserved; they are not successful runs.

The native input-test driver passes eight local ownership, terminal-status,
log, lost-reply and isolated-shell tests. Coordinator 67875 then exits 1 on
its initial SSH timeout, before core selection, upload or remote job dispatch.
Its original failure is retained. A fresh v2 driver adds bounded read-only
initial connection recovery. Original coordinator 25858 exits 0 after its
single dispatched native job completes with status 0. All fourteen runtime
contracts pass on ARM with direct and supervised peers, all four gamepads'
bits, independent keyboard edges/repeats/any-key queries and replacement
during held input. It verifies canonical payload/settings, Tetris, sampled
audio and HDMI restored. This is synthetic input; it does not qualify actual
physical controllers or keyboards, and the new production products still
need private combined integration.

Additional mouse contracts now pass all fourteen runtimes on the host. Two
interleaved direct/supervised peers cover absolute border offsets, signed
relative coordinates including -128/127, all eight button masks, both signed
six-bit wheel limits (-32/31), two mouse queries per TIC and replacement of
one peer. The WASM fixture writes negative values as unsigned PMEM words,
preserving its distinct signed-i64 -1 getter sentinel. The actual host test
and source snapshots are retained in `build/runtime-mouse-contracts-host-20261005`.
ARM test `04edba5c` is compiled with the exact repaired runtime archives and
passes inspection against recorded MiSTer providers. Its native driver rejects
missing original coordinator 23002 terminal evidence before SSH; even a failed
predecessor must have collected original jobs and verified canonical restoration.
The first native coordinator 77253 exits 1 before upload or job dispatch when
an immediate post-MENU Main singleton assertion fails. It verifies Tetris and
canonical state restored. A read-only inventory subsequently verifies one
canonical Main. Main's core-loader source explicitly uses a double fork and
exec to restart itself. A fresh v2 driver waits, within the original MENU
deadline, for exactly one canonical Main path/hash and a stable second PID
enumeration. Missing/duplicate Main, foreign path/hash and another core all
fail local counterexamples. The original failure is retained. Original v2
coordinator 78524 exits 0; its one native job passes all fourteen mouse
contracts. Actual Main observations settle from two PIDs to one canonical
Main before upload. Canonical payload/settings, Tetris, audio and HDMI are
verified restored. Evidence is sealed in
`build/runtime-mouse-native-progress-20261005.json` (`7b912ce9`).
This synthetic contract does not qualify a physical USB mouse.
No production frontend or FPGA change was needed for this added API coverage.

The repaired combined candidate is frozen as
`build/runtime-input-repaired-candidate-seed22-20261005`, manifest `38e60e9d`.
It contains repaired player `1fd795b5` and Studio `49667c08`, with byte-identical
Main `81253dc2` and FPGA `fd673c65`. The parent candidate and ARM build/input
evidence remain bound by hashes. Original coordinator 87591 now tests this
combination in a private layout. Its Studio phase has fresh user confirmation
of stable Samsung HDMI 720p/60 picture and clear stereo audio, recorded in
`build/runtime-input-integration-studio-human-20261005.json`.
The Studio ten-minute phase has now passed, with both original monitors
collected at status 0. Independent review verifies 3,045 Studio audio samples
with zero underruns, 21 memory/save checkpoints and flat sampled steady RSS.
The player phase has fresh user confirmation of stable picture and clear stereo audio in
`build/runtime-input-integration-player-human-20261005.json`.
Original coordinator 87591 exits 1 when its unguarded reconnect after the
player observer disconnect times out. All four original detached jobs are
collected with status 0, and canonical Main/handler/payload/settings, Tetris,
audio and HDMI are verified restored. Independent raw review verifies
600.062 seconds and 3,047 player audio samples with zero underruns, 21 valid
memory/save checkpoints and flat sampled steady RSS. The final player save
and post-phase clock checks did not complete; successful monitors do not
qualify the entire failed suite. This evidence is sealed in
`build/runtime-input-integration-failure-progress-20261005.json` (`00585282`).
A fresh two-frontend ten-minute music/save/clock/memory driver was
prepared with unchanged drift, capture-gap and RSS limits; it rejects a
missing original Main-transfer terminal receipt before network access.
Its first preparation remains bound to failed coordinator 45056 and is not
executed. The fresh v2 preparation binds coordinator 23002, its exact new driver
and native folder, and rejects a missing terminal observation before SSH.
It requires the current physical transfer suite to finish successfully and
restore the canonical installation before claiming the board.

An additional host contract finds the WASM mouse import accepts a destination
that extends past linear memory. An owned pre-fix process fails its expected
out-of-bounds trap at memory size minus eight. The generated adapter now checks
the integer destination before creating a host pointer and writes exactly the
nine public result bytes, including unaligned destinations. The repair passes
22 valid/alignment calls, 24 rejected destinations with unchanged linear memory,
two interleaved runtimes, a supervised cartridge fault and replacement, all
fourteen normal mouse/input contracts and the existing WASM return-slot contract.
Fresh host evidence is in `build/wasm-mouse-bounds-fixed-host-20261005`.
The original archives and preceding frozen candidate are unchanged. Fresh ARM
products reproduce the preceding repaired player/Studio hashes before adding
the mouse repair. New products are player `ed14e37b` and Studio `d0c0a0f4`.
Original native coordinator 39255 exits 0 after one dispatched job passes the
bounds contract and all fourteen normal mouse/input contracts. All three
individual statuses and the aggregate status are 0; actual provider hashes
are verified. Canonical payload/settings, Tetris, sampled audio and HDMI are
restored. Evidence is sealed in
`build/wasm-mouse-bounds-native-progress-20261005.json` (`dcbab02e`).
This remains synthetic input and does not qualify the new production frontend
integration or physical devices.

The new combined candidate is frozen in
`build/runtime-wasm-mouse-repaired-candidate-seed22-20261005`, manifest `212cb1c7`.
It retains Main `81253dc2` and FPGA `fd673c65`. A fresh two-frontend ten-minute
driver requires original coordinator 39255's terminal, collected statuses and
canonical restoration before network access. Its intentional observer reconnect
now uses bounded read-only recovery: an owned test reproduces the old immediate
failure and verifies recovery after two simulated timeouts, without restarting
monitors or mutations. Clock, RSS, queue, save and capture-gap limits are unchanged.
Original private integration coordinator 69253 now exits 0 after both ten-minute
phases pass. All four original jobs are collected at status 0. Canonical Main,
handler, payload/settings, Tetris, audio and HDMI are verified restored.
Independent raw review is sealed in
`build/runtime-wasm-mouse-integration-soak-progress-20261005.json` (`2e08edb3`).
Studio/player have 3,045/3,047 audio samples, zero underruns and 21 memory/save
checkpoints each, with flat sampled steady RSS. Fresh physical confirmation
for these exact frontends is not inferred from earlier replies.

A further owned host probe confirms that `trace()` accepts an invalid text
pointer without trapping. Working-source repairs now validate complete strings,
transparency arrays, map descriptors and map result destinations before access.
Optional transparency pointer zero behaves as C NULL. Unaligned remap transfer
uses copies and explicit result bytes; callbacks require four i32 arguments and
no result. Host contracts pass boundary strings/colors, rejected calls with
unchanged memory, NULL colors, an unaligned descriptor/result and invalid callback
tables/signatures. Six actual faulting cartridges return supervised errors and
valid replacements run afterward. The mouse bounds, fourteen-runtime normal
mouse/input and WASM result-slot regressions also pass. Evidence is in
`build/wasm-pointer-fixed-host-v3-20261005`. The corresponding ARM build passes
the same native pointer, mouse and input contracts on MiSTer. Original session
9656 exits 0, collects all phase statuses at 0 and verifies canonical payloads,
settings, Tetris, audio and HDMI restored. Independent review is sealed in
`build/wasm-pointer-native-progress-20261005.json` (`2e62a771`). Production
player/Studio integration for this broader repair remains pending.
The preceding failed strict compile, incorrect configure anchor and attempted
build against the incomplete configure are retained separately. The frozen
playback candidate does not contain this broader pointer repair.

A subsequent refinement stores the unaligned remap destination as a byte
pointer. Expanded fixtures explicitly reject a callback with four floating-point
arguments and a callback returning a value. Fresh host pointer, mouse and input
contracts pass in `build/wasm-pointer-alignment-host-20261005`; its WASM archive
is byte-identical to the preceding host repair. Fresh ARM build and native checks
also pass. Original session 62288 collects every phase at status 0, exits 0 and
verifies canonical restoration. The expanded suite is sealed in
`build/wasm-pointer-alignment-native-progress-20261005.json` (`e6580435`). The
configured CTest registration passes with the independently built exact-source
host binary; this does not claim a full CMake target build. The ARM player/Studio
are byte-identical to the preceding broader pointer repair.

The exact source and ARM products are frozen in
`build/runtime-wasm-pointer-repaired-candidate-seed22-20261005` (manifest
`586664a0`). Its player/Studio are `7c358063`/`e0823fed`; Main `81253dc2` and RBF
`fd673c65` are unchanged. A fresh private ten-minute playback suite for each
frontend passes with bounded observer reconnects and detached audio, save and
memory monitors. Studio/player have 3,046/3,047 audio samples, zero underruns
and 21 checkpoints each with flat sampled steady RSS. Both clocks remain
approximately 48 kHz/60 Hz; fitted queue changes are +0.089/-0.084 ms.
Original session 39988 exits 0, collects all four original jobs at status 0 and
verifies canonical Main/handler/payloads/settings, Tetris, audio and HDMI restored.
Independent raw review is sealed in
`build/runtime-wasm-pointer-integration-soak-progress-20261005.json` (`994175fe`).
It remains an unaccepted candidate; prior physical
confirmations and prior production playback do not qualify these new frontends.

A human-operated Xbox/mouse diagnostic is prepared separately in
`build/final-input-physical-fixture-v2-20261005`. It displays the D-pad/face
actions and WASD/Enter/Esc/Q/E as separate labels, preserves press counters in
a fresh UUID save and verifies releases. The host idle run completes 180 ticks
with zero mouse movement. The first fixture's unsigned-save/negative-coordinate
counter error is retained separately. Its private driver preparation is
`build/final-input-candidate-v2-preparation-20261005`: it requires successful
termination, collection, restoration and sealed review of original playback
session 39988 before any SSH connection or core mutation. Idle, incomplete and
restarted input sequences are rejected. The earlier helper with incorrect
prerequisite paths was prepared but never executed; the corrected helper checks
the gate remains exactly unchanged during naming. No physical input is qualified
by these preparations, and no input is injected by that driver.
The corrected driver's pure prerequisite now passes against the actual completed
and sealed playback suite. The physical driver has not been dispatched; human
availability is pending.

A further WASM memory-growth probe reproduces a stale TIC RAM pointer when
the interpreter reallocates linear memory (`memory.grow`: four to five pages,
one allocator move, RAM pointer unsynchronized). The baseline is retained in
`build/wasm-memory-growth-baseline-v2-20261005`. The working source now reserves
the documented 256 KiB backing once, keeps logical growth within the machine
and module limits, and rejects incompatible startup memory. Newly accessible
pages are zeroed without relocating the backing. Allocation failure releases
the partial runtime and permits a later cartridge replacement.

Fresh host tests pass all sixteen defined/imported memory cases, callback-time
growth, overflow rejection, zero-fill and allocation-failure recovery. Five
supervised invalid-memory cartridges each permit a valid replacement. Existing
pointer, mouse-bounds, fourteen-language mouse/input and WASM result-slot tests
also pass in `build/wasm-stable-memory-host-v2-20261005`. Both configured CTest
registrations pass with those independently built binaries. The helper exits 1
after the tests because its output-summary assertion expects different wording;
the separate review preserves that failure and verifies the original successful
test output without rerunning it (`build/wasm-stable-memory-ctest-review-v2-20261005`).
The first review's incorrect assumption about wrapper-log content is also
preserved. These checks do not claim a full CMake target build. The new memory
repair is not in frozen candidate `586664a0` and still requires native ARM and
production frontend qualification.

The corresponding ARM build passes the recorded MiSTer provider ABI checks.
Original native coordinator 24319 collects memory, pointer and fourteen-runtime
mouse/input phases at status 0. It exits 1 during canonical restoration because
its immediate source-ticket check observes a handoff before it settles; this
does not qualify the complete suite. A separate read-only observation verifies
the canonical payloads, Tetris source ticket 1090, zero audio underruns and the
correct HDMI clock (`build/wasm-stable-memory-restoration-observation-v3-20261005`).
The observer's earlier missing-host-key-file and inherited worker-name assumptions
are preserved as failed preparations. No original native job was restarted.
A fresh coordinator adds a bounded wait for a complete source ticket; its pure
checks accept a pending-to-ready handoff and reject a persistent mismatch or
incorrect cartridge size. The previous failure and later restoration observations
are retained in `build/wasm-stable-memory-native-v3-preparation-20261005`.

Fresh native coordinator 8885 passes the complete memory, pointer and fourteen
mouse/input language contracts and verifies canonical Tetris, audio and HDMI
restoration. All six phase statuses are 0, and the original coordinator exits 0.
Independent review, including the preceding failed attempt and later read-only
restoration, is sealed in `build/wasm-stable-memory-native-v3-progress-20261005.json`
(`5147f987`). Exact ARM products and source are frozen in
`build/runtime-wasm-stable-memory-candidate-seed22-20261005` (manifest `cf661c69`):
player `a254f7ff`, Studio `4ab49c67`, unchanged Main `81253dc2` and RBF `fd673c65`.
A fresh private ten-minute Studio/player integration suite now passes on this
exact frozen candidate. Original coordinator 21431 exits 0, collects all four
original monitor jobs at status 0 and verifies canonical files/settings, Tetris,
audio and HDMI restored. Studio/player have 3,045/3,047 samples, zero underruns
and 21 memory/save checkpoints each; sampled steady parent/worker RSS is flat.
Audio clocks are approximately 47,999.22 Hz, game clocks approximately 60 Hz,
and fitted queue changes are +0.036/-0.043 ms. Both phases recover after deliberate
35-second observer disconnects without restarting their detached monitors.
Independent review is sealed in
`build/runtime-wasm-stable-memory-integration-soak-progress-20261005.json`
(`afd1bae1`). Fresh human confirmation remains pending for these exact frontends.

A separate fresh configure now builds the actual CMake `wasm` target and links
its resulting archive into both exact-source memory and pointer contracts; both
pass (`build/wasm-stable-memory-cmake-target-20261005`). The archive contains one
new environment object, no original environment object and exactly one defined
`ResizeMemory` entry point. Earlier host products remain unchanged. This validates
the CMake library integration; it does not claim a full CMake build of all test
or frontend targets.

A separate probe confirms the remaining parser issue: an imported zero maximum,
a minimum exceeding a zero maximum, a maximum exceeding the WASM32 limit and
a truncated maximum all start without errors. The expected failing child and
valid imported-memory control are retained in
`build/wasm-memory-declaration-baseline-20261005`. The parser discards the explicit
maximum flag, does not validate the limits, and its defined-memory section
ignores the memory-type parse result. The working source now validates supported
memory flags and limit relationships before losing that flag, rejects memory
types incompatible with TIC-80 RAM, and propagates defined-memory parsing errors.
The pinned vendor files remain unchanged; the adapter is source-hash checked.

The new actual CMake WASM target passes four supported types, twelve rejected
limits/encodings and sixteen supervised replacements, alongside all six preceding
regression groups (`build/wasm-memory-declaration-fixed-host-20261005`). Its new
registered CTest passes with the independently built exact-source executable.
This does not claim a full CMake build of test targets. Its actual ARM CMake
WASM target builds, exact parent frontend relinks pass, and all seven new
products pass the recorded board-library ABI checks. Frozen candidate
`cf661c69` does not contain it.

Original native declaration coordinator 2851 now passes all five contract
phases, collects all seven phase/aggregate statuses at 0, exits 0 and verifies
canonical Tetris, audio and HDMI restoration. Independent review is sealed in
`build/wasm-memory-declaration-native-progress-20261005.json` (`7958fd8f`). The
four supported and twelve rejected declarations each permit a valid supervised
replacement, and the preceding memory, pointer and fourteen-language mouse/input
contracts pass. Original production-worker coordinator 71529 passes the player
cases but fails Studio startup because the helper intercepts `posix_spawn`,
while Studio uses `tm_spawn_closed`. This harness failure and canonical
restoration are preserved. The corrected helper intercepts Studio's actual
launcher. Fresh coordinator 76571 passes all sixteen declarations and sixteen
replacements in each production executable, verifies 32 actual player workers
and one retained Studio worker, exits 0 and restores canonical Tetris, audio
and HDMI. Evidence is sealed in
`build/wasm-production-worker-native-v3-progress-20261005.json`. Candidate
`runtime-wasm-declaration-candidate-seed22-20261005` (manifest `05b7f4ad`)
contains player `6dfae0d1` and Studio `5b4f0f57`, with unchanged Main `81253dc2`
and RBF `fd673c65`. Original playback coordinator 41036 exits 0 after separate
ten-minute Studio/player music runs. Independent raw review passes 3,045/3,047
audio samples, zero underruns, 21 memory/save checkpoints per mode and flat
sampled steady parent/worker RSS. Audio clocks are approximately 47,999.23 Hz,
game clocks approximately 60 Hz and fitted queue changes +0.129/-0.078 ms.
All four original monitors finish with status 0 and are collected once.
Canonical files/settings, Tetris, audio and HDMI are verified restored.
Evidence is sealed in
`build/runtime-wasm-declaration-integration-soak-progress-20261005.json`.
Fresh picture/audio confirmation and final physical input remain pending.
The final Xbox/mouse driver is prepared against this exact candidate and the
completed playback receipt; it has not been dispatched while human availability
is pending.

The broader matrix then finds a concrete regression in this candidate. Original
coordinator 21720 matches the first ten native Studio demos exactly, but the
standard WASM demo is rejected with `WASM imported memory does not fit TIC-80 RAM`.
Its full frame differs from both desktop reference poses. The extracted module
is byte-identical to the pinned upstream 625-byte demo and imports memory with
minimum and maximum two pages. The new import guard incorrectly requires four
pages even though TIC RAM fits two. Coordinator 21720 exits 1 and verifies
canonical files/settings, Tetris, audio and HDMI restoration. This failure is
sealed in `build/runtime-declaration-matrix-v3-failure-progress-20261005.json`.
The prepared input driver for `05b7f4ad` is superseded and will not be dispatched.
The working source now applies compatible imported logical limits during memory
initialization, retaining the same stable 256 KiB backing. Expanded contracts
cover two- and three-page imports and an incompatible one-page maximum. A full
standard-demo frame regression now passes on the host, including exact
30/60-tick RGBA matches and stable two-page RAM. The host also passes 20 memory
cases, eight supported declarations, thirteen rejected declarations and
twenty-one supervised replacements, plus prior pointer/mouse/input/result
regressions. Exact-parent ARM relinks and all eight product ABI checks pass.
The new production binaries are player `dfe36c88` and Studio `9123c34a`.
Production-worker helpers build against these binaries with the corrected
Studio launcher interception. Local proof is sealed in
`build/wasm-import-limits-build-progress-20261005.json`. No native contract,
actual-worker or full-matrix pass is claimed for these new binaries yet.

The unchanged six ARM contract executables subsequently exit 0 under local QEMU
with SDK glibc. These include the exact standard-demo frames, 20 memory cases,
21 declaration/replacement cases, pointer recovery and all 14 runtime
mouse/controller contracts. The original coordinator 24684 exits 1 because its
summary supplied only four of the six completed logs. A separate review checks
all six original logs successfully and preserves the original failure. Evidence
is sealed in `build/wasm-import-limits-emulated-progress-20261005.json`.
This is emulated execution; it does not qualify the board runtime or actual
production frontend workers. The six-phase native shell passes an offline
syntax check, but remains undispatched because hardware is unavailable.

A fresh desktop CMake build also builds the three memory/demo targets from
scratch. Both registered memory tests and the independent standard-demo frame
check pass. The same fresh build then produces the actual desktop player and
Studio executables. Their supervised contracts pass all 21 declaration cases
and 21 valid replacements per frontend, verifying 42 distinct player worker
starts and one retained Studio worker. Original coordinators 2449 and 52950
both exit 0. Evidence is sealed in
`build/wasm-import-offline-production-progress-20261005.json`, without duplicate
ELF archives. These desktop checks leave ARM production-worker, native playback,
full cartridge-matrix and physical input/display/audio qualification pending.

The corrected desktop workers also pass the full frozen demo matrix: both
frontends, fourteen languages and three cartridge formats, for 84 cases and
84 verified real worker starts. All 168 full RGBA frames match the independent
30/60-tick references; all 5,040 PCM tick buffers match the frozen direct-runtime
reference. Each Studio case starts a fresh worker; retained-worker replacement
is covered separately above. The original coordinator 57561 exits 1 after
both matrix executables exit 0 because it rejects the upstream Studio console's
unterminated `>` prompt before success lines. A new review accepts only that
exact documented prefix, verifies every saved frame independently and preserves
the original failure. Evidence is sealed in
`build/wasm-import-runtime-matrix-host-progress-20261005.json`.
Matching ARM helpers build and pass recorded-provider ABI checks; original
builder 65184 exits 0. Their preparation proof is
`build/wasm-import-runtime-matrix-arm-progress-20261005.json`. Neither helper
has run on MiSTer, and these desktop results do not qualify FPGA publication,
cartridge-source acknowledgement or physical output/input.

Original coordinator 39283 also builds and runs 28 selected registered runtime
regressions in the fresh CMake configuration. All pass in 214.21 seconds with
no skipped cases: language/input and banked-asset contracts, sound bounds,
defaults and note parsing, FFT/VQT bindings and synthetic capture, spectrum
argument handling, Forth stack recovery, VM/PNG recovery, WASM result slots
and pointer bounds, and basic cartridge/clock/memory checks. Existing memory
CTest results are preserved rather than repeated; production binaries and
runtime sources remain unchanged. Evidence is sealed in
`build/wasm-import-runtime-regressions-host-progress-20261005.json`.
Capture-device boundaries are deterministic test doubles. No physical
microphone, MiSTer or display is accessed, and the batch does not claim every
project test or any new native/physical qualification.

The working source now also corrects the platform I2S reset phase. It passes
4,096 full audio-pipeline reset/rate-change cases and digital edge-phase formal
assertions. Two seed-21 fits fail core/DDR hold timing and are rejected. An
isolated Quartus compile also reproduces an ignored ANSI output initializer;
module-scope state registers fix that compiler startup mismatch. The corrected
source passes the same simulation and six-assertion formal proof. A stronger
seven-assertion proof includes the exact platform enable divider with arbitrary
defined startup states and no input assumptions. The seed-22 full fit passes
all 140 timing checks and five fitted audits; its overall minimum is 0.031 ns
on core/DDR hold. Its full functional netlist starts SCLK/LRCLK/data at 0/1/0.
The exact frozen RBF is `deb79999`; its fresh private Studio/player music soaks
both pass for ten minutes with zero underruns, valid saves and flat sampled
memory. Canonical Main/handler/payloads/settings and Tetris are verified restored.
It is not accepted as a release or permanently installed.

This audit follows the six outstanding gates in `validation.md` and the
hardware release requirements in `bring-up.md`. A bounded pass below does not
close a broader gate.

| Gate | Current evidence | Still unverified |
| --- | --- | --- |
| Transport, restart and profiling | Shared-reset RBF `fd673c65`: bounded Studio/player music phases pass with zero underruns, valid saves and flat sampled memory. Fresh delayed RUN-read recovery passes; observed worker disappearance is bracketed at 235.577–254.448 ms. Studio and player parent TERM/KILL tests pass automatic daemon relaunch, old-worker departure and valid save/retry checks. Four Main TERM/KILL cases pass healthy playback while absent and explicit restart with retained supervisor and one BOOT. Fresh lifecycle testing passes four cold entries, twelve selections and sixty-four reloads, with exact BOOTs 1–76, unchanged memory/descriptor limits and canonical restoration. All 16 Studio file/hang/worker-kill checkpoints pass on this RBF. Digital transport retry passes 858 GPIO interruption points and rejects a prefix-accepting mutant; ten transport regressions and final DDR drain pass. | Broader transport faults, every physical transfer edge, workload/clock-drift profiling and longer varied sessions. The one-hour Studio/player music/save soak passes independent raw review with zero observed underruns and flat sampled steady RSS. Main recovery uses explicit restart; the digital cut suite does not model full Main initialization. Sampled worker disappearance does not identify an exact cancellation timestamp. |
| Physical input and audible cartridges | Current build `18de1e89` passes physical Xbox, mouse and 22-key Bluetooth keyboard diagnostics, including separate WASD/Enter/Esc/Q/E, modifier chords, repeat and releases. Both frontends pass ten-minute music tests with fresh human picture/stereo confirmation. | Broader peripherals, four-controller sessions, audible cartridges and physical Studio editor workflows. |
| Timing, CDC and external I/O | Shared-reset candidate passes all 140 internal checks and six source-bound fitted audits; minimum internal slack is 0.118 ns. The fitted shared-reset consumer audit covers 398 consumers at each of four corners. All 116 custom chains have calculated MTBF at four corners. Both scaler divider stages have equivalence evidence. Compiler startup, exact-enable I2S formal assertions, pulse simulations and 4,096 full-pipeline reset/rate cases pass. Conditional zero-skew HDMI and I2S output checks are positive. | Full platform/reset CDC, physical qualification, complete external-I/O modeling, unconstrained external endpoints and physical metastability qualification remain. Conditional checks do not measure PCB skew, clock jitter or physical receiver timing. |
| Display modes and latency | Current Studio and player have confirmed Samsung HDMI picture/stereo at 720p/60. Final installed Tetris measures 74.25 MHz with zero measured clock error and zero audio underruns in its bounded check. | Analog/CRT modes, broader HDMI geometry, tearing, latency and long-session signal behavior. Published frame/PCM checks do not replace physical capture. |
| Persistence and launcher/device stress | Native SD writer error/crash, save rejection, failed-rename retry, blocked reads and worker cancellation pass. Bounded launcher/reload suites pass. The matched installation preserves settings and has verified backups; actual full Main/payload rollback passes. Keyboard wake/reconnection is observed once. | Physical power-loss durability, Bluetooth power-cycle/reconnect coverage, broader mouse devices, physical horizontal wheel, four controllers and wider launcher/reset stress. |
| Runtime/API and Studio compatibility | Current build passes all 84 Main/FPGA language/format/frontend cases with exact full RGB frames, plus native worker RGBA/PCM comparisons and bounded declaration, bank/palette/SFX/language API contracts. Console/editor/SURF and recovery have documented bounded checks. | Broader cartridge/API and Studio feature coverage, plus real microphone/FFT capture. No capture device is available. |

The latest source-progress receipt is
`build/platform-audio-reset-source-progress-20261004.json`; its parent is the
completed `build/i2s-module-init-studio-fault-progress-20261004.json`, with corrected and
baseline audio simulations in its parent chain. The full pinned audio model confirms steady-state edge
spacing but records 768 mid-session LRCLK/rising-BCLK reset coincidences. The
phase-preserving override passes 636,672 channel-word checks with zero such
coincidences, preserves BCLK pulse widths during reset and retains short reset
requests. Two negative controls reject the original reset behavior and a lost
reset latch. Physical timing, electrical initialization and reset CDC remain
unqualified. The original seed-21 phase-reset fit fails core/DDR hold by
0.044 ns. A separate fit with an experimental additional 0.150 ns hold margin
also fails, with a worst reported slack of -0.282 ns. Detailed paths identify
HPS read-data to `boot_session` registers. Neither bitstream was deployed.
The unsuccessful additional-margin experiment is removed; the original
setup/hold requirements and full timing gate remain in force.

Quartus 17's generated functional netlist and an independent compiler probe
show that the ANSI `output reg lrclk = 1` declaration does not retain the
requested high startup value in this project's Verilog input mode. Internal
module-scope initialized state registers with output aliases correct this.
The isolated actual serializer compile passes explicit output-cone checks;
the original full-core netlist fails the same check. This is compiler startup
evidence, not a physical electrical measurement or accepted full-core fit.
The revised source's build in
`build/fpga-i2s-module-init-seed22-20261004` exits 0. All 140 checks are positive;
the worst core/DDR hold margin is 0.031 ns. All five source-bound fitted audits
pass, including 116 custom synchronizer chains at four corners. Full-core
functional-netlist output-cone checks retain SCLK/LRCLK/data at 0/1/0. The
candidate is frozen in `build/i2s-module-init-candidate-seed22-20261004` with
RBF SHA-256 `deb799992732d289cec23b537303af8c48cc880e88af63ea23ae960fa923b118`.
The exact pinned enable divider is included in a seven-assertion unbounded
proof with unrestricted mclk enable, reset and stereo words. The original
serializer yields a counterexample under that same generated wrapper.
Physical receiver timing, reset CDC and fresh human HDMI/audio confirmation
remain separate from these compiler/digital checks. Fresh private Studio and
player music soaks complete in 600.024/600.062 seconds with 3,044/3,047 samples,
zero underruns, valid saves, approximately 48 kHz audio/60 Hz completion and
flat sampled RSS/descriptors. Each has 21 memory checkpoints; maximum wall gaps
are 30.681/31.165 seconds. Both original monitor jobs and the coordinator exit 0,
and canonical payloads/settings, Tetris, HDMI and audio are verified restored.
The fresh human picture/stereo question remains pending; earlier answers are
not assigned to this RBF.

The same frozen I2S candidate also passes a new ten-second injected native
RUN-save read. Ten coherent PCM observations remain silent and seven complete
frame observations retain the same CRC during the guarded read window. The
cancelled save remains unchanged, and an explicit retry reaches BOOT 10 with
zero audio underruns. Worker disappearance is bracketed at 233.461–248.531 ms;
the trace does not measure an exact cancellation timestamp. Original coordinator
session 18380 and its bounded observer exit 0. Canonical Main/handler/payloads,
settings, Tetris, HDMI and audio are verified restored at that suite's end.
Evidence is sealed in
`build/i2s-module-init-native-read-451e93df/archive-manifest.json`. That historical
restoration does not describe the board during a later private test.

The fresh I2S lifecycle coordinator, original session 68306, exits 0 after four
cold entries, twelve native/modern-PNG/legacy-PNG selections and sixty-four raw
FPGA reloads. Actual saved BOOT counts are exactly 1–76. Each cold entry has a
different supervisor; all selections/reloads within that cycle retain it.
All 12,400 settled audio samples and 4,601 coherent transition samples have
zero underruns. Sampled parent/worker RSS stays within the 128/256 KiB steady
limits, and all four clean exits report zero recoveries and waiting frames.
The original eleven bounded transition jobs are collected with status 0.
Canonical Main/handler/payloads/settings and Tetris/HDMI/audio are verified
restored. Evidence is sealed in
`build/i2s-module-init-lifecycle-86bc20e3/archive-manifest.json`. This finite
suite does not close the broader transport, physical timing or long-session
gates.

Native Studio fault recovery on the same I2S candidate passes sixteen
checkpoints: corrupt/oversized rejection, missing files, spaced and superseded
MGL transfers, the exact 4 MiB extracted boundary, first/late tick hangs and
an abrupt worker kill. The last completed save is preserved through failures;
three synthetic keyboard RUN retries traverse actual Linux/Main input. The
supervisor is retained and three worker recoveries finish cleanly. All 896
settled and 1,243 coherent transition audio observations have zero underruns.
Original coordinator session 52783 and all six original bounded jobs exit 0,
and canonical Main/handler/payloads/settings and Tetris/HDMI/audio are restored.
The inherited second-file delay is corrected to three seconds before execution;
two preparer failures occurred locally before any driver was written or hardware
accessed. Those preparers remain in the archive. Evidence is sealed in
`build/i2s-module-init-studio-faults-f794316c/archive-manifest.json`. This checks
worker death, not arbitrary parent/Main death or physical SD power loss.

The new `fpga/sys/audio_out.v` override changes only the shared reset wiring
and adds a three-stage release chain. Six downstream blocks use its final
stage; seven original reset reference sites are replaced. A guarded comparison
with the pinned platform source verifies unrelated logic is unchanged. The
digital reset proof uses an explicit async2sync abstraction; separate simulation
checks pulses between edges, repeated requests and stopped clocks. All 450
cases (578 pulses) pass, and early release, missed asynchronous assertion and
raw-reset bypass controls are rejected. The revised full pipeline checks
636,544 channel words across 4,096 cases with zero reset LRCLK/BCLK coincidences.
The original pipeline regression still checks 636,672 words. The source-bound
serializer enable proof also passes. Three registered CTest checks pass and
sixteen build-record regressions pass, including stale/missing reset audits,
raw bypass, unreviewed logic and duplicate corners. Evidence is sealed in
`build/platform-audio-reset-source-review-20261004/archive-manifest.json`.
Original full-fit session 36325 terminates with coordinator exit 1 after a
successful compile: PowerShell's native stderr handling obscures a Tcl audit
error. The first diagnostic identifies a global-variable collision; the second
identifies an invalid register-name lookup. Both failed diagnostics are retained.
Using procedure-local variables and actual fitted register IDs resolves these
audit errors. Fresh UTF-8 audits on a copy of the fitted project pass all six
checks, including 398 reset consumers at each corner with complete paired
timing checks and no raw request bypass. The original 446 project files remain
byte-identical. No compile is restarted. The build recorder binds all four
consumer tables and rejects incomplete, stale, unpaired or nonpositive evidence.
Nineteen build-record tests and five Windows native-runner tests pass; the new
runner's CTest registration also passes. Fresh netlist output checks pass for
RBF `fd673c65`. No hardware is accessed for this recovery; physical reset width,
metastability, electrical startup, fresh fitted output timing and hardware
qualification remain separate. The current receipt is
`build/platform-audio-reset-fit-progress-20261004.json`.

The same RBF passes 432 conditional HDMI checks, with minimum setup/hold
slacks of 0.198/1.221 ns. Its source-bound I2S interval analysis gives
114.923/35.785 ns under the fixed-clock, rising-capture and zero-extra-skew
assumptions. These are fitted models, not physical measurements. Candidate
`platform-audio-reset-candidate-seed22-20261004` freezes RBF `fd673c65` with
unchanged Main and ARM frontends. Its manifest SHA-256 is
`6dfc8ee22fea7687b755d90e6df8ac385d018e56d99ba313732eb90030eb6c0a`.
A fresh read-only board check finds TIC-80 selected with canonical installed
hashes. The private Studio/player music coordinator is started once as original
session 85132, run `platform-audio-reset-sustain-baa1409d`. It finishes with a
failed player memory-coverage check. Studio passes its ten-minute suite with
3,046 samples, zero underruns and 20 memory checkpoints; sampled parent/worker
RSS is flat. Player playback also completes with zero underruns, but an SSH
reconnection leaves a 51.220-second monitor/51.168-second wall gap between memory
checks, exceeding the unchanged 40-second limit. That phase is unqualified.
Both original remote monitors exit 0, and canonical Main/handler/payloads,
settings and Tetris are verified restored. The PowerShell launch wrapper
reports exit 0 despite Python's assertion: its final environment cleanup masks
the native status. The Python failure is retained; wrapper exit 0 is not used
as proof of coordinator success. Future launches must explicitly return the
native status. Remote checkpoint collection is a follow-up, not an executed
fix or passing replacement run. The output/freeze receipt is
`build/platform-audio-reset-output-progress-20261004.json`; subsequent live
state must be read from the original process and that run's journal. The
completed failure is preserved in
`build/platform-audio-reset-sustain-failure-progress-20261004.json`.

The exact candidate subsequently passes two private Studio parent-exit cases:
TERM and KILL. The observed old worker groups depart, the unchanged installed
daemon relaunches Studio automatically, saved data stays CRC-valid and an
explicit cartridge retry works using the relaunched parent. Canonical
Main/handler/payloads/settings, Tetris, HDMI and audio are restored afterward.
This tests Studio parent death; Main/player death and physical power loss are
separate. Evidence is sealed in
`build/platform-audio-reset-parent-exit-progress-20261004.json`.

A read-only remote process/save sampler now passes six Linux integration
checks and its registered CTest. Its static ARM binary is bound to its source
and has no program interpreter. The new player-only coordinator, original
session 41400, uses detached remote checkpoints and deliberately closes only
its observer SSH transport for 35 seconds. It completes with native status 0;
both original monitor jobs are collected with status 0. The player phase
passes 600.062 seconds and 3,047 samples with zero underruns, CRC-valid saves,
flat sampled parent/worker RSS and descriptors. All 21 remote checkpoints
cover the run with a maximum capture gap of 30.106 seconds. The earlier failed
player run is unchanged, and the 40-second limit remains. Canonical binaries,
settings, Tetris, HDMI and audio are restored at the completed suite's end;
this does not describe a subsequent private test's live state. The source/tool
receipt is `build/remote-memory-checkpoint-source-progress-20261004.json`;
completed evidence is sealed separately in
`build/platform-audio-reset-player-remote-progress-20261004.json`.

This layout also passes a fresh 432-path conditional HDMI analysis, with HDMI
setup/hold minima of 0.198/1.218 ns under nominal receiver limits and zero board
skew. Combining the exact-enable digital I2S proof with this fit's four-corner
pin-delay intervals gives setup/hold minima of 115.207/34.786 ns for data/LRCLK,
assuming the fixed fitted clock, rising capture, zero PCB skew and no additional
relative edge uncertainty. A read-only observation during the private run finds
receiver register 0x0B=0x0E, confirming rising capture. These are conditional
interval checks, not physical receiver timing or reset-CDC qualification.

The preceding conditional HDMI receipt is
`build/hdmi-conditional-output-timing-progress-20261004.json`, linked through
the parent chain of `build/studio-progress-latest.json`. Its copied-project analysis covers 432 HDMI
setup/hold checks with explicit forwarded-clock inversion and separate clock
branches. Under nominal receiver limits and zero board skew, all checks are
positive; the HDMI branch's minimum setup/hold margins are 0.199/1.220 ns.
These conditional margins do not qualify actual PCB skew, receiver clock delay,
every dynamic video mode or audio/other external endpoints. The original
unverified-filter extraction and launch/preparation errors remain preserved;
the corrected run completes successfully. All 444 original project files remain
byte-identical, and no hardware or production constraints are changed.
See `hdmi-external-timing-review.md` for assumptions and scope.

Its parent is
`build/second-stage-divider-player-checkpoints-progress-20261004.json`.
The fresh player run completes
600.062 seconds and 3,047 samples with zero underruns, valid saves, approximately
48 kHz audio and 60 Hz completion. Its 21 memory checkpoints have maximum monitor
and wall-clock gaps of 30.338 and 30.483 seconds. Sampled parent/worker RSS and
descriptor counts remain flat. The original monitor starts once and exits 0;
the driver exits 0 and verifies canonical Main/handler/payloads/settings, Tetris,
HDMI and audio restored. Evidence is sealed in
`build/second-stage-divider-player-checkpoints-5a9cb224/archive-manifest.json`.

The shared observer now retries failed read-only reconnections within a bounded
budget. Launches and core switches retain single dispatch. Regression checks
cover repeated connection failures, original journal offsets, partial appends,
terminal errors and lost launch replies. A separate live observer deliberately
closes only its own SSH transport, reconnects and reads the same advancing
monitor/session without remote mutations. Its original ancillary verifier hits
a partial final JSON row; that failure is preserved and a completed-row verifier
passes. This does not claim a network outage occurred during the fresh soak.
Linux shell inspection also verifies that a departed `/proc` entry returns no
argv and cannot be mistaken for a frontend parent.

The parent external-I/O inventory and its parent record fresh sustained traces
for the latest RBF: 3,043 Studio and 3,047 player samples over approximately
600 seconds each, with zero underruns, valid saves, approximately 48 kHz audio
and 60 Hz completion. Both original monitors start once and complete with
status 0. Studio has 20 memory observations; player has only six because an
SSH outage leaves a 470.632-second gap. Sampled RSS remains flat, but uninterrupted
player checkpoint coverage is not established by that interrupted run. The
separate fresh player run above closes the bounded checkpoint gap without
rewriting the earlier trace.

The original observer fails to reconnect and cannot verify rollback. A new
coordinator collects the same original monitor without reactivation or cartridge
reload. Its final restoration check races a departing `/proc` entry. A separate
read-only verification then proves canonical binaries, launcher, settings,
Tetris, HDMI and audio restored without another core load. Original failures
remain in the sealed evidence; neither is rewritten as a successful driver.
The receipt is `build/second-stage-divider-sustained-recovery-progress-20261004.json`;
evidence is sealed in `build/second-stage-divider-sustain-b9534bd9/archive-manifest.json`.
The shared SSH helper now raises a connection error before dispatch when no
transport exists, and its delayed-output, timeout, disconnect and no-duplicate-
start regression checks pass.

The native RUN-read parent receipt is
`build/second-stage-divider-native-corrected-progress-20261004.json`.
The new frozen RBF is
`d08bb729516adeb26233460e2fde55cea5d1cbd8b76bd6c788a810f295aedb3d`.
Its native RUN-read test captures nine silent PCM snapshots and seven stable
complete-frame snapshots during the original guarded interval, preserves the
cancelled save and retries to BOOT 10. All 167 completed audio observations have
zero underruns. Canonical Main/launcher/payloads/settings, Tetris, HDMI and audio
are verified restored. Evidence is sealed in
`build/second-stage-divider-fast-observer-native-bd57eebb/archive-manifest.json`.
Worker disappearance is bracketed at 242.106–258.011 ms, without proving an
exact cancellation timestamp. A separate corrected receipt fixes inherited
explanatory prose while preserving the original sealed trace and correct
numerical bracket.

Its parent chain preserves the first-stage-only RBF's sustained receipt,
`build/parallel-divider-sustained-playback-progress-20261004.json`.
On that earlier RBF, Studio/player independently observe
600.108/600.064 seconds, with 3,045/3,047 samples and zero underruns.
Both retain valid saves, a single worker, flat sampled parent/worker RSS after
60 seconds, approximately 60 Hz completion and 48 kHz audio. Fitted queue drift
is 0.029/0.329 ms. Both original monitor jobs exit successfully, and canonical
Main/launcher/payloads/settings, Tetris, HDMI and audio are verified restored.
Evidence is sealed in `build/parallel-divider-sustain-a0b79136/archive-manifest.json`.
The separate human question is bound to this actual RBF and Studio run; it has
no recorded reply yet. Its parent preserves the exhaustive divider
comparison, positive fit, source-bound frozen candidate and failed first private
read-timeout observation. That run's 147 ms observer gap and original error
remain preserved. Two subsequent observer-rate experiments also fail their
unchanged calibration guards and remain archived.

The final observer keeps the same complete-frame and PCM checks, sleeps 5 ms
instead of 10 ms, and writes its trace to RAM. Its canonical-Tetris calibration
measures a maximum gap of 20.4 ms. A new private candidate run passes the
original pending-read requirements with ten silent PCM and four unchanged
complete-frame snapshots, preserves the cancelled save, retries explicitly to
BOOT 10, and restores canonical Main/handler/payloads/settings, Tetris, HDMI
and audio. Worker disappearance is bracketed between 244.75 and 263.69 ms;
the trace cannot establish an exact cancellation time or a sub-250-ms reap.
Fresh sustained testing passes on that first-stage-only RBF; both sustained
and human picture/audio testing are recorded separately for the latest
second-stage divider RBF. Its continuous playback traces and fresh player memory
checkpoint coverage now pass; physical confirmation remains incomplete.
Parent receipts preserve the earlier rejected fits, cancelled builds, video
simulation and DDR timing investigation.
Read-only HDMI receiver observations are described in
`hdmi-external-timing-review.md`; external timing remains unqualified.
The running-read receipt verifies the original native
coordinator, fixture/source/ABI hashes, private save contents, actual Main
keyboard delivery, worker liveness, PCM/frame observations, and restoration of
the confirmed installation. Earlier receipts and incomplete runs remain
separately preserved.

The earlier shared-reset bitstream's RUN-read test observes the injected worker
gone at 233 ms, preserves the unacknowledged save, and resumes PCM after an
explicit retry. That observation does not establish the cancellation time for
the newer parallel-divider bitstream. Its separate liveness interval is given
above. All six broader release gates remain partial or open, so the candidate
is not accepted as a finished release.
