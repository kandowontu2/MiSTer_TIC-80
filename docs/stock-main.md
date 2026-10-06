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
