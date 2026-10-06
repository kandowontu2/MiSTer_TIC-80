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
audio and HDMI. Then make the default manifest omit Main and make its installer
preserve the shared executable. Keep any optional experimental Main extension
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

The decoder is not yet connected to native HIDraw discovery or the frontends.
Device GET feature requests can block; that I/O must be isolated from audio
publication. Hotplug, OSD queue draining, bounded worker failure/recovery and
actual report delivery while stock Main owns evdev still need verification.
The proposed evdev path must not be treated as functional stock-Main horizontal
scrolling before that work is complete.

The first fresh SDK compile exited 2 because its build-time Forth bootstrap
was dynamically linked and QEMU could not find the target loader. Its original
failed coordinator/result remain in `build/linux-wheel-sdk-v2-20261005`.
The helper now links statically; a separate target dictionary generation passes
and its ELF has no interpreter segment. A corrected full build uses the fresh
frozen snapshot in `build/linux-wheel-sdk-v3-20261005`; its result must be checked
separately. None of these local checks establishes full stock-Main qualification.
