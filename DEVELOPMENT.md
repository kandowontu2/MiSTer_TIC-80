# TIC-80 for MiSTer

TIC-80 now has its own [Frontier install script](Scripts/Install_TIC80.sh), using
the same per-core launcher arrangement as PICO-8. The
[development installer release](https://github.com/kandowontu2/MiSTer_TIC-80/releases)
contains the core, ARM frontends and handler; it preserves the shared MiSTer
executable. Extract it to the SD root and run **Install_TIC80** from the Scripts
menu. See [installation and rollback](docs/install.md). Complete stock-Main
runtime qualification remains pending; this installer is a development package.

A working hybrid TIC-80 development core is installed and packaged as build
`18de1e89` (October 5, 2026). Studio is the default frontend; a cartridge player
is also included. The matched Main, FPGA, ARM frontends and launcher run through
the existing Frontier Master Daemon. Tetris is running on the tested MiSTer at
standard HDMI 720p/60.

The earlier matched prototype package is retained locally as a historical artifact. It is not an asset of the Frontier installer release.
This prototype package requires its patched Main. The default release target
is standard MiSTer without replacing that shared executable; see
[standard-Main compatibility](docs/stock-main.md) for the dependencies and checks.
Use the bundle's `USAGE.txt` and `manifest.json` when installing on another board.
Cartridges belong in `games/TIC-80/Carts`; saves belong in `saves/TIC-80`.
The tested board retains its settings, existing cartridges and saves, with
verified rollback backups for Main and the replaced payloads.

Both Studio and player pass ten-minute music/save/clock/memory tests with zero
measured audio underruns, valid saves and stable sampled memory. The user
confirms stable Samsung HDMI picture and clear stereo audio for both frontends.
All 84 combinations of fourteen language demos, native/modern-PNG/legacy-PNG
cartridges and both frontends pass Main/FPGA loading and exact full-frame RGB
checks. Separate native production-worker checks match RGBA frames and PCM.
Physical Xbox, mouse and Anker A7726 Bluetooth keyboard diagnostics pass.

The remapper names the face actions Z/X/A/S. Xbox defaults also map the left
analogue stick to W/A/S/D, Start to Enter, Back to Esc and LB/RB to Q/E;
LT/RT can be selected in the remapper. The Bluetooth keyboard remains paired
and trusted, and reconnects after the user wakes it in the observed session.

This is a qualified development build. Broader cartridge/API and Studio editor
coverage, Bluetooth power-cycle/device coverage, CRT and microphone hardware,
physical SD power-loss durability, and complete external timing/latency remain
unverified. See [release status](docs/release-status.md) and
[validation evidence](docs/validation.md) for the tested scope and earlier runs.

The architecture follows [MiSTer PICO-8](https://github.com/MiSTerOrganize/MiSTer_PICO-8):
the DE10-Nano ARM processor runs the official
[TIC-80 runtime](https://github.com/nesbox/TIC-80); the FPGA supplies video timing,
stereo output, controller transport and MiSTer OSD integration.

## Current contents

- `tic80-studio-live`: a separate Studio frontend using the FPGA frame/audio
  transport and physical input snapshots. Host integration tests cover console
  rendering, RUN, controller/remapped-key input and release, frame ownership,
  and clean core departure. A bounded native Tetris run completes 900 ticks
  with zero underruns in its ten-second observation. This frontend is the installed default launcher. Bounded native
  OSD/reset and physical-input checks pass; broader Studio feature and device
  coverage remain open. `--saves` selects an
  existing directory containing the service's keyed persistent-memory files;
  the parent saves only acknowledged Studio values. Failed save transitions
  return to the last acknowledged editor state with a visible warning; RUN
  can be retried in the same session after storage recovers. Failed RUN save
  reads also keep the console open, preserving the rejected file for repair
  and a later retry. Paced read-stall tests also cover timeout recovery and
  core departure during a blocked load. The frontend now consumes MiSTer OSD
  cartridge tickets through asynchronous load/RUN requests; local models cover
  native and PNG games, rejected transfers and the latest queued selection.
  OSD selections now use Studio's unsaved-changes dialog with NO selected by
  default. Cancelling retains editable work; explicit YES loads the candidate.
  Reset or worker recovery recreates the question without inferring approval.
  Reset holds cancel unaccepted work, silence playback and retain the latest
  OSD selection. Release runs the chosen cart once; otherwise it restarts the
  last acknowledged Studio cart, including accepted edits. Local reload models
  cover lost sessions, temporary FPGA absence, initialization gating and repeat
  reloads. Bounded native cold-start, selection and reload checks also pass;
  broader reset/reload qualification remains open.
  CLI native/PNG loads and browser project loads retain their resolved source
  paths. Default Save uses that source after directory changes or recovery;
  an explicitly named Save uses the current browser directory. Cartridge saves
  flush a temporary file, replace the destination, then sync its directory.
  Failed writes keep the editor modified for retry. If a rename succeeds and
  its later directory sync fails, Studio retains the committed destination so
  a retry updates that file. If the worker dies after publication before its
  reply, the replacement worker verifies the prepared inode, content digest and
  cartridge checkpoint before acknowledging the new destination. Edits and the
  unsaved hash remain intact for retry. Verification runs in the worker.
  Managed Saves also record each private temporary before writing. Recovery
  and cancellation reclaim a matching recorded inode in a worker; unrelated
  files, replacement inodes, symlinks and FIFOs remain intact. The gap before
  registration and loss of the supervisor's in-memory record remain work.
  A candidate filename
  protocol now carries OSD source paths with cartridge tickets; it requires the
  staged companion Main patch and the new RBF. Native testing now verifies the
  acknowledged Tetris source filename through this candidate combination;
  broader Studio OSD/dialog/source-selection qualification remains open.
  The worker compares the file with the transferred bytes when a selection is
  approved. Missing or changed files use a working copy. Its first default Save
  creates a new file, skipping occupied names; subsequent saves update that copy.
  For example, a later cart saves as `MiSTer cart-2.tic` while keeping
  `MiSTer cart.tic`. Studio shows a working-copy message when no original file
  can be verified. Existing Main/RBF
  combinations continue loading carts without original source paths.
  Uppercase `.TIC` and `.PNG` source names keep their format and destination
  when saved; filename suffix checks also handle short names safely.
  The complete companion Main candidate now links with the pinned GCC 10.2.1
  toolchain. Its ABI matches an unpatched build of the same Main pin, and the
  actual MiSTer library-provider audit passes. The native source-path smoke
  check passes; broader protocol restart and mixed-version checks remain open.
  Transport regressions now execute Main's actual SPI helpers against the
  pinned HPS download parser and both current and accepted older cart loaders.
  Restarted transfers discard incomplete carts before sending fresh source
  metadata; delayed acknowledgements preserve the previous cart and filename.
  These are local simulations; hardware restart checks remain open.
  The next companion Main also holds a cached game during the first MGL file's
  delay. Download completion waits for FPGA cartridge/source writes and ticket
  publication, and DDR pairs reset release with that ticket. The regression
  uses Main's real MGL parser (which normalizes file type to `F`), status sender
  and SPI handshake. Hardware qualification of this handoff continues.
  A supervised Studio
  regression drives the MiSTer input converter to switch all five toolbar
  tabs, select bank 7, paint a sprite pixel and undo/redo the edit. Recovery
  checkpoints now retain editor bank selections; a hung run returns to the
  sprite editor and a subsequent mouse edit still affects bank 7. Sprite view
  checkpoints also retain the selected tile, colors and drawing tool; local
  regressions continue painting and filling with the pre-failure selections.
  Code checkpoints retain cursor/selection offsets, both scroll axes and the
  preferred vertical column. Local recovery tests replace selected text and
  resume navigation after hung runs and worker termination.
  Local recovery now preserves all 41 undo/redo histories: code plus sprite,
  map, sample, waveform and music in all eight banks, including recovery while
  positioned before the latest edit. Changed history travels in a sealed
  file with its frame acknowledgement; the supervisor retains the accepted
  descriptor until a complete replacement acknowledgement passes validation.
  History restoration keeps acknowledged RUN asset changes until actual Undo.
  Keyboard and mouse bookmarks also survive recovery, including clearing all
  marks without editing text or adding an undo step. Vi checkpoints retain
  Normal, Insert, Select and both seek modes; unfinished Insert edits and
  bookmarks recover within the original single undo group. Code search,
  replace, goto and bookmark/outline sidebars retain their queries, origin
  selection, scroll and popup transitions across recovery. Replacement labels
  fit the popup buffer, and long goto numbers clamp safely. Escape closes the
  active code view before leaving its underlying Vi mode; cancelling a view
  preserves an unfinished Insert edit within its original undo group. Drag
  cancellation also works during opening and prevents a held mouse from
  restarting the gesture during closing. History snapshots batch small undo
  records through a fixed 64 KiB buffer while retaining every undo node and the
  existing format. Native history memory,
  latency, broader physical editor workflows and editor views remain work.
- `tic80_studio_system`: the pinned console, code and asset editors, project
  formats and local SURF UI without SDL. The offscreen integration contract
  covers text entry, clipboard shortcuts, mouse sprite painting, undo/redo,
  save/reload and running an edited cartridge. This library is not yet connected
  to `tic80-live`; supervised editor execution and hardware presentation remain
  in progress. Its asynchronous HTTP backend uses MiSTer's curl, with bounded
  downloads, timeouts, cancellation, verified HTTPS and validated cartridge
  caches. Build its editor contract with
  `cmake --build build/linux --target studio_test` and run
  `ctest --test-dir build/linux -R '^studio_console_editors$' --output-on-failure`.
  Run the network contract with `ctest --test-dir build/linux -R '^studio_http$'
  --output-on-failure`. Studio networking needs `curl` and the bundled
  `cacert.pem` beside the executable; `TIC80_CA_BUNDLE` can select another CA
  file. CMake copies the pinned Mozilla CA bundle into each Studio build folder.
  Its supervisor retains an exact cartridge checkpoint. Linux page protection
  identifies writes to interior pages; changed pages and partial boundaries
  use equality scans, with NEON on ARM. Unsupported or sanitized builds scan
  the whole cartridge. For phase timings, configure with `-DTM_BUILD_STUDIO_PROFILE=ON`,
  build `studio_session_profile_test`, and run it with
  `--benchmark-cart path/to/cart.tic`. The profiler is separate from the normal
  library and reports tick, sound and checkpoint costs.
- `src/video.c`: portable conversion of a complete TIC-80 RGBA8888 output frame
  to tightly packed RGBA8888 without losing color precision. Keeps the 256x144 border-inclusive frame, including
  the 240x136 gameplay region.
- `tests/video_test.c`: verifies colors, stride, edge pixels, buffer bounds,
  and rejection of invalid buffers.
- `tic80-player`: all 14 pinned upstream runtimes with deterministic offline ticking,
  full-color frame packing and PCM export; built for Linux and MiSTer ARMv7.
- `project-to-cart`: uses upstream's project parser to convert supported projects to `.tic`.
- `src/runtime_*.c`: cartridge VM isolation for interpreters whose upstream
  adapters share global state. Candidate validation preserves the running game.
  Host and ARM builds keep MRuby archives and Forth dictionaries separate.
  Staged SFX binding repairs preserve Ruby's explicit playback speed and reserve
  Wren's scratch slot before reading stereo-volume lists. The audio contract
  compares direct and isolated-worker PCM against C API calls across all 14
  runtimes, including Wren's default-speed and explicit-speed list overloads.
  Janet rejects out-of-range sound channels with numeric error messages;
  WASM traps invalid nonnegative effect IDs before reaching the sound engine.
  A fault-isolated boundary corpus also checks accepted stop IDs and WASM's
  existing invalid-channel no-op behavior, with an independent game still running.
  JavaScript returns effect/channel exceptions immediately; Scheme and Forth
  reject unsafe sound arguments before engine access. An expanded fourteen-runtime
  corpus checks channel edges, signed 32-bit extremes and stop calls against
  independent C audio, direct execution and isolated workers.
  MiniScript uses effect 0's stored pitch when note arguments are omitted.
  Lua, MoonScript, YueScript and Fennel preserve each stereo-volume table value.
  A separate preset/default corpus compares complete direct and worker PCM
  against explicit engine calls for these five runtimes.
  Named notes require a supported uppercase note pair and octave digit 0–8.
  Invalid names leave parser outputs and the playing sound unchanged. Scheme
  uses the common octave mapping; JavaScript returns its note exception before
  reaching the sound engine. A twelve-runtime corpus checks numeric pitch
  equivalence, error diagnostics, complete PCM and an independent game.
  The documented numeric note sentinel `-1` preserves stored pitch. Scheme
  uses stored pitch and speed for omitted arguments; Python distinguishes
  omitted speed (`None`) from explicit `-1`. Null note placeholders retain
  presets, and typed Forth/WASM calls resolve the `-1,-1` pitch pair while
  preserving explicit speed zero. A fourteen-runtime default/override corpus
  compares complete PCM and channel state with an independent C plan.
  Janet's smoothed FFT call dispatches to `ffts` rather than `fft`. A separate
  binding corpus checks distinct FFT results, arguments and worker isolation
  in all fourteen runtimes. Staged bindings add WASM/MiniScript FFT functions
  and Forth raw FFT words. WASM uses `(i32,i32)->f64`; Forth raw results retain
  its scale of 65535 and saturate to signed 32-bit range. MiniScript argument
  guards and incompatible WASM imports are checked before engine calls.
  FFT gain uses the current spectrum to avoid startup and rising-level spikes.
  Invalid capture channels become silence; magnitude overflow cannot poison
  gain or smoothing. Independent direct-transform tests check every FFT bin
  and all four range APIs. Range queries still return sums, which can exceed one.
  VQT also uses a stable magnitude calculation for large finite inputs. A
  separate oracle checks sparse Hamming kernels, whitening on/off, smoothing,
  peak tracking and all eight VQT APIs against independent transform and
  matrix calculations.
  Staged WASM, MiniScript and Forth bindings expose all eight VQT functions.
  WASM uses `(i32)->f64`; Forth retains the 65535 spectrum scale with signed
  saturation. A separate fourteen-runtime corpus checks argument and result
  routing through direct interpreters and supervised workers. A capture-disabled
  variant also calls the real backend and checks zero results.
  A real-capture corpus compares all VQT bins with the independent spectrum
  oracle in all fourteen runtimes, through direct interpreters and exec workers.
  It checks stereo cancellation, level changes and capture pause/resume while
  preserving script state. The capture-device boundary supplies deterministic
  PCM; native microphones and provider behavior still require hardware tests.
  Spectrum argument guards reject nonfinite or out-of-range floating values
  before integer casts in Lua-family, Wren and Squirrel bindings. JavaScript
  conversion exceptions propagate before spectrum queries. A seven-runtime
  corpus checks finite truncation, existing coercions and defaults, invalid
  calls and worker recovery across all twelve FFT/VQT functions.
  Forth native words check their required stack depth before C dispatch and
  raise the standard stack-underflow exception when operands are missing.
  Incompatible bootstrap C-test aliases are rejected. The admission corpus
  covers all 62 words, exception handling and fresh execution-worker recovery;
  most native C bodies are doubled there and checked separately by API tests.
  Linux builds include opt-in ALSA microphone capture. The cartridge service
  and Studio accept `--fft` for the default input or `--fft-device name` for a
  matching named input; capture remains disabled unless requested. Capture
  opens in the supervised worker. An unavailable microphone reports a status
  and leaves execution usable with zero FFT input. Builds can omit support
  with `-DTM_ENABLE_FFT_CAPTURE=OFF`.
  Broader API compatibility and native audio qualification remain open.
- `tic80-fft-probe`: a separate Linux microphone diagnostic using the real
  miniaudio ALSA backend and FFT/VQT calculations. Its staged backend repairs
  capture resource ownership and frequency clamping; local tests use known
  PCM and injected device failures. The diagnostic accepts `--list` or
  `--capture device-name-substring`. The worker frontends share this repaired
  backend. Native microphone/provider ABI and simultaneous playback checks
  remain pending; local device doubles do not establish board support.
- `src/runtime_tick.c`: persistent per-player callback data and clock epoch.
  Elapsed `time()` survives ticks and supports pause/resume and fresh resets.
- `src/exchange.c`, `fpga/rtl/tic80_video_control.sv`: C producer and synthesizable
  session/frame controller, verified together with Verilator.
- `src/backend.c`, `tic80-live`: reserved DDR mapping, ARM barriers, live core
  identity/heartbeat checks, session establishment, frame/audio transport,
  four-pad button mapping, and real-time cartridge execution.
  Physical transport operations also check MiSTer's selected core; short
  CORENAME rewrites pause transport without restarting the game. Leaving the
  core flushes the cartridge save and exits without waiting on the old FPGA.
  Cartridge transfers retain private payload and source-path copies before
  releasing staging. Failed/no-ticket reads clear their outputs; accepted and
  rejected transfers both recheck the session and ticket before acknowledgement.
  Guarded maximum-size and deferred worker-request checks pass locally on host,
  AddressSanitizer and ARM/QEMU; native copy timing and audio impact remain open.
- `src/pacer.c`: startup tick spacing followed by pacing from the FPGA's
  consumed-sample counter. Keeps a 1,600-frame playback reserve, handles counter
  wrap, and avoids accumulating the difference between the Linux and FPGA clocks.
- `src/serve.c`: menu cartridge service with splash/error screens, bounded
  loading, hot-swap, reset, and preservation of the running game after bad loads.
  Physical playback and its interpreter use CPU 0, separate from MiSTer's Main
  on CPU 1, with CFS nice -10 to protect audio deadlines during transfers.
  The save thread uses nice 19. A private deferred picture keeps frame acknowledgment waits from
  blocking production of the next audio tick.
- `src/vm.c`: supervised cartridge processes for all 14 runtimes. Hung or
  crashed candidates preserve the current game; later failures return to the
  menu service and save the last completed tick. The launcher stays responsive.
- `src/cart_file.c`: bounded PNG decoding for both upstream's compressed `caRt`
  chunks and older cartridges hidden in pixel bits. Live decoding runs in the
  cartridge worker; malformed images cannot enter upstream's unchecked loader.
- `src/runtime_miniscript.cpp`: restores synchronous raster/menu callbacks
  while preserving MiniScript's yielded main loop.
  The generated MiniScript binding also corrects specific-key `keyp()` calls
  to use keyboard state, including hold/repeat, instead of controller state.
- `src/runtime_wren.c`: isolates cartridge handles and skips the inherited empty
  `OVR()` callback so it cannot erase graphics drawn in the second video bank.
  Explicit callbacks, including inherited overrides, retain legacy behavior.
- The generated WASM adapter writes complete 32-bit results for `peek`,
  `peek1/2/4`, `pix` and `vbank`. Its previous byte writes leaked stale high
  bits from earlier calls. Regression cases cover ordinary cartridge execution
  and seeded interpreter result slots.
- `src/input.c`, `fpga/rtl/tic80_input.sv`: physical-key state, extended keys,
  modifier aliasing, mouse movement/buttons/wheel, and coherent DDR snapshots.
  Local tests cover these paths. A Linux uinput probe on the board
  passed through MiSTer's normal input path into a real Lua cartridge; keyboard
  controls also started Tetris. The connected Xbox controller and optical mouse
  passed the basic physical diagnostic. The Anker A7726 Bluetooth keyboard now
  passes 22 physical keys, press/release edges, three modifier combinations,
  held-W repeat and all eight default gamepad actions on candidate `18de1e89`.
  The user confirms the matching labels; broader editor behavior, reconnects
  and four-controller hardware coverage remain open.
  Horizontal-wheel support is implemented in a separate development candidate:
  Main normalizes evdev coarse/high-resolution reports, a core-specific UIO
  transaction accumulates signed deltas, and the coherent DDR snapshot feeds
  TIC-80 `mouse().scrollx`. Linux Studio reverses platform horizontal input;
  the player applies the same cartridge convention. Horizontal bursts retain
  excess motion with symmetric limits to avoid six-bit negation overflow.
  Nineteen targeted host checks and the ARM input
  fixtures under QEMU pass. The corrected candidate passes 44 automated native
  checkpoints across Studio and player. Fresh long-run display/audio and
  physical horizontal-wheel qualification remain open; the installed
  qualified build is unchanged.
  This revision uses protocol TIC3 and requires its matching RBF/player.
- `src/pmem.c`: checked, atomic persistent-memory saves with a background
  autosave worker and upstream-compatible cartridge identity keys. Fault tests
  cover slow storage, interrupted/failed writes, directory flush reporting,
  snapshot coalescing, recovery after temporary I/O failures and process death
  around rename on desktop and ARM.
  Its snapshot API also accepts a supervisor's private acknowledged values;
  runtime and snapshot interfaces share the same checked save format. Connecting
  Studio's `--saves` path loads these files before RUN, retains recent values
  across restarts and queues completed parent snapshots for autosave. Shared
  save identities prepare on a background thread while the frontend stays
  paced. Shutdown still waits for final durability; physical storage and shared
  native source-packet qualification and broader project support remain work.
- `fpga/rtl/`: serialized DDR master, two-bank BRAM scanout, and 48 kHz stereo
  reader with an asynchronous FIFO. Playback starts after two TIC ticks are
  published; shorter clips start after a bounded 50 ms wait so they can drain.
  `tic80_pixel_enable.sv` generates the 341×262 raster from
  the playback clock: exactly 800 audio sample periods per frame, with
  four/five-clock pixel enables. The first candidate was rolled back after the
  HDMI TV rejected its doubled output clock. A revised PLL-controller build
  is installed, measures the correct 720p/60 clock, and passes the user's stable
  picture and stereo audio checks. With the CPU 0 player it also passes motion
  stress, 32 reloads, all 28 native/PNG demo comparisons and a ten-minute
  synchronized-clock soak with zero underruns. Broader output-mode and latency
  qualification remains open. See `docs/validation.md`.
  The larger measurements above bind player `85828e73`; the subsequent
  MiniScript keyboard and Wren video-bank repairs have focused native ARM and
  cartridge checks. The asset-bank contract covers all 14 runtimes, banks
  0/1/7, write-back, bank isolation and both palette/video banks.
  Player `09951069` also passes a fresh ten-minute soak with zero underruns,
  flat memory, valid saves and 0.173 ms fitted audio-queue change.
  Validation records keep these artifact scopes separate.
  The native video path runs on hardware;
  the hardware playback counter consumed 2,880,000 stereo sample frames during
  a 60-second music demo. The user confirmed the HDMI picture and continuous
  stereo music; broader output-mode and listening coverage remain open.
- `fpga/rtl/tic80_cart_loader.sv`: byte-wide MiSTer downloads, partial-word
  writes, a 4 MiB bound, and staging ownership through ARM acknowledgment.
- `games/TIC-80/_handler.sh`: launch hook for the existing Frontier Master Daemon.
- `protocol/memory_map.json`: shared DDR layout with generated C/RTL constants.
- `tic80-runtime-monitor`: read-only JSONL samples of FPGA audio output slots,
  underruns, playback, frame adoption and heartbeat. Optional statistics occupy
  reserved protocol words and use their own coherent sequence and magic.
- `tools/test_live_soak.py`: guarded native music/clock/save soak, including
  CRC-checked autosaves and a zero-underrun acceptance check.
- `tools/mister.py`: development SSH transfers and commands; credentials stay
  out of source files. Does not switch the board's current FPGA core.
- `docs/validation.md`: hardware measurements, output parity and limitations.
- `docs/bring-up.md`: implementation sequence and hardware acceptance checks.
- `reference/`: ignored local checkouts of both upstream projects, for research.

## Build and test on Linux (WSL is supported)

```sh
python3 tools/bootstrap.py
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux -j4
ctest --test-dir build/linux --output-on-failure
```

The default build enables Lua, JavaScript, MoonScript, YueScript, Fennel,
Scheme, Squirrel, Python, Wren, Janet, WASM, Ruby, MiniScript, and Forth, plus
legacy cartridge support. All 14 native demos pass desktop and MiSTer ARM
tests and match frame/PCM hashes after 600 ticks. Native menu loading also
passes for all 14; their live captures match desktop reference frames.
Broader cartridge/API coverage,
microphone FFT capture and editor/SURF features remain unfinished.
The host build needs a C/C++ compiler, CMake, Python, Ruby and rake. ARM builds
also need the cross C/C++ compiler and `qemu-arm` (Ubuntu's `qemu-user` package)
to generate a dictionary with the target's cell size. For a Lua-only diagnostic
build, pass `-DTM_BUILD_EXTENDED_RUNTIME=OFF`.
Install Verilator to include the C/RTL co-simulation in CTest.

On Ubuntu, restore the build dependencies with:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends build-essential cmake git python3 ruby rake gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf qemu-user qemu-user-binfmt verilator curl ca-certificates
```

Keep the workspace, reference repositories and build directories on Windows
storage when using WSL so they survive replacement of its Linux disk. This
workspace's `build` and `reference` junctions point to
`D:/CodexBuilds/tic80-mister/`. Use
`TMPDIR=/mnt/d/CodexBuilds/tic80-mister/tmp` for compiler temporary files;
that directory must exist before building.
ARM worker tests also require QEMU's `qemu-arm` registration under
`/proc/sys/fs/binfmt_misc`; it allows spawned ARM children to execute. On a
systemd-enabled WSL distribution, `sudo systemctl restart systemd-binfmt`
reloads the installed registrations.

For portable conversion/protocol tests on Windows, configure with
`-G "MinGW Makefiles" -DTM_BUILD_RUNTIME=OFF`. The runtime player uses POSIX
monotonic time and is currently tested on Linux.

## Build for the MiSTer ARM CPU

```sh
cmake -S . -B build/arm -DCMAKE_TOOLCHAIN_FILE=cmake/arm-linux.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/arm -j4
```

Requires `arm-linux-gnueabihf-gcc` and `arm-linux-gnueabihf-g++`. Executables are statically linked to avoid
depending on the development host's newer glibc. This builds the runtime and
test tools, including `tic80-live` for the FPGA transport.

An additional microphone-enabled profile uses the GNU Arm
`10.2-2020.11-x86_64-arm-none-linux-gnueabihf` SDK:

```sh
cmake -S . -B build/mister-sdk -DCMAKE_TOOLCHAIN_FILE=cmake/mister-arm-linux.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/mister-sdk -j2
```

The default SDK location is `build/toolchains/` followed by that SDK directory
name. Set `TM_MISTER_SDK` in the environment or with `-D` to select another
location. The profile checks GCC 10.2.1 and glibc 2.31, links libc dynamically
for ALSA, and links the GCC/C++ support libraries statically. A conditional
MiniScript descriptor adaptation preserves its original field values while
avoiding GCC 10's anonymous-struct initializer error.
The complete Studio build also links with this SDK. A shared process helper
preserves worker descriptors and process groups, closes unrelated descriptors
and searches PATH for curl on glibc 2.31. SDK descriptor tests and local ABI
checks pass; broader integration regressions and native validation remain open.

Audit the candidate against copies of the target's actual library directories
before deploying it. Include the ARM loader, libc and their dependencies as
well as ALSA:

```sh
python3 tools/check_runtime_abi.py --binary build/mister-sdk/tic80-live \
  --library-root path/to/copied-lib --library-root path/to/copied-usr-lib \
  --dlopen-library libasound.so.2 --miniaudio reference/tic80/src/ext/miniaudio.h
```

The audit checks ARM hard-float requirements, strong imports, the glibc version
ceiling and miniaudio's ALSA symbols. Passing against local SDK providers is
offline evidence; target libraries and physical capture still need validation.
Keep temporary ALSA fixture configurations and FIFOs on Linux storage under
`/tmp` when testing this profile in WSL.

## Build the FPGA core

Quartus Prime Lite 17.0 with Cyclone V support is required. Stage the pinned
MiSTer framework and PLL alongside the project, then compile:

```sh
python tools/prepare_fpga.py
cd build/fpga
quartus_sh --flow compile TIC80.qpf
python ../../tools/check_timing.py output_files/TIC80.sta.summary
quartus_sta -t ../../tools/audit_audio_cdc.tcl
quartus_sta -t ../../tools/audit_video_clock.tcl
```

Output is `build/fpga/output_files/TIC80.rbf`. The staging script copies the
pinned framework and applies the files under `fpga/`, including an explicit
HDMI PLL-controller adapter at `fpga/sys/pll_cfg/pll_cfg_hdmi.v`. That adapter
uses the complete bundled Intel reconfiguration IP. Its seed-11 image measures
74.249–74.250 MHz at 720p/60 and passes the user's TV check; longer live
qualification is in progress. The preceding controller left HDMI at 148.5 MHz
when 720p/60 requested 74.25 MHz. On this development machine Quartus
is installed under `C:/Users/kando/intelFPGA_lite/17.0/quartus/bin64`.
On Windows, `powershell -File tools/build_fpga.ps1` stages and builds the
project, then rejects negative timing slack even if Quartus exits successfully.
It also checks routed delays for all five audio Gray-counter buses and six
video/audio control crossings at four timing corners. Clock-group false paths alone do not qualify these crossings;
the physical checks supplement the remaining full CDC and external I/O work.
Before qualifying a new image on HDMI, `tools/test_live_hdmi.py --host HOST`
checks repeated transmitter lock/VIC and automatic CTS readback for 720p/60.
It requires the matching installed RBF and player; a correct native screenshot
does not prove that the panel receives the requested refresh rate. Keep a
verified rollback available and perform the physical picture/audio check.
The development FPGA profile supports HDMI and RGB analog output. Composite
and S-Video encoding is disabled while its framework timing path is resolved;
`-EnableYC` opts into that experimental build for analysis.

Load a development RBF using MiSTer's `load_core` command, then run
`tic80-live --probe`, `tic80-live --pattern 600`, or
`tic80-live cartridge.tic 3600`. The optional last argument injects the same
one-tick button pulse as the offline player. Payload writes require a live
core handshake; exiting the frontend stops its session. Cartridge persistent
memory is loaded from `<cartridge.tic>.pmem`, saved when changed once per 60
ticks and on graceful exit.

## Development SD-card installation

For a MiSTer with the Frontier Master Daemon already installed, copy:

The current handler defaults to Studio. Its default-console hardware
qualification passes bounded startup/switching audio checks; its new music
soak also passes with a fresh HDMI/stereo confirmation. Broader qualification
remains open. This is a development installation.
Both frontends are required below. An
explicit `frontend.txt` containing `player` selects the cartridge player;
`studio` selects the console. Use binaries whose ABI was checked against the
board's libraries, such as the pinned SDK build used in the validation record.

| Local artifact | MiSTer path |
| --- | --- |
| `build/fpga/output_files/TIC80.rbf` | `_Other/TIC80_YYYYMMDD.rbf` |
| `build/main-mgl-popup-20261003/source/bin/MiSTer` | `MiSTer` |
| `build/sdk-native-candidate/bin/tic80-live` | `games/TIC-80/TIC-80` |
| `build/studio-playback-candidate/tic80-studio-live` | `games/TIC-80/TIC-80-Studio` |
| `assets/cacert.pem` | `games/TIC-80/cacert.pem` |
| `games/TIC-80/_handler.sh` | `games/TIC-80/_handler.sh` |
| `build/arm/tic80-runtime-monitor` (optional diagnostic) | `/tmp/tic80-mister-dev/runtime-monitor` |
| `.tic` or PNG cartridges | `games/TIC-80/Carts/` |

Make both frontends, the handler and optional monitor executable. The monitor's `/tmp`
location must be recreated after a reboot. Select the TIC80 core; the daemon
discovers the handler by its `TIC-80` core identity. Open MiSTer's OSD with your
configured controller menu button, or Win/Command+F12 on a keyboard, and choose
**Load Cartridge**. The handler creates `saves/TIC-80` and `logs/TIC-80`.
The companion Main and RBF form a matched pair; the tested board already runs
this Main. `tools/package_candidate.py` creates a verified SD-card layout from
the sealed Studio playback receipt, including both frontends and license notices.
The qualified package is `build/studio-playback-package-20261003`. On the tested
board, `tools/install_studio_candidate.py` verifies the running companion Main,
records the existing destinations, backs them up, and installs from MENU.
Its `--mode inspect`, `--mode install` and `--mode rollback` commands share
`--host`, `--package`, `--plan` and `--tag` arguments. Set `TM_SSH_PASSWORD`
transiently on the SSH host. A fresh tag and plan are required for each
installation attempt. The installer preserves explicit frontend choices and
checks the daemon and video settings for changes. Rollback restores previous
files and removes destinations that were initially absent; it retains the
already qualified companion Main. The actual board rollback and subsequent
reinstallation pass, with Tetris left running through the installed Studio.
The button remapper names the four actions **Z, X, A, S**, matching TIC-80's
controls for button IDs 4–7, plus **W (Up), A (Left), S (Down), D (Right),
Enter, Esc, Q and E**. The Xbox profiles map the left stick to WASD, Start to Enter,
Back/Select to Esc, and L/R to Q/E (LT/RT can be selected in the remapper);
the D-pad retains the arrow/gamepad directions and
the four face actions retain their existing bindings. Controller WASD acts
as keyboard input without also pressing the A/S gamepad actions.
`tools/controller_defaults.py` generates these core-specific v3 profiles from
a saved global controller map, optionally preserving an existing TIC-80 map.
Xbox left-stick axes default to Linux axes 0/1; other controllers can supply
`--axes X Y`. MiSTer's global controller definition must include the analog
axes so Main can translate both positive and negative stick directions.
Install the generated map in `config/inputs/` using the corresponding
`TIC-80_input_<controller>_v3.map` name, retain a backup, and reload the core.
The FPGA's `jn`/`jp` defaults select the four face buttons, Start and Select;
WASD stick bindings come from these controller profiles or the remapper.
Saves use the MD5 of the upstream `saveid` tag, or bank zero when the tag is
absent. They contain the 256 persistent 32-bit values, a version and CRC.
Autosaves run on a worker once per 60 game ticks; switching, reset and graceful
exit flush the final state. Logs go to `logs/TIC-80/tic80.log`.

The tested Frontier version can interpret Main's briefly empty `CORENAME` file
during a same-core reload as a departure, terminating the cached cartridge.
`tools/patch_frontier.py original-daemon.sh patched-daemon.sh` creates a local
compatibility patch for the known source hash. It adds a bounded 50 ms re-read
for TIC-80 and refuses unrecognized daemon versions. Validate the generated
script with `tests/frontier_test.py --daemon patched-daemon.sh`.
For a controlled hot restart in MENU, `TM_FRONTIER_PRESERVE_S0=1` skips boot-time
mount cleanup and startup sweeps; normal boot behavior retains those operations.
The development board has this patch, with its original daemon retained as
`Master_Daemon.sh.tic80-original`. See the validation record for exact hashes.

The development board includes the 14 template cartridges under
`games/TIC-80/Carts/Language_Demos/`. Open the OSD, choose **Load Cartridge**,
then browse to that folder. The source tests can recreate them with
`language_test --export <existing_directory>`.
The board also includes PNG versions in `games/TIC-80/Carts/PNG_Demos/`.

With TIC-80 selected, the optional monitor can run alongside the service:

```sh
/tmp/tic80-mister-dev/runtime-monitor --seconds 600 --interval-ms 197
```

It prints JSONL without changing the live session. It rejects missing
statistics, a changed session/core, stale heartbeat or persistently torn
snapshots. `tools/make_soak_cart.py --converter build/linux/project-to-cart`
creates a fresh native music/clock/save fixture. The SSH soak harness requires
matching installed artifacts and a trusted host key, and reads its password
from `TM_SSH_PASSWORD`. It flushes the final save by selecting MENU after the
run. Its acceptance checks include startup and steady-playback underruns and
at most 2 ms of fitted audio queue growth over runs of ten minutes or longer.
The 197 ms sampling interval covers tick phases; intervals aligned with 60 Hz
can alias the queue's normal per-tick variation into apparent drift.
The soak also records supervisor/worker RSS and descriptor counts. To measure
buffered audio separately, run `python tools/analyze_audio_queue.py
build/soak/hardware-samples.jsonl --output build/soak/audio-queue.json`.
Its fitted queue change is a latency observation, not an underrun check.

PNG containers and their extracted native cartridges are each limited to 4 MiB.
Cover images may be at most 4096 pixels on either side and four million pixels
in total. The decoder checks PNG chunk bounds/CRCs, the image stream, hidden-data
capacity and decompression bounds. Menu-loaded native and PNG versions of the
same cartridge share their persistent-memory save. `png_cart_test --export <existing_directory>`
recreates the upstream-compatible PNG template fixtures.

For a decoder-only sanitizer stress run, export those fixtures to `build/png-carts`
and build the optional mutation fixture:

```sh
cmake -S . -B build/png-asan -DTM_BUILD_EXTENDED_RUNTIME=OFF -DTM_BUILD_PNG_STRESS=ON \
  -DCMAKE_C_FLAGS="-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/png-asan --target png_mutation_test -j4
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  build/png-asan/png_mutation_test build/png-carts/*.png
```

The runtime also has a separate allocation regression and an instrumented
all-language check. The allocation regression measures live heap after warmup
across repeated core, unused-player and Lua-player closures. The sanitizer run
checks all 14 demos, peer deletion and failed-candidate cleanup:

```sh
ctest --test-dir build/linux -R runtime_memory --output-on-failure
cmake -S . -B build/runtime-asan-extended -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build/runtime-asan-extended --target language_test -j4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 build/runtime-asan-extended/language_test
```

`tools/test_live_lifecycle.py --host <MiSTer>` tests four cold entries and
16 FPGA reloads per supervisor. After allocator warmup it requires RSS to
plateau within 128 KiB, verifies worker replacement, exact BOOT counts and
CRC-checked final saves, then leaves MENU. It requires matching installed
artifacts, a trusted host key and `TM_SSH_PASSWORD`.

For the installed Studio, `python tools/test_studio_lifecycle_native.py
--host <MiSTer>` exercises four cold console entries, 12 native/modern-PNG/
legacy-PNG selections and 64 raw FPGA reloads. It checks exact BOOT counts,
CRC-valid shared saves, supervisor continuity, settled memory and descriptors,
worker placement and audio journals, then restores Tetris. It requires the
recorded installation plan and monitor profile in `build`, a trusted host key
and `TM_SSH_PASSWORD`. The fixtures use a unique saveid and leave the installed
launcher and user cartridges intact.

The prototype depends on that existing daemon for automatic launch. Without
it, start `tic80-live --serve /media/fat/saves/TIC-80` manually after loading
the RBF and creating the save directory. The guarded candidate installer
requires the qualified companion Main and existing daemon; installation on
additional board configurations remains a separate task.

## Exercise a cartridge

```sh
build/linux/project-to-cart reference/tic80/demos/tetris.lua build/linux/tetris.tic
build/linux/tic80-player build/linux/tetris.tic 600 build/linux/tetris.rgba build/linux/tetris.s16le 16
```

The player runs 600 logical ticks without real-time pacing. The optional final
argument injects a 32-bit TIC gamepad mask at tick 1, then releases it; `16`
presses player one's A button. Output frame is 256x144 packed R,G,B,A bytes;
audio is 48 kHz signed S16 little-endian stereo. Use this offline harness for
repeatable cartridge checks; `tic80-live --serve` supplies the live MiSTer frontend.

## Physical controller and mouse diagnostic

With the qualified RBF and ARM player installed, generate a fresh diagnostic:

```sh
python3 tools/make_physical_input_cart.py --player build/linux/tic80-player
```

On the SSH host, set `TM_SSH_PASSWORD` transiently and run
`python tools/test_live_physical_input.py --host <MiSTer-IP>`. The loader accepts
MENU or TIC-80 and refuses to interrupt another core. Leave all controls
released during loading. Press the controller's four directions and A/B/X/Y;
check that the matching labels light up. Move the mouse in both axes, click
left/middle/right and scroll both ways. Release everything for two seconds.
Run the same command with `--inspect` to check the fresh cartridge's saved
observations. It never injects input. This checks one controller and the mouse;
keyboard, multiple-controller and device-specific qualification remain separate.

## Reference versions

Inspected on September 29, 2026:

| Project | Commit |
| --- | --- |
| MiSTer_PICO-8 | `72cb0405417d506c33e59ab51c1a374fc4db649e` |
| TIC-80 | `4dba5bc2640d9cde650fb0b427c9be6aab598de9` |

To recreate the ignored reference checkouts:

```sh
git clone https://github.com/MiSTerOrganize/MiSTer_PICO-8.git reference/pico8
git -C reference/pico8 checkout 72cb0405417d506c33e59ab51c1a374fc4db649e
git clone https://github.com/nesbox/TIC-80.git reference/tic80
git -C reference/tic80 checkout 4dba5bc2640d9cde650fb0b427c9be6aab598de9
```

The integration is GPL-3.0-or-later; see `LICENSE` and `NOTICE`. Reference
TIC-80 source remains MIT licensed. The staging script retains upstream
copyright and license notices with the platform source.
