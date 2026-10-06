# Standard MiSTer compatibility

Standard MiSTer Main is the default release target. Users should install the
TIC-80 FPGA core, ARM runtime and launcher without replacing `/media/fat/MiSTer`.
`Scripts/Install_TIC80.sh` now implements that installation contract using
Frontier's normal handler discovery. The separate development installer ZIP
omits Main, passes thirteen local installer/package checks and a read-only
bundle check on the MiSTer. It is not a claim of completed standard-Main
runtime qualification; see [installation](install.md).
The separate, frozen October 5 prototype package includes a patched Main;
its hardware qualification does not establish the standard-Main release target.
The public Frontier preview omits Main and labels that qualification as pending.

## Why the prototype patches Main

`tools/stage_main_source.py` changes three Main translation units:

| Extension | Purpose | Standard-Main approach |
| --- | --- | --- |
| Source-path packet before a cartridge transfer | Associate an OSD-loaded cartridge with its original file for Studio Save | Use Studio's own file browser for a known source path. Standard OSD transfers already load without a path; preserve the existing safe working-copy/Save As behavior when the original cannot be verified. Any automatic lookup must compare complete bytes and reject ambiguous matches. |
| Temporary reset hold for the first delayed MGL file, plus popup handling | Prevent a retained cartridge from running before the intended initial file arrives | Resolve startup and cached-cartridge ownership in the TIC-80 runtime/launcher and core. Qualify delayed, missing and rejected first files, direct entry, reset and reload before replacing the prototype path. |
| Horizontal mouse-wheel evdev events sent through a TIC-80 UIO command | Deliver an input axis absent from the common PS/2 mouse path | Handle this axis in a TIC-80-only Linux input helper, with device hotplug, coarse/high-resolution normalization, OSD suppression, release and cleanup checks. Retain Main's ordinary input path for other axes and controls. |

These extensions do not execute TIC-80 games or generate its video/audio.
Normal cartridge bytes use MiSTer's existing file-download interface. The
current FPGA cart loader publishes an empty source path when no extension was
sent; the ARM backend accepts that path and Studio has a working-copy fallback.
The requested controller/key mappings are outside these Main patches.

The reference PICO-8 port's documented installation contains the FPGA core,
ARM executable, handler and Frontier setup, without a replacement Main:
<https://github.com/MiSTerOrganize/MiSTer_PICO-8#manual-install>.

## Evidence and remaining work

`tools/test_hps_transport.py --scenario stock` extracts the transfer/status
helpers from unmodified pinned Main, rather than the staged patch. It sends
standard native and PNG-index transfers through the real SPI handshake,
pinned HPS download parser and current TIC-80 cart loader, with stalled DDR
and delayed cartridge acknowledgements. It does not invoke the source packet,
MGL overlay or horizontal-wheel extension. This is a digital transport check;
it is not full Main execution or hardware qualification.

The original October 5 run exits 0 under coordinator 17820. Eight transfers
pass exact byte comparison, delayed-ACK staging ownership and empty source
metadata. Independent inspection confirms the exercised download/status helpers
are identical to unmodified pinned Main. Evidence is
`build/stock-main-transport-progress-20261005.json`; the original log and
generated transport sources are retained. No MiSTer hardware is accessed.

Before qualifying the standard-Main preview as a finished release, verify both frontends on an
unmodified supported Main, including actual cartridge loading/switching,
Studio source/save behavior, startup/MGL/reset/reload, all requested controls,
audio and HDMI. The published manifest already omits Main, and its installer
preserves the shared executable. Keep any optional experimental Main extension
separate from that package. Do not strip Main from the existing frozen ZIP and
reuse its qualification as proof of a different installation.

Two private-path native smoke attempts (original coordinators 33762 and 51981)
failed their expected-executable guard and restored the installed prototype,
Tetris playback, audio and HDMI. The second attempt's `/proc` observations show
that Main restarted the configured `/media/fat/MiSTer` instead of staying on the
private stock binary. Unmodified `user_io.cpp` compares `cfg.main` with its own
executable and calls `app_restart` when paths differ. Consequently these attempts
do not prove stock-Main runtime behavior or a core incompatibility. Neither
changed the shared Main file. Their original results remain under
`build/stock-main-native-smoke-20261005` and
`build/stock-main-native-smoke-v2-20261005`.

A subsequent native check ran official `MiSTer_20260912` in a private mount
namespace, bound to Main's configured path only inside that namespace. The
shared executable's bytes and the outside namespace stayed unchanged. The
running Main executable matched SHA-256
`9f6e5a237c36be6404ab4823d804821491db4bf125827f84aca2a1ca31f0a8a6`.
Studio loaded Tetris through its MGL, acknowledged all 25,147 cartridge bytes
and correctly reported an empty source path. A ten-second observation recorded
272 audio samples, zero underruns and 481,294 played frames. Four HDMI register
observations measured the expected 74.25 MHz clock for 720p/60.

That coordinator (65843) exited 1 during restoration: its single-Main guard
caught a transient restart after returning to MENU. Preserve that failure;
it was not an end-to-end passing run. A separate restoration coordinator
(30000) waited for Main to settle, restored the installed prototype and Tetris,
and exited 0 after checking the original source acknowledgement, outside mount
namespace, payload hashes, audio and HDMI clock. The unsaved Studio cartridge
was backed up with an exact full-cartridge round trip before the core switch.
Evidence is retained in
`build/stock-main-namespace-native-smoke-v2-20261005` and
`build/stock-main-namespace-restoration-v2-20261005`.

