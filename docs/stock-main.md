# Standard MiSTer compatibility

Standard MiSTer Main is the default release target. Users should install the
TIC-80 FPGA core, ARM runtime and launcher without replacing `/media/fat/MiSTer`.
`Scripts/Install_TIC80.sh` now implements that installation contract using
Frontier's normal handler discovery. The separate development installer ZIP
omits Main, passes thirteen local installer/package checks and a read-only
bundle check on the MiSTer. It is not a claim of completed standard-Main
runtime qualification; see [installation](install.md).
The October 5 package is a qualified prototype that includes a patched Main;
its hardware qualification does not yet prove the standard-Main release target.

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

Before making a standard-Main package available, verify both frontends on an
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