This establishes a narrow native stock-Main Studio loading/playback check.
It does not qualify the player, startup/reset corner cases, source-save
workflow or the proposed per-core horizontal-wheel path.

The development source adds a bounded per-core evdev reader and an FPGA OSD
edge epoch. Host tests cover independent coarse/high-resolution devices,
companion suppression, lost events, disconnect, node replacement, menu-time
motion, large queued bursts and descriptor cleanup. Real Studio and player
cartridge APIs agree on direction, wrap, burst limits and OSD suppression.
Input, DDR and combined-transport RTL checks pass. The seed-22 FPGA build
completed its 140 internal timing checks and required post-fit audits with
minimum reported slack of 0.040 ns. Its RBF SHA-256 is
`5595ba3246a1d8af1cc6926a518da1907d287c5e2b96a8de320fb6cefb5feb6d`.
This is a separate development candidate, not the published release, and has
not been installed or qualified on a live HDMI display.

Inspection of unmodified Main identifies a remaining access constraint:
`input.cpp` initializes `grabbed = 1` and uses `EVIOCGRAB` when discovering
devices. A second non-grabbing evdev client cannot receive events while Main
owns that grab. The local simulated kernel queues do not cover this condition
and do not establish stock-Main horizontal scrolling. A HID report path or
another supported access mechanism must resolve it without changing Main or
disabling the ordinary controller, keyboard and pointer paths. The board kernel
has HIDRAW and UHID enabled; actual device-report coverage remains unverified.
See the kernel's [hidraw interface documentation](https://www.kernel.org/doc/html/latest/hid/hidraw.html).

An isolated HID descriptor/report decoder is now available in `src/hid_pan.c`.
It reads relative Consumer AC Pan fields, signed and unaligned reports,
input report IDs, GET feature values and logical-collection resolution
multipliers. Fractional detents are kept per field; truncated input reports
discard them without publishing partial motion. It never changes device
features. Frozen-source tests pass under host ASan/UBSan and SDK ARM/QEMU,
including composite mouse/keyboard descriptors, multiplier priority,
multi-value feature fields and 20,000 deterministic descriptor mutations.
Evidence is `build/hid-pan-local-v2-20261005/result.json`.
The separate Release-mode CMake/CTest run also passes both the HID decoder and
evdev reader tests (`build/wheel-host-cmake-20261005/result.json`).

The development frontends now dispatch `--hid-wheel-worker` before normal
initialization. Their backend uses that private process for HIDraw discovery
and report decoding instead of the direct evdev reader. Descriptor and GET
feature queries run in separate per-device children with a 1.5-second deadline.
A stalled request does not stop other devices or the reader heartbeat. Temporary
query errors, timeouts and probe socket/fork failures retry on the same device
node after 1, 2, 4, 8, 16 and then 30 seconds. Unsupported or malformed
descriptors stay cached until node replacement. A recovered device drains its
old motion before accepting new reports. Killed probe
slots remain reserved until their specific child is reaped, bounding pending
helpers even when a kernel request has not returned. Probe children close other
inherited descriptors and receive SIGKILL when their reader parent dies.
The frontend exchanges nonblocking sequenced messages with the worker; a
generation change discards old responses and drains raw queues at OSD edges.
Cumulative counters preserve queued totals during socket backpressure. Worker
crashes or a three-second heartbeat timeout trigger private-helper retirement
and a delayed retry without waiting in the audio publication loop. Specific
children are reaped; unrelated processes are never killed or waited on.

Local tests exercise real spawn/exec, socket messaging and directory/file
lifecycle with substituted HID ioctls and raw kernel queues. They cover signed
and fractional motion, node replacement, truncation, stale OSD responses,
bursts, worker death, stuck descriptor/feature probes and cleanup. Optimized
host ASan/UBSan and SDK ARM/QEMU checks pass, including healthy-device motion,
new discovery and node replacement with stalled devices still attached.
The reader heartbeat continues past the frontend watchdog interval, and the
timed-out probe children are reaped. Separate reader suspension still exercises
the frontend watchdog. Current-source recovery evidence is in
`build/hid-retry-after-20261006/result.json`. It additionally covers temporary
feature/descriptor failures and exhausted probe sockets without reconnecting,
discarding their queued motion and retaining unsupported-descriptor caching.
The original regression failed before the fix and is preserved separately in
`build/hid-retry-before-20261006`. Earlier probe-isolation evidence is in
`build/hid-probes-current-v9-20261005/result.json`; the preceding CMake run is
`build/hid-wheel-probes-v8-20261005/result.json`. Those simulations
do not prove that native device reports remain available while stock Main
owns evdev. The revised worker additionally handles unnumbered feature GET
responses with or without a zero report-ID prefix: Linux USB and Bluetooth
HIDP return different layouts. This follows their pinned
[USB HID](https://github.com/torvalds/linux/blob/v6.18/drivers/hid/usbhid/hid-core.c)
and [Bluetooth HIDP](https://github.com/torvalds/linux/blob/v6.18/net/bluetooth/hidp/core.c)
implementations; it still needs native device qualification.

Native hotplug, Bluetooth reconnect, real kernel queue saturation, feature
query failures and latency during audio publication remain open. The first
worker required removing a stalled node before recovery. The per-device probe
isolation and current regression replace that limitation; actual kernel/device
behavior still requires native qualification.

The first fresh SDK compile exited 2 because its build-time Forth bootstrap
was dynamically linked and QEMU could not find the target loader. Its original
failed coordinator/result remain in `build/linux-wheel-sdk-v2-20261005`.
The helper now links statically; a separate target dictionary generation passes
and its ELF has no interpreter segment. The corrected full SDK build in
`build/linux-wheel-sdk-v3-20261005` compiles both frontends and all language
libraries, but its optimized evdev fixture fails; that original failure is
preserved. Current fixture corrections pass separately under host and ARM/QEMU.
Both actual frontends are subsequently rebuilt with the current HID integration
in `build/hid-frontends-v2-20261006`. This component build reuses unchanged,
hash-bound language libraries; it verifies the target backend/input ABI before
linking, passes backend/input/frontend tests and checks both helper entry points.
It does not represent a fresh rebuild of the unchanged language libraries.
None of these local checks establishes full stock-Main qualification.

The rebuilt ARM production binaries also pass modeled player held-transfer and
delayed-initialization workflows, plus Studio anonymous-selection acceptance
and cancellation. The original fifth case, Studio queued reload, fails its
disk-state comparison in `build/hid-frontends-workflows-v1-20261006`; that
overall failure remains preserved. Investigation observes an asynchronous pause
flush of previously completed ticks, rather than new cartridge execution.
Unbuffered cartridge tracing shows no TIC calls during the hold. An intentionally
early initialization edge produces six TIC calls and is detected by that oracle
(`build/studio-reload-trace-diagnostic-v3-20261006`).

The corrected reload fixture observes actual TIC traces across ordered hold
markers and checks final persistent values against the exact acknowledged tick
and BOOT counts. It retains CRC, worker cleanup, frozen DDR and modeled audio
checks. All nine reload scenarios and the early-edge negative control pass with
the unchanged production Studio binary under ARM/QEMU
(`build/studio-reload-oracle-v5-20261006/result.json`). This run extracts the
actual CMake reload-test registration into a minimal project and imports the
hash-verified production binary; it does not rebuild the language libraries.
The stdout-unbuffering library is test-only and is not installed or packaged.
These models do not execute stock Main, qualify initial delayed MGL selection
on hardware, or close the pending native runtime gates.

The user confirmed the published installer works after the clean-install
preparation on October 5. Preparation restored official stock Main, removed
and backed up the TIC-80 cores and per-core mappings, and preserved the games
tree and saves. The staged installer and bundle matched the published release.
A separate read-only SSH check verifies all five installed payloads against
that release and confirms the shared stock Main hash is unchanged. The selected
core remains PICO-8 throughout the check. Evidence is in
`build/user-install-readonly-20261005/result.json`. Installation success does
not close the remaining runtime gates. The newer HID worker remains a local
development candidate and has not been deployed by these checks.

## Native reader test preparation

`tools/hid_pan_probe.c` creates four private UHID mice only while TIC-80 is
selected. It checks that the named Main process has each evdev handle and that
its own grab request returns busy. It also enumerates process handles to reject
another possible owner. Coarse, numbered high-resolution and unnumbered
high-resolution reports then pass through the real kernel and the production
private HID reader. A fourth device withholds its feature reply while healthy
motion continues. The test never changes a physical mouse's feature setting.
Its direct active/epoch calls exercise the reader's gate behavior; they do not
establish end-to-end FPGA OSD or frontend `mouse()` behavior.

The test uses the public
[UHID API](https://www.kernel.org/doc/html/latest/hid/uhid.html) and checks the
descriptor/report layouts locally before device creation. Closing its UHID
handles removes its virtual devices. Core changes stop injection; its temporary
character-node aliases are confined to its own newly created `/tmp` directory.

`tools/test_hid_pan_native.py` runs the reader-only test with the published
TIC-80 payloads and stock Main. It records the original job once, collects its
status journal, and restores Main's original RBF/MGL argument. It verifies
installation/configuration hashes and cartridge-tree metadata. Local offline
regressions cover successful restoration, a lost launch reply without relaunch,
refusal of an unexpected Main and protection of a user-selected different core.
This driver requires a free, user-authorized test window before switching an
occupied core. No candidate binaries are installed in this first test.

The earlier prepared probe passes optimized host ASan/UBSan and ARM/QEMU
descriptor/refusal checks (`build/native-hid-probe-v4-20261006/result.json`).
The preceding CMake run passes the parser, private worker, probe descriptor and
evdev fixture tests on both architectures. Its snapshot predates the added Main
identity/other-owner checks; those changes are compiled in the current probe.
The Python driver's five offline rollback/ownership cases also pass through
its registered CTest. The current host sanitizer CMake run passes all five
selected decoder, worker, probe descriptor, evdev and driver tests
(`build/hid-retry-cmake-20261006/result.json`). These checks prepared the first native execution.

`tools/prepare_hid_native_candidate.py` separately freezes the new RBF and both
production frontends, the published handler/CA file, the current native probe
and an exact copy of the published five-file rollback. Its `--frontend-build`
argument selects a checked rebuild; source and executable hashes must match
that build's passing receipt. The private manifest
`build/hid-native-candidate-v3-20261006/manifest.json` excludes a Main payload
and marks native qualification false. It predates the subsequent device-I/O
isolation repair and needs a fresh frontend rebuild before qualification.

The user-authorized October 6 reader test created all four private mice and
verified stock Main's exclusive evdev ownership for each. Its first coarse pan
assertion failed (`build/hid-reader-native-v1-20261006`). A separate diagnostic
run also failed, showing that the worker opened three devices and then stopped
producing heartbeats (`build/hid-reader-native-trace-v2-20261006`). Both original
failures are retained. The latter run successfully restored PICO-8.

The running kernel is `6.18.38-MiSTer`. Its HIDraw behavior is consistent with
the upstream [6.18 shared semaphore](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/hid/hidraw.c):
feature ioctls take a read lock, while opens and releases take a write lock.
Moving only feature queries into helpers therefore leaves the central reader
vulnerable to a blocked open or release. The revised per-device helpers own all
raw opens, queries, reads and releases. The supervisor owns only their sockets
and checks bounded deadlines without waiting for their exit. Unsupported
devices remain cached; temporary failures retain backoff and reserved child
slots. A readiness observation lets the native fixture wait for the three
healthy devices' current-gate acknowledgments before injecting its first tick.
Open acknowledgments now precede descriptor queries; feature GETs start after
the descriptor phase completes. These barriers leave existing streaming
helpers running. A shared-lock fixture models concurrent GET read locks and
raw open/release write locks, alongside the existing per-device fault cases.

The first isolated, tracing-enabled native run passed, but the subsequent
uninstrumented run failed with only two healthy devices ready
(`build/hid-reader-native-production-v4-20261006`). That startup failure remains
preserved. After adding the explicit phases, the uninstrumented probe passes
on the real MiSTer (`build/hid-reader-native-phases-v5-20261006`, original
coordinator 96299, terminal native status 0). All four virtual devices retain
verified stock Main evdev ownership. Coarse positive/negative pan, numbered
and unnumbered scale-eight accumulation, stalled feature GET isolation, and
direct active/epoch queue suppression pass, with the final total exactly four.
Its driver verifies PICO-8 restoration and unchanged protected hashes; a fresh
read-only review additionally verifies no helpers, virtual devices or aliases
and all 4,377 cartridge metadata rows excluding exFAT inode numbers
(`build/hid-reader-native-restoration-review-v7-20261006`). No candidate payload
or Main was installed. This passes the reader-only gate; candidate FPGA/OSD
and frontend `mouse()` qualification remain separate.

The first restoration attempt also encountered a brief overlap of two stock
Main processes. The driver now waits for a single verified stock process
before dispatching. Nine offline cases check the overlap, refusal of changed
Main, ambiguous launches, user core changes and cartridge metadata guards.
The actual SD filesystem is exFAT, whose [inode numbers are allocated in memory](https://raw.githubusercontent.com/torvalds/linux/v6.18/fs/exfat/inode.c).
The cartridge check retains paths, types, device numbers, sizes and timestamps,
but excludes inode numbers for FAT-family filesystems. The separate read-only
restoration review verifies PICO-8, unchanged protected hashes, no private
helpers/aliases, and all 4,377 cartridge metadata entries excluding exFAT inode
numbers (`build/hid-reader-native-restoration-review-v4-20261006`). This is a
metadata check, not a full cartridge-content hash comparison.

The final repair passes the optimized host ASan/UBSan and SDK ARM/QEMU worker
matrix, including the shared kernel-lock startup model
(`build/hid-kernel-phases-after-v7-20261006`). The registered CTest run passes
all five selected parser, worker, probe descriptor, evdev and native-driver
tests (`build/hid-phases-cmake-v4-20261006`). Its expanded recovery cases take
about 38 seconds; the worker test timeout is now 60 seconds. The preceding
30-second CTest timeout remains preserved in `build/hid-isolation-cmake-v2-20261006`.
Both production frontends are rebuilt and pass target ABI, backend/input and
worker-dispatch checks using the unchanged SDK runtime archives
(`build/hid-frontends-v4-20261006`). Candidate
`build/hid-native-candidate-v4-20261006/manifest.json` binds these new frontends,
the current uninstrumented probe, the prepared FPGA, and exact published
rollback files. It still marks full native qualification false: this reader
test uses the published FPGA and directly supplied gate values, rather than
the new FPGA's actual OSD epoch or the frontend's `mouse()` API.

The subsequent private frontend fixture (`tools/hid_frontend_probe.c` and
`tools/test_hid_frontends_native.py`) loads the candidate RBF and starts each
unchanged production frontend outside the installed game folder. Four UHID
mice and a UHID keyboard retain stock Main's exclusive evdev ownership. A Lua
cartridge calls the real `mouse()` API twice per tick and records its results
in CRC-protected persistent state; no private reader supplies these results.
Win and F12 are sent in separate reports to establish Main's modifier state.
The probe reads the real FPGA OSD-open bit and requires exactly one epoch
increment at both opening and closing. It never writes the OSD gate.

The player and Studio both complete the nine horizontal scrolling stages in
`build/hid-frontends-native-v5-20261006`: coarse directions, fine fractions,
complete fine notches, blocked feature-query isolation, suppression during
OSD, suppression of its backlog, and cleared fractions after OSD. Both finish
at API pan total -5 with BOOT=1 and no differing repeated `mouse()` calls.
Their HDMI transmitter measurements infer 74.25 MHz at 720p/60. These are
synthetic-device horizontal API/OSD checks, not qualification of every mouse
field, physical peripherals, audio quality or a sustained soak.

The v5 coordinator originally reports failure because its liveness check races
the completed Studio job's terminal journal. All four original jobs have exit
status zero. A separate read-only review validates both original probe logs,
their terminal statuses, restoration to the original PICO-8 RBF, resumed
Frontier, removed virtual inputs, protected file hashes, source caches and
all 4,377 cartridge metadata entries (`build/hid-frontends-native-review-v5-20261006`).
The coordinator result remains unchanged. The collector now rechecks that
same journal when a process exits during observation; offline controls prove
it neither relaunches a completed job nor accepts a genuinely absent journal.

Earlier original results remain available: v1 lacks the kernel's optional
process-children file; v2's launcher incorrectly excludes the player's CPU 0;
v3's combined Win/F12 report fails to open OSD. Those test-driver issues are
corrected. The v4 player reaches both OSD transitions but then stops cartridge
execution with "MiSTer initialization did not complete". That intermittent
transport/recovery failure remains unresolved despite v5 passing. Every run
restores PICO-8 and verifies the protected installation. No candidate has been
installed or published, and full candidate qualification remains false.

Host sanitizer and SDK ARM/QEMU probe checks, including invalid persistent
state rejection, are recorded in `build/hid-frontend-probe-v5-20261006`.
The two registered frontend-probe/oracle CTest checks pass in
`build/hid-frontend-cmake-v2-20261006`. Production sources and binaries are
unchanged by these development tools.

A separate player with compile-time reset tracing is prepared in
`build/hid-frontends-trace-v1-20261006` for investigating v4. Its target ABI,
backend/input and dispatch checks pass. Its subsequent private hardware run is
described below; it remains explicitly ineligible for release or qualification
of the default production binary.

## Live output recovery and current native checks

Eight cold player cycles using the separate reset-trace diagnostic pass the
real mouse/OSD sequence (`build/hid-frontends-native-trace-v6-20261006`). Its
read-only restoration review also passes. These cycles did not reproduce the
original v4 failure and do not qualify the default player binary.

A separate reproducible recovery defect is now fixed in both default
frontends. A foreign video acknowledgement or impossible audio read counter
can fail output while FPGA identity, session acknowledgement and Main's
generation still match. The former player treated that as a Main reload and
waited for an initialization event that never came; Studio exited. The new
paths flush only their own transport and retain the running interpreter. An
actual lost FPGA session still goes through the existing initialization/reset
guard. Wheel totals are re-baselined, including the cached input fallback
while the FPGA input snapshot is incoherent; the legacy FPGA counter is kept.

The original failing regressions are preserved in
`build/output-recovery-before-v1-20261006` and
`build/studio-output-recovery-before-v1-20261006`. The default rebuild is
`build/hid-frontends-v7-20261006`; player SHA-256 is
`23346d3c41bbdac7b3c199e9ec24bb4d3577c313f1063ca2c91edbf0f7834db6`,
Studio SHA-256 is
`baf2b1fc6ec1a1af5e35a88872da3e9f1cbcae21a2362545e58979cfa8b4cb01`.
Seventeen registered ARM/QEMU recovery, initialization, held-reset and reload
checks pass against those exact binaries, including BOOT=1 and unchanged
interpreter PID after the injected live-output faults
(`build/output-recovery-final-v1-20261006`). Sixteen further registered
live/OSD/dialog/selection/reset/PNG/worker-fault regressions pass
(`build/frontend-integration-current-v1-20261006`). None uses MiSTer hardware.

Four cold hardware cycles per default frontend also pass all nine real
horizontal mouse API/OSD stages on the prepared RBF
(`build/hid-frontends-native-v8-20261006`). The original sixteen probe/frontend
jobs exit zero. Studio records zero output flushes in these healthy runs.
Their independent read-only review verifies PICO-8 restoration, stock Main,
protected hashes, source caches, Frontier and the 4,377 cartridge metadata
entries (`build/hid-frontends-native-review-v8-20261006`). Frozen copies of
the original driver and oracle preserve the exact test sources. This remains
a synthetic horizontal-axis check, not fresh physical input or audible output
qualification.

The reproducible output-contract defect is not proof of the original v4
failure's cause. That original failure remains recorded and unreproduced.
Candidate `build/hid-native-candidate-v6-20261006` is privately staged;
installed payloads, the shared Main executable and the public release remain
unchanged. Its broader native music, lifecycle and source-save checks are
recorded separately when complete.

## Native music and reconfiguration checks

Both default v7 frontends complete separate ten-minute music runs with the
prepared RBF (`build/private-music-native-v1-20261006`). Each retains its
original interpreter and BOOT=1, records zero audio underruns, and passes the
resource plateau checks. Measured audio clocks are approximately 47,999.25 Hz
and presentation is approximately 59.9988 frames/s. Fitted queue changes are
-0.118 ms for the player and -0.141 ms for Studio over ten minutes. The
read-only review verifies original job statuses and restoration, protected
files, source caches, Frontier and cartridge metadata
(`build/private-music-native-review-v1-20261006`). These measurements qualify
those exact binaries' automated music checks; audible stereo and visible HDMI
quality still require a human observation of the tested build.

A later private test exposes a separate player reconfiguration failure:
the first raw RBF reload resumes neither the original player nor its cartridge
(`build/private-lifecycle-native-v1-20261006`). Main can reset the bridge for
longer than one transport handshake, and the newly configured FPGA first
captures a stale DDR session request. The player now retries its own transport
for at most ten seconds while TIC-80 remains explicitly selected. Another or
unknown core ends the wait, SIGTERM remains a clean exit, and a cartridge
already transferred during reconfiguration takes priority over the cached
cartridge. No ticks run while transport is unavailable. This change does not
establish the cause of the earlier intermittent v4 OSD failure.

The latest default player SHA-256 is
`930e7531e510c80aacc52fb6e8c3309e36d04b00d7746976da976a6507158eb8`
(`build/hid-frontends-v9-20261006`); Studio remains the v7 binary above.
Eleven registered ARM/QEMU checks pass against this player, covering lifecycle,
held reset, Main initialization, six reconfiguration scenarios and both live
output faults (`build/player-reconfiguration-after-v3-20261006`). The firmware
fixture preserves the RTL's sticky session-arm behavior, including after a
zero request; earlier failed fixture and regression receipts remain available.
This is local validation, not native reconfiguration qualification.

The native lifecycle driver exercises actual stock-Main RBF reloads and
native/modern/legacy PNG transfers, then Studio editing and Save into private
working copies. Its original v2 Studio and diagnostic v3 player runs resume
after their first raw reload but fail an incorrect test expectation that the
ARM-owned cartridge acknowledgement clears across FPGA reconfiguration.
The corrected oracle permits that previous acknowledgement when Main has not
transferred another cartridge. Those original failures remain recorded.

The default player's original v4 lifecycle run resumes through three raw
reloads, reaching BOOT=4. The third reload's monitor records 39 normal samples
before reporting "Matching hardware statistics unavailable". The player
remains alive and exits cleanly during restoration; this does not establish
why the monitor rejected identity or statistics magic. A read-only follow-up
retains the failed monitor's original output. The monitor now reports the exact
failing register values without retrying or accepting that snapshot, and the
driver saves monitor output before checking its exit status.

The original v5 attempt loses SSH during observation after its first raw
reload. A new read-only connection finds the original player running and
restarted once. Separate guarded restoration verifies PICO-8, stock Main,
protected files, source caches, Frontier and all 4,377 cartridge metadata
entries (`build/private-lifecycle-restoration-v6-20261006`). The original
coordinator failure is preserved. Subsequent read-only observations may
reconnect within a bounded budget; mutations and original jobs are never
redispatched after a lost reply.

Candidate `build/hid-native-candidate-v8-20261006` remains private and has not
passed full qualification. The installed payloads and public release are
unchanged. Current player native lifecycle, mouse/OSD and ten-minute music
checks must be recorded against its new binary before they count for it;
Studio's unchanged binary retains its existing mouse/OSD and music evidence.

The original v6 lifecycle run passes cold startup and all four raw player
reloads, retaining the original process with BOOT progressing from 1 through
5 and active audio after each. It then observes the retained cartridge before
Main's three-second MGL file delay has elapsed and fails the transfer oracle.
The driver now assigns each replacement cartridge a separate save identity
and waits for its own BOOT=1. PNG MGL actions use menu slot F0 (`index="0"`)
and a `.png` filename; stock Main derives the transfer extension ordinal.
These are test corrections, not changes to runtime or shared Main.
The v6 monitor did not reproduce the earlier statistics-marker failure.

The corrected native lifecycle run passes both exact default frontends
(`build/private-lifecycle-native-v7-20261006`). Each cold start and four raw
RBF reloads retains the original frontend, progresses BOOT from 1 to 5, and
resumes measured audio without underruns. Native, modern PNG and legacy PNG
MGL transfers each run the newly identified cartridge with BOOT=1 and the
expected advancing acknowledgement. Studio's anonymous working-copy Save
contains the requested code edit and leaves the original OSD cartridge
unchanged. Console loading a private file followed by another editor Save
updates that established file with the second edit. These synthetic keyboard
actions do not qualify physical keyboard input. All twenty original jobs exit
zero. A separate read-only review confirms restoration and all protected
installation state (`build/private-lifecycle-native-review-v7-20261006`).
Frozen source copies retain the original test driver and oracle. The earlier
intermittent statistics-marker rejection remains unreproduced, with its
original failure preserved; no installed or published payload is replaced.

Four fresh native mouse/OSD cycles pass against the latest default player
`930e7531` (`build/hid-frontends-native-v9-20261006`). Each completes all nine
horizontal API stages with BOOT=1 and identical repeated `mouse()` results.
The eight original frontend/probe jobs exit zero. The independent read-only
review verifies the frozen test sources and protected installation/restoration
state (`build/hid-frontends-native-review-v9-20261006`). These cycles do not
reproduce the original intermittent OSD failure.

The latest default player also completes a fresh ten-minute native music run
(`build/private-music-native-v2-20261006`): 3,047 coherent samples over
600.062 seconds, zero measured underruns, 47,999.246 Hz audio and
59.998791 frames/s. Fitted audio queue change is -0.0173 ms. The original
interpreter is retained, BOOT remains 1, sampled parent/worker RSS and
descriptor counts stay flat, and the largest recorded game-tick gap is 31 ms.
Both original jobs exit zero. The independent read-only review verifies
samples, resources, saves, frozen sources and protected restoration state
(`build/private-music-native-review-v2-20261006`). The optional human
picture/stereo confirmation for this exact player is still pending.

## Current production-worker matrix and startup audit

The exact current player `930e7531` and Studio `baf2b1fc` also pass the
native production-worker matrix (`build/native-runtime-matrix-board-v2-20261006`).
Fresh ARM helpers execute 84 distinct workers: both frontends, all fourteen
script runtimes and native/modern/legacy cartridge formats. Every one of the
168 complete RGBA reference frames and 5,040 PCM tick buffers matches the
independent frozen references. A separate read-only review checks original
terminal journals, all 84 distinct worker PIDs, payload hashes and retention
of the installed Studio and stock Main
(`build/native-runtime-matrix-board-review-v2-20261006`). The suite does not
map DDR or change core selection, installed files or physical input. It covers
the frozen demo programs, not every cartridge or API.

The original preparation loses SSH after its first helper unpack and remains
failed (`build/native-runtime-matrix-board-v1-20261006`). Read-only reconciliation
finds that unpack complete, with the exact hash and executable permissions.
It also observes Wi-Fi reassociation in the kernel log. The successful second
coordinator adopts that verified file without repeating the unpack, journals
all remaining preparation jobs and starts the matrix exactly once.

A requirement audit then finds a real startup defect, distinct from transport
recovery: the retained cartridge runs an extra BOOT before stock Main's delayed
MGL replacement arrives. The original lifecycle v7 pass verified that the new
cartridge eventually runs, but did not reject this intervening BOOT. Read-only
inspection of its CRC-valid saved state confirms both frontends' original
cartridge has BOOT=6 after the four raw reloads should leave it at 5. Their
intermediate native and modern PNG cartridges each have BOOT=2 instead of 1;
the final legacy cartridge remains at 1
(`build/mgl-retained-boot-audit-v1-20261006`). Thus that passing run does not
qualify the original no-premature-cached-execution requirement.

The lifecycle driver now compares the retained cartridge's BOOT counter before
and after each replacement, in addition to verifying the new cartridge's own
BOOT. Its offline negative controls reject increments, decrements and resets.
Both current default runtime binaries still exhibit the startup defect; a
per-core synchronization fix is required before release qualification. The
shared Main and installed preview remain unchanged.


## Per-core delayed-MGL startup correction (October 6)

The source now holds a retained cartridge after Main initializes a new FPGA
when Main's first valid MGL action loads F0/F64. The frontends locate the single
stock Main executable through `/proc`, read its RBF and optional XML launch
arguments (MGL is `argv[2]`), and use the vendored SXMLC parser with Main's
first-valid-action rules. Reads are bounded to 8 KiB of arguments and 64 KiB of
XML. A missing or ambiguous launch context stays held; elapsed time never
permits cached BOOT. A real transfer, including a rejected transfer, or a later
user reset releases the wait. Raw RBF reloads and first-reset MGLs keep their
one-restart behavior. Main and the FPGA protocol are unchanged.

The final SDK/default rebuild is `build/mgl-frontends-v3-20261006/result.json`:
player `b72812952e3bf472708e4c91c48eb90735a5e2767eaf4e9fd8b846f41a4d749b`,
Studio `b2e558e474e227b3fa03ae6504f75e23cef310ccf3ac12dc10445244f66d1947`.
Its ELF hashes exactly match the tested final binaries. The earlier complete
37-test ARM/QEMU regression passes; after the player waiting-message change,
all 16 affected registered cases pass on the final binaries, followed by both
new rejected-initial-transfer cases. These exercise real frontend/interpreter
IPC, exact BOOT counters, CRC-valid saves, delayed replacement, missing MGL,
explicit reset cancellation, departure and SIGTERM. The previous production
binaries fail the same counter oracle with BOOT=2 before replacement arrives.
Receipts are `build/mgl-startup-local-v2-20261006`,
`build/mgl-startup-local-v3-20261006`, `build/mgl-startup-local-v4-20261006`, and
`build/mgl-startup-negative-v1-20261006`. Original failed harness preparation
is preserved separately; it never ran the runtime tests.

A native read-only launch-context check also passes on the MiSTer while keeping
Main PID 18493 and installed Studio PIDs 18525/18552 alive, with unchanged
protected payloads. Its original journaled job returns raw-launch classification
0, matching the actual two-argument Main launch. Evidence is
`build/main-launch-native-context-v2-20261006/result.json`; the initial guard's
wrong handler pathname failed before any hardware mutation and is preserved.
This check does not qualify delayed-MGL switching on the FPGA. The stronger
native lifecycle gate, current physical controls, exact-candidate human output
confirmation and the broader validation gates remain open. The published
preview and installed frontends remain unchanged.


## Launch-context recovery and stronger native gate (October 6)

The first stronger native run passes the player cold start, four raw reloads
and all three delayed native/PNG replacements with retained BOOT counters
5 to 5, 1 to 1 and 1 to 1. Studio passes cold start and its first raw reload,
then stays paused after its next lookup returns `launch_context=-1`. That
original run fails and is preserved in `build/mgl-native-lifecycle-v1-20261006`,
with a partial-scope independent review; it is not a passing combined gate.
The exact initial lookup failure reason was not recorded. All original jobs
terminate and the installed Studio is restored without changing protected
payloads.

Both frontends now re-observe unavailable launch context every 100 ms while
keeping the retained cartridge paused. A coherent raw launch permits one
restart; a coherent first-cartridge MGL keeps waiting for its actual transfer.
Elapsed time never permits cached BOOT. Persistent unknown context still
supports explicit reset, a new transfer, SIGTERM and core departure. Future
native failures capture matching Main processes, their arguments and the
original frontend log before cleanup changes the selected core.

The exact SDK/default binaries are player
`2a03b8491dcaa3d85e87e18c60756caefe94e97f16179c6ecf36870135e994c2` and Studio
`e36136383e5cadc441cc8085ac638fbf70948500a8633e266ac267a5fa0ec64b`, built in
`build/mgl-frontends-v4-20261006`. All 43 registered ARM/QEMU frontend/reload/
output tests pass. Both prior binaries reproduce the unresolved-context
regression; the new cases verify unknown-to-raw and unknown-to-MGL recovery.
Receipts are `build/mgl-context-local-v1-20261006` and
`build/mgl-context-negative-v1-20261006`.

The new full native run passes for both exact binaries: cold start, four raw
RBF reloads each, three delayed native/modern-PNG/legacy-PNG replacements each,
and active audio after all 16 cartridge actions. Every monitor has zero
underruns. The retained BOOT counters remain 5 to 5, 1 to 1 and 1 to 1 in both
frontends; new cartridges each BOOT once. Studio also passes private working-
copy Save and console-load/editor Save. Its logs directly show unknown launch
context resolving to raw launch during reload and to MGL during replacement,
without resuming the old cartridge prematurely. All 20 original jobs terminate
with exit 0. Evidence and independent review are
`build/mgl-native-lifecycle-v2-20261006` and
`build/mgl-native-lifecycle-review-v2-20261006`.

Before switching, a read-only ABI-bound inspector verifies the installed
Studio has no unsaved changes or pending selection. The owner journal checks
that again immediately before the normal MENU transition. Installed files,
Main, original cartridge metadata, source caches and Frontier are preserved;
the owner restores the installed TIC-80 Studio afterward. Evidence is
`build/mgl-native-lifecycle-owner-v3-20261006`. The first owner attempt's
empty-core-name guard failure occurs before candidate launch and is preserved
separately. This passing gate does not replace the remaining exact-binary
music/resource, physical-input, human HDMI/stereo or broader release gates.


### October 6: current-frontends sustained playback and input checks

The same default player `2a03b849` and Studio `e3613638`, with private RBF
`5595ba32` and official stock Main `9f6e5a23`, now pass a fresh ten-minute
music/resource test each. Both original monitors collect 3,047 samples over
600 seconds with zero underruns, one cartridge BOOT and flat sampled parent
and interpreter memory. Measured audio clocks are 47,999.254 and 47,999.236 Hz;
fitted queue drift is -0.343 and -0.012 ms. Both HDMI checks infer 74.25 MHz.
The four original frontend/monitor jobs exit 0, and independent review verifies
normal installed-Studio restoration, protected files, games-folder metadata,
source caches and Frontier resumption. Receipts are
`build/mgl-music-native-v1-20261006`,
`build/mgl-music-native-review-v1-20261006` and
`build/mgl-music-owner-v1-20261006`.

Fresh native production-worker helpers also exercise these exact default
binaries across all fourteen languages and three cartridge formats, for 84
cases. All 168 full RGBA frames and 5,040 PCM tick buffers match frozen
references. Six original preparation/matrix jobs exit 0. The independent
review verifies the 84 original workers, logs, payload identities and unchanged
installed Studio/Main. This direct-worker test never maps DDR or changes the
selected core; it does not replace Main/FPGA cartridge integration or physical
output testing. Receipts are `build/native-runtime-matrix-board-v3-20261006`
and `build/native-runtime-matrix-board-review-v3-20261006`. The earlier v2
helper preparation attempted to invoke a Linux compiler from Windows Python;
that failure is preserved, and the fresh v3 build succeeds under WSL.

Both exact frontends additionally pass nine native synthetic mouse API stages
with actual stock Main owning evdev, including horizontal scrolling, stalled
device isolation and real OSD epoch suppression. All four original probe/
frontend jobs exit 0. Independent review verifies their raw observations,
restoration, all nine protected files, source caches, Frontier and 4,377
unchanged games-folder metadata rows. Installed Studio is restored normally.
Receipts are `build/mgl-hid-native-v1-20261006`,
`build/mgl-hid-native-review-v1-20261006` and
`build/mgl-hid-owner-v1-20261006`. Synthetic reports do not qualify the user's
physical controller, mouse or Bluetooth keyboard.

The next candidate's installer and corresponding-source ZIPs are prepared
privately and independently reviewed, with all five payload identities,
49 license notices and five screenshots. Source packaging now accepts a
checksum-bound per-core candidate manifest while retaining the first-preview
default. These ZIPs remain private; the published release and installed
payloads are unchanged. Current-binary human picture/stereo confirmation and
physical controls are still pending. Linux's latest physical inventory contains
only MiSTer's virtual keyboard; no human input is injected or credited.

### October 6: integrated stock-Main cartridge matrix

The same exact player `2a03b849`, Studio `e3613638` and RBF `5595ba32` now
pass all 84 actual stock-Main MGL cartridge loads: fourteen languages in native
TIC, modern PNG and legacy PNG formats, through both default frontends. This
exercises Main's file transfer, the FPGA mailbox, the retained frontend,
interpreter execution and native scaler output together. It complements the
earlier direct-worker checks, which did not map FPGA memory.

For every case, a read-only, session-bound DDR snapshot matches every received
cartridge byte to the selected fixture, with the correct ticket, size and ACK.
This matters because different language demos can produce the same picture.
The stock-Main screenshot matches every RGB channel of the full 256 by 144
image to a frozen reference pose. These comparisons cover RGB, not alpha or
the TV's physical HDMI presentation. Each original frontend survives all 42
loads without a changed process identity. Each case also has a two-second
audio/frame monitor with at least 100 samples, active playback and zero
underruns. These bounded monitors complement the earlier ten-minute soaks;
they are not another sustained-playback test or a PCM comparison.

All 338 original frontend and measurement jobs terminate with exit 0. The
independent review rechecks the 84 raw payloads, 84 screenshots, monitor logs,
fixture/reference manifests and source hashes. It also verifies restoration
of normal installed Studio, all nine protected files, 4,377 games-folder
metadata rows, source caches and Frontier. Only fresh UUID-named test
screenshots were removed after their hashes were checked; existing screenshots
were preserved. Receipts are `build/stock-main-matrix-native-v1-20261006`,
`build/stock-main-matrix-review-v1-20261006` and
`build/stock-main-matrix-owner-v1-20261006`.

The payload snapshot helper passes host and ARM/QEMU negative and read-only
checks in `build/cart-payload-snapshot-v2-20261006`. The original v1 ARM build's
signed-size compiler failure remains preserved. The matrix oracle also has
five local regression tests, including wrong delivered language bytes despite
an identical picture, wrong ACK/size/source metadata and a mismatched final
RGB pixel. No production binary changed during this matrix work. Current
physical controls, human picture/stereo confirmation, remaining recovery
checks and final release acceptance remain open; full qualification is false.
