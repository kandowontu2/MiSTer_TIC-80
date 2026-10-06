# Validation record

These results prove individual development milestones, **not completion of a
finished MiSTer core**. A native-video development bitstream has been built and
loaded. The board's video pipeline has captured real runtime output; the user
confirmed the connected HDMI picture and audible stereo output during the
corrected music soak. Broader physical-mode and signal qualification remain.

## Builds and tests

- Desktop: GCC 13.3.0 in Ubuntu/WSL, Release CMake build.
- MiSTer executable: GCC 13.3.0 `arm-linux-gnueabihf`, ARMv7-A/NEON/hard-float,
  static ELF with no program interpreter. Embedded runtime configuration disables
  OS script-module discovery, removing the static-glibc `dlopen` dependency.
- Board: ARMv7 MiSTer Linux 6.18.38, Buildroot 2021.02.4, installed glibc 2.31.
- Early SSH artifacts lived under `/tmp/tic80-mister-dev`. The menu milestone
  adds a new `_Other/TIC80_20260930.rbf`, `games/TIC-80` player/handler/demos,
  `logs/TIC-80`, and `saves/TIC-80`; other installed cores were not replaced.
- Eighteen desktop CTest checks pass: RGB conversion, cartridge envelope bounds,
  buffer ownership/audio capacity, generated constants, seven RTL simulations,
  live transport, persistent memory, cartridge service, Lua functionality,
  keyboard/mouse mapping, scripting runtimes, and upstream demos. The full input revision run took
  40.83 seconds (`build/input-final-ctest.log`); the wheel-polarity correction
  subsequently passed the two affected runtime/service checks.
- Persistent-memory validation separately passes on desktop and board: a real
  Lua cartridge increments saved values across two runtime instances, preserves
  all 32 bits at index 255, and rejects a corrupted save without overwriting it.
- C executable checks also run on the board: RGB conversion, cartridge bounds,
  buffer ownership, and actual Lua runtime behavior.

The Lua fixture checks all 32 gamepad buttons using a distinct four-player
mask, `pmem` API writes, 1600 individual S16 samples per tick, audible generated
PCM, full-frame corner pixels, raster palette changes, border colors, and
video-bank overlay compositing. This does not test controller-to-FPGA wiring
or persistent-memory save files.

The C/RTL co-simulation exercises 10,000 frame publications. It checks that
ARM does not write the displayed buffer, adoption waits for vblank and read
completion, stale publication cannot display before session establishment,
and session restart flushes ownership. The portable C test separately checks
sequence wrap and audio ring-counter wrap/capacity. Additional RTL tests verify
held DDR commands through stalls, every word of 12 frame transfers, stale DDR
on boot, RGB scanout pixels, sync timing, blanking-only bank switching, stereo
sample order, the 256-frame asynchronous FIFO, playback counter synchronization,
and underrun silence. A forked mmap transport test exercises startup, delayed
ACK/flush ordering, 32 frame payloads, 25,600 stereo frames including ring wrap,
four-pad mapping, and refusal to write when the heartbeat is stopped.

## FPGA and live video bring-up

Quartus Prime Lite 17.0.0 Build 595 is installed at
`C:/Users/kando/intelFPGA_lite/17.0/quartus/bin64`, including Cyclone V support.
The first native-video build completed successfully in 8 minutes 45 seconds.
It used 7,638 ALMs (18%), 1,564,191 block-memory bits (28%), and 202 RAM blocks
(37%) of the DE10-Nano device. All reported constrained timing paths have
positive slack. The framework still reports unconstrained external I/O;
positive internal slack alone is not a complete board timing qualification.

The RBF was loaded from `/tmp/tic80-mister-dev/TIC80.rbf` using MiSTer's command
FIFO. The live frontend verified `/proc/iomem` excludes kernel RAM from the DDR
payload range, then verified identity, geometry, and a moving FPGA heartbeat.

- Test pattern: 600 acknowledged frames in 10.000 seconds. The MiSTer screenshot
  contains the complete 256x144 gradient with an intact white edge border,
  centered in a 256x224 carrier.
- Upstream Tetris: 3,600 acknowledged frames in 60.000 seconds. The pipeline
  capture shows a running playfield, HUD, next-piece area, colors, and borders.
- Local captures: `build/hardware-pattern.png` and `build/hardware-tetris.png`.

These captures come from the board's MiSTer video pipeline. They do not prove
the connected display's geometry, analog signal quality, or absence of tearing
over long sessions. The first hardware build held audio outputs at zero. The
new stereo ring reader passes simulation. Its first hardware build compiled
but missed HDMI scaler setup timing by 0.191 ns at the cold slow corner; that
bitstream was not loaded. Further fitter builds still missed timing in the
framework's HDMI-clocked YC encoder. The development profile disables YC
encoding and retains HDMI/RGB analog output; composite/S-Video support remains
an unresolved gate. The framework source itself is kept unchanged.

The HDMI/RGB stereo build subsequently passed all 152 reported timing checks,
including every reported temperature/model corner; minimum slack is 0.040 ns.
It uses 7,359 ALMs (18%), 1,572,316 block-memory bits (28%), and 203 RAM blocks
(37%). The bitstream was loaded on the board and the live identity/geometry/
heartbeat probe passed. The music demo completed 3,600 acknowledged frames and
2,880,000 stereo sample frames in 60.012 seconds. The frontend waited for the
FPGA playback acknowledgment, rather than merely copying PCM into a FIFO.
`build/hardware-music.png` captures its image from the MiSTer video pipeline.
This proves the live ring/consumer path and measured playback rate; audible
sound at the connected output still needs listener confirmation.

Development artifact SHA-256:

- HDMI/RGB stereo RBF: `2b993888081093e46f61b4f2f03cfa8bb70b2a603e77d750ef924cd8096c3a10`.
- ARM frontend: `19674b93832e1c3c643aa64ae93f0a5c76babd02901fc6ec92dd03c070fe2828`.

## Desktop versus board output

Each demo ran 600 deterministic logical ticks. A single input pulse at tick 1
starts Tetris/music or plays an SFX. Final RGB565 frame and the complete PCM
stream were hashed separately on desktop and MiSTer; all six hashes matched.

| Fixture | Input mask | MiSTer average tick |
| --- | --- | --- |
| Upstream Tetris | 16 | 5.922 ms |
| Upstream SFX demo | 1 | 2.432 ms |
| Upstream music demo | 16 | 2.654 ms |

Measurements include runtime ticking, sound synthesis, conversion of every
frame, and PCM file writes. They exclude DDR publication, physical video/audio
output and input polling. They are averages, not worst-case frame deadlines.
More demanding cartridges and live pacing must be profiled separately.

Tetris's upstream project explicitly overrides `sfx()` with an empty placeholder;
its PCM is expected to be silent. SFX/music streams contain nonzero samples.

| Output | Matching SHA-256 |
| --- | --- |
| Tetris frame | `c00d2e5b5101fdaeba2fcc40124029e4e8fe36f9fc5ec3f2c48bd83adbbfb6d4` |
| Tetris PCM | `124617c1f65e92d3bc895fbd869e4bb16a30754b198f59e6e973949b9aaa1b01` |
| SFX frame | `c11ac7c4739c0e8f8a2824ca0739c26a8152f1770f3b9e0bf9e646e507196e30` |
| SFX PCM | `085d1ae772916fa09d727375ff8548c23cab5aebc83013b0d7063c79439b04d9` |
| Music frame | `c5f4b2f55d352bb967678fd278a5a83c3d916a370e3d0346a77805409dffc51a` |
| Music PCM | `b99662deb408f9527a81880c23892ab9533f97afdc7abee7323dc39b502a7f11` |

## Native menu and persistence milestone

The first OSD build passed all 152 timing checks with minimum slack 0.091 ns
and used 7,698 ALMs (18%), 1,572,316 memory bits (28%), and 203 RAM blocks (37%).
Its qualified RBF is preserved at `build/TIC80-osd-qualified.rbf`; SHA-256 is
`31fcf1d3321ca19f2ed895576c5897bed091971e937ab834aff5a6e6e17d2fc7`.

MiSTer's MGL automation exercised the native F0 menu transfer path, rather
than copying cartridges into staging from ARM. Two 242-byte Lua fixtures
loaded and hot-swapped. A malformed 23-byte cartridge was acknowledged,
displayed an error notice, and left the current game available to resume.
SD-card saves for both fixtures had valid CRCs; the first fixture retained its
counter across two VM boots. The native 256x144 pipeline capture is in
`build/hardware-osd-test.png`. `build/hardware-splash.png` captures the launcher.

A native cartridge padded with ignored chunks to exactly 4,194,304 bytes loaded
on hardware. A 4,194,305-byte transfer was rejected and acknowledged with the
error ticket, then a 25,147-byte upstream Tetris cartridge loaded successfully.
`build/hardware-osd-tetris.png` captures its title screen. This proves the
staging limit and subsequent recovery on the real MiSTer file-transfer path.

The service uses the existing Frontier Master Daemon without changes to its
startup configuration. Menu/core transitions start and terminate the player.
One early launch attempt stalled its frame/audio acknowledgment and returned
to Menu; its cause was not isolated. Same-core reload handling was then improved:
the live process (PID 9569) retained its private cartridge, established a new
session, and restarted the game after an intentional FPGA reload without a
daemon respawn. The host service test covers this recovery, ordinary reset,
invalid-load recovery, hot-swap, background saves and final flush.

The background save worker and corruption checks also pass on the ARM board.
An additional desktop test run encountered a Verilator internal thread-pool
abort during compilation; the affected DDR test passed when rerun.

The keyboard/MGL reset connection required placement qualification. Seeds 4
and 5 failed cold-corner HDMI scaler timing by 0.003 ns and 0.053 ns; those
RBFs were not loaded. Seed 6 passed all 152 checks with minimum slack 0.106 ns,
using 7,726 ALMs (18%), 1,572,316 memory bits (28%), and 203 RAM blocks (37%).
The new RBF was installed and the native MGL reset test passed: the service
loaded the 242-byte fixture and restarted it on MiSTer's reset command while
remaining in the same process (PID 13763). Both installed binary hashes match
the build artifacts. Physical output, storage
power-loss behavior and long-session stress remain separate gates.

Menu/reset milestone artifact SHA-256 (superseded by the input revision below):

- Menu/reset RBF: `e5578787d46f2f13d6c3e80563b415ecdd71aa03c807f70529609370267f4d3e`.
- ARM service: `ec75e33ee161e8a78d260ac891e05228898dba47b91b312c6746d6dd1ed827b2`.

## Keyboard and mouse milestone

The TIC2 protocol adds all 512 physical held-key bits and a coherent keyboard/
mouse snapshot. Local tests cover every physical bit, all mapped TIC key codes,
modifier aliases, the four-key runtime limit, controller/keyboard combination,
mouse bounds/buttons, wheel queue/counter wrap, OSD filtering, and snapshot
consistency while DDR stalls and live input changes. The service supplies
current input to cartridge `BOOT()` and presents that first tick once.

The initial input placements failed cold-corner hold/setup timing and were not
loaded. Seed 7 passed all 152 reported checks, with minimum slack 0.037 ns.
It uses 8,437 ALMs (20%), 13,686 registers, 1,572,316 memory bits (28%),
203 RAM blocks (37%), and 31 DSP blocks (28%). External I/O and full CDC
qualification remain separate from these internal timing checks.

`tic80-input-probe` creates temporary Linux uinput devices on the board. Its
events traverse MiSTer's ordinary device discovery/translation, HPS transport,
FPGA PS/2 capture, DDR snapshots and the live Lua APIs. The helper checks core
identity and heartbeat before creating devices and stops if another core is
selected. This proves the board input pipeline, but does not qualify physical
USB/Bluetooth devices, layouts, controllers or device-specific behavior.

The first board run exposed reversed vertical wheel direction. After correction,
the fixture's CRC-valid save recorded these values:

| Observation | Result |
| --- | --- |
| A key detected / A presses | 1 / 2 |
| Either Ctrl held, including releasing only the left Ctrl | 60 game ticks |
| F12 presses reaching the cartridge | 1 |
| Final gameplay mouse coordinates | (128,77) |
| Mouse buttons seen / held after menu close | all three / none |
| Accumulated TIC vertical wheel movement | +2 for Linux +3 then -1 |
| Held keyboard after menu close | none |
| `BOOT()` mouse coordinates / boot count | (120,68) / 1 |
| Keyboard A mapped to TIC gamepad X | bit 6 (64) |

Win+F12 opened/closed the OSD while A and left mouse were held; releases during
the menu did not leave either stuck. Evidence is preserved in
`build/hardware-input-after-wheel-fix.pmem` and `build/hardware-input.png`.
A second guarded probe pressed Z to start the 25,147-byte upstream Tetris
cartridge, then Right; `build/hardware-input-tetris.png` shows its running
playfield. Tetris remains selected after this test. These are MiSTer pipeline
captures; physical display appearance is still unconfirmed.

The input milestone's installed RBF/player hashes matched the local artifacts
(the ARM player is superseded by the scripting milestone below):

- TIC2 RBF: `81cac10a486cdc00f394f0525160714d81de1b2492deeda887b3b079b6d16918`.
- ARM service: `9b84af6ce7fbba05344fc386e99e77bf6e170849dacb86858c93a211ecf063b4`.

Verilator 5.020 intermittently aborted its internal thread pool during combined
generation/build commands. The harness now invokes generation and make
separately; all 17 checks passed with that harness. This records the observed
result rather than a guarantee against future tool failures.

## Scripting-runtime milestone

The default player now links all 14 runtimes supported by the pinned source:
Lua, JavaScript, MoonScript, YueScript, Fennel, Scheme, Squirrel, Python,
Wren, Janet, WASM, Ruby, MiniScript and Forth. The ARM executable remains
statically linked, with no ELF interpreter. The source checkout stays clean;
integration changes are staged or implemented by separate adapters.

Python, Janet, Wren, WASM, Ruby and Forth had global interpreter/adapter state
that could corrupt the service's old cartridge during candidate validation.
Adapters now restore the owning cartridge's VM, handles or globals on every
interpreter entry. The runtime test executes two copies of every native demo
in interleaved order, checks matching frames, applies input to only one copy,
then deletes it and continues the other. Additional Python, Janet, Wren,
Forth and Ruby tests check independent counters and resuming after a failed
candidate. Both desktop and real ARM runs pass. Forth also defines a scanline
callback during play, verifying callback-cache invalidation.

The pinned native serializer omitted CHUNK_LANG from its returned byte length.
A staged one-line correction retains explicit language selection even without
a script tag; the negative-candidate tests check the selected interpreter,
avoiding an accidental pass through the upstream Lua fallback.

MRuby uses separate host/target output directories. Forth generates its
dictionary per build: the ARM bootstrap runs under qemu-arm with 32-bit cells,
while the desktop dictionary uses its native cell size. Neither build writes
generated dictionaries into the reference checkout or overwrites the other's
archive. Ruby/rake and qemu-user are additional build dependencies.

The full desktop suite passed all 18 checks in 33.00 seconds. After optimizing
Forth, the two affected runtime/service checks passed again. Forth's absent
optional scanline callbacks had searched its dictionary on every row, costing
19.326 ms per average ARM tick. The adapter now skips absent callbacks and
refreshes their presence when definitions change; the same output hashes are
preserved at 2.334 ms per average tick.

Every native demo ran 600 ticks with a one-tick Right input (mask 8). ARM frame
and PCM hashes match desktop for all 14 languages. Measurements include VM
initialization, rendering, conversion, synthesis and PCM file writes; they are
averages, exclude DDR/display output, and do not prove worst-case frame bounds.
These template demos deliberately produce silent PCM; audible synthesis is
covered separately by the earlier Lua SFX/music milestone.

| Runtime | ARM average tick (ms) |
| --- | --- |
| Lua | 2.510 |
| JavaScript | 2.888 |
| MoonScript | 2.704 |
| YueScript | 2.511 |
| Fennel | 5.752 |
| Scheme | 2.643 |
| Squirrel | 3.878 |
| Python | 2.446 |
| Wren | 2.871 |
| Janet | 2.832 |
| WASM | 2.430 |
| Ruby | 3.838 |
| MiniScript | 2.674 |
| Forth (optimized) | 2.334 |

Evidence: `build/language-parity.json`, `build/language-desktop-bench.log`,
`build/language-arm-bench.log`, `build/forth-arm-final-bench.log`, and
`build/runtime-14-board-final-test.log`. `tools/bench_languages.sh` reproduces
the offline run using cartridges exported by `language_test --export`.

All 14 demos also loaded through native MiSTer MGL/F0 transfers with the
installed service. Each produced a completed 256x144 pipeline PNG, and those
captures match the desktop demos pixel-for-pixel after the FPGA's RGB565 bit
replication (one of the two animation poses, no injected input).
`build/live-languages.json` records all 14 loads; captures are named
`build/hardware-language-<language>.png`. The guarded harness waits for fresh
load evidence and a complete PNG, and stops if another core is selected.
Capture comparison results are in `build/language-capture-parity.json`.
The 14 cartridges are installed under
`/media/fat/games/TIC-80/Carts/Language_Demos/`; their installed SHA-256 values
match the exported fixtures (`build/installed-language-demos.log`).
The Lua-only diagnostic configuration also builds and passes its generated
protocol, input, runtime and cartridge-service checks.

The ARM service SHA-256 at the 14-runtime milestone was
`7eb7f71eb14069b092504a41adb236f1c53b0cc26d722e7fc433cc6b0faa058c`.
It uses the existing TIC2 RBF
`81cac10a486cdc00f394f0525160714d81de1b2492deeda887b3b079b6d16918`;
this milestone changes software, not FPGA timing. Passing these demos does
not establish full language/API, heavy-cartridge or long-session compatibility.

## Runtime clock correction

The pinned public `tic80_tick` recreated callback data on the stack with
`start=0` every frame. Initialization set the first frame's epoch, but later
frames returned host uptime through `time()`. The retained `core->data` pointer
also outlived that stack object. Offline clocks starting at zero hid this bug.

`src/runtime_tick.c` now owns a persistent callback context per public player.
Core initialization starts each new/reset VM's epoch, pause/resume can adjust
it, and VM shutdown finishes before the context is freed. The pinned checkout
remains untouched. A regression with a large nonzero clock failed against the
old implementation, then passed with the adapter. It covers two independent
epochs, BOOT/TIC/raster timing, calls between ticks, pause/resume, reload and
deleting one player while the other continues.

The desktop suite now passes all 19 checks in 33.01 seconds. Its cartridge
service test also checks that a rejected-load notice does not advance the
running game's clock. The real ARM clock test and all 14 language/state tests
pass (`build/runtime-clock-board-tests.log`).

The installed ARM service SHA-256 at the clock milestone was
`90bc8ac8e67e34dea2f2cd8c04b16f15bc37843f6630165ba9bacce0617bf866`;
the TIC2 RBF remains unchanged. A native MGL clock cartridge requested exit
after 5,013 ms and 301 ticks, with a 3 ms BOOT timestamp and a largest observed
tick gap of 27 ms. Its final persistent-memory save passed length/CRC checks.
`tools/test_live_clock.py` reproduces this test; evidence is in
`build/live-clock.json` and `build/runtime-clock-live-test.log`.
All 14 native loading/capture checks were repeated with this installed player;
their captures match newly generated desktop reference frames pixel-for-pixel
(`build/runtime-clock-live-languages.log` and
`build/runtime-clock-capture-parity.log`). The board is left running Forth.
This short clock check does not establish long-session drift or hang recovery.

## Supervised cartridge execution and fault recovery

Cartridges now run in separately exec'd Linux processes for both the menu
service and the direct live player. The service owns DDR transport and saves;
workers exchange input, completed RGBA frames, 48 kHz stereo PCM and pmem
through shared memory with one outstanding socket command. `posix_spawn`
avoids invoking interpreters in a forked copy of a multithreaded save worker.
DDR descriptors close on exec, so children do not retain transport ownership.

Initialization and the first tick (which compiles code and runs BOOT) allow
five seconds; subsequent ticks allow one second. Closing a worker allows
500 ms before forced cleanup. Workers have a 128 MiB virtual-address limit
and no core dumps. These bounds apply to all runtimes and need qualification
against heavier cartridges. An ARM Fennel template cold tick took 1,940 ms,
which exposed an overly short original cold-start limit.

Hung/crashed candidates are discarded while the old VM and clock remain
intact. A later hang or crash returns to the error screen, keeps the service
alive for another native load, and saves the last completed tick rather than
partial writes from the hung tick. Parent death terminates its worker through
the command channel or Linux's parent-death signal; normal shutdown also
reaps it. This is fault containment, not a security sandbox.

The tests also exposed a MiniScript bug: raster callbacks entered synchronous
`RunFunction` while the main harness's yield flag was still set, preventing
the callback from making progress. The adapter clears/restores that flag
around SCN, BDR and MENU. Tests count 136 SCN and 144 BDR calls per frame,
exercise MENU directly, and continue the main loop afterward.

The desktop suite passed all 21 checks in 83.09 seconds. The expanded worker
check passed again after adding nonzero PCM/input coverage and adapting
process inspection to MiSTer's kernel (which omits `/proc/PID/task/PID/children`).
Evidence is in `build/runtime-worker-full-tests.log` and
`build/runtime-worker-final-tests.log`. Desktop service fault tests reject a
hung candidate without rebooting the old VM, recover from a later hang and
killed worker, and interrupt hung execution with SIGTERM while flushing the
old save in about 32 ms (`build/runtime-worker-recovery-tests.log`).

Real ARM tests cover all 14 native templates, with exact frame, sample and
pmem parity against the direct runtime, save identity, timeout paths through
initialization/BOOT/TIC/SCN/BDR, a runaway callback in every language, syntax
errors, explicit exits, crashes, parent death and descriptor/child cleanup.
An additional fixture verifies nonzero stereo PCM, all 32 controller bits,
four keyboard keys and their release, and BOOT mouse position/buttons/wheels
through IPC (`build/runtime-worker-board-tests.log`,
`build/runtime-worker-board-io-tests.log`).

For the tested templates, supervised steady ticks averaged 2.798–4.372 ms,
including IPC and snapshot copying, with an observed maximum of 8.428 ms.
These are 119 steady ticks per template on the board; they exclude DDR output
and do not establish worst-case bounds. Profiles are recorded in
`build/runtime-worker-performance.json`.

The ARM service SHA-256 at the supervised-execution milestone was
`050927713b569714c78acaf153b9533d3d60d80026391ab90eade356372bf10d`;
the corresponding RBF was `81cac10a486cdc00f394f0525160714d81de1b2492deeda887b3b079b6d16918`.
All 14 native MGL loads and pipeline captures pass with this player, and every
capture matches the desktop reference pixels
(`build/runtime-worker-live-languages.log`,
`build/runtime-worker-capture-parity.log`). A native clock fixture exits and
saves after 5,006 ms and 300 ticks, with a largest observed gap of 38 ms
(`build/runtime-worker-live-clock.log`).

`tools/test_live_recovery.py` also exercises native failed-candidate recovery,
a later hang with a partial pmem write, and loading a new cartridge afterward.
The same supervisor PID survives throughout. The hung game's saved counter
is exactly 2, excluding its partial write of 9,999. Evidence is in
`build/live-recovery.json` and `build/runtime-worker-live-recovery.log`.
These MGL transfers reload the FPGA, so cached-game reset/recovery is part
of that hardware check; the desktop fixture separately proves preserving
the same old VM without a reset. Switching to MENU stopped both service and
worker before reloading the demo through the existing daemon.

BusyBox `pidof TIC-80` can include the worker because both exec the same ELF.
The guarded native harnesses distinguish the supervisor by its `comm` name
`TIC-80`; workers use `tic80-vm`. Credentials remain outside source files.
The Lua-only diagnostic build also passes the worker, service and fault
recovery checks (`build/runtime-worker-lua-tests.log`).

## Bounded PNG cartridge support

The menu service and direct players accept both upstream PNG cartridge formats:
zlib-compressed native cartridges in `caRt` chunks, and the older payload packed
into pixel bits. `src/cart_file.c` validates PNG chunk bounds, names, ordering
and CRCs, decodes the image through a checked libpng read callback, and validates
the inflated native envelope. It bypasses upstream's unchecked PNG read path,
including malformed or nested four-byte PNG prefixes. Modern payloads take
precedence over hidden payloads; an empty `caRt` chunk permits legacy fallback.

Both file input and extracted native cartridges have a 4 MiB limit. Covers are
limited to 4096 pixels per side and 4,194,304 total pixels (16 MiB RGBA). Hidden
payload headers must fit the actual channel capacity; zlib streams must finish
within the output bound and consume their complete input. Live image decoding
and inflation execute inside the supervised worker's time/address-space limits.
A malformed candidate leaves the current game and completed save intact.

Host and real ARM tests compare decoded native bytes, save identity, 30 ticks of
RGBA output and stereo PCM across native/chunk-PNG/legacy-PNG versions of all
14 templates. Fixtures use the pinned upstream encoder and cross-check its
legacy decoder. Another 52 covers exercise every PNG color/depth combination,
alpha/transparency, 16-bit stripping and interlacing. Negative cases cover every
truncation, chunk CRC errors, unknown critical chunks, duplicate payload chunks,
corrupt IDAT streams with valid CRCs, hidden-data bounds, truncated/trailing zlib,
invalid native payloads and exact/over-limit extraction. Evidence is in
`build/png-tests.log` and `build/png-board-tests.log`.

The complete desktop run passed 22 checks in 79.61 seconds; the subsequently
added PNG service check passed separately (`build/png-full-tests.log`,
`build/png-service-ctest.log`). It loads native, modern and legacy versions of
one cartridge, preserves their save counter across three boots, rejects a
corrupt image and oversized output while retaining the running VM, and loads a
new native game afterward. The optional standalone mutation fixture ran 60,000
deterministic mutations with ASan/UBSan and leak detection enabled, including
mutations whose chunk CRCs were repaired. No sanitizer errors occurred
(`build/png-asan-mutations.log`). This is bounded adversarial coverage, not an
exhaustive fuzzing result.

Adding `TICPNG` to the OSD's F0 extension list exposed a hardware integration
bug: Main_MiSTer includes the extension ordinal in transfer bits 7:6, so PNG
arrives as index `0x40` rather than native TIC's `0x00`. The loader now accepts
both and ignores other slots/unsupported ordinals. RTL tests cover both indices
with partial writes, ACK ownership, exact/over-capacity transfers and bad
addresses; the combined stalled-DDR test uses the PNG index
(`build/png-rtl-tests.log`, `build/png-rtl-integration-final.log`).

The rebuilt seed-7 RBF passed all 152 reported timing checks with minimum slack
0.012 ns; resource use is 8,500 ALMs (20%), 13,630 registers, 1,572,316 RAM bits
(28%), 203 RAM blocks and 31 DSP blocks. Full external-I/O/CDC qualification
remains open. The artifacts installed at this PNG milestone were:

- ARM service SHA-256: `25b92c9655e7b59f3d2b06aa4b75177acd7de57bb617da6e91f8493d68b86717`.
- RBF SHA-256: `22db3d9d9c56f3811ec06de8aab492fcaf673edb4c21c56c1e3462b5828720dd`.

All 14 native captures with the new ARM service match desktop reference pixels
(`build/png-live-native-languages.log`, `build/png-native-capture-parity.log`).
All 14 modern PNG cartridges then passed actual native MGL/F0 loading with the
corrected RBF; their live captures also match desktop pixels exactly
(`build/png-live-languages.log`, `build/png-capture-parity.log`). The demos are
installed, with verified hashes, under `games/TIC-80/Carts/PNG_Demos/`.
The 14 legacy PNGs also passed native transfers and exact live capture parity
(`build/png-live-legacy-languages.log`, `build/png-legacy-capture-parity.log`).
`tools/test_live_png_recovery.py` verifies corrupt-image and oversized-output
rejection through actual PNG transfers, with the same supervisor process and
subsequent cached-game autosaves. Loading the native version afterward retains
the PNG game's save identity/counter. A playable PNG whose native payload is
exactly 4,194,304 bytes also loads and saves. Evidence is in
`build/live-png-recovery.json` and `build/png-live-recovery.log`. MGL reloads the
FPGA during these checks, so cached-game BOOTs are expected; the host fixture
separately verifies rejection without resetting the same running VM.
With the final RBF, native and PNG clock fixtures both requested exit and
flushed valid saves after about 5,015 ms and 301 ticks. BOOT timestamps were
1 ms and the largest observed gaps were 31 ms (native) and 29 ms (PNG)
(`build/png-live-clock-native.log`, `build/png-live-clock.log`). These remain
short regression checks rather than long-session drift measurements.
The image boundary was also exercised with a valid 4096x1024 cover through
decode and a supervised first tick on desktop and ARM. A 4096x1025 cover is
rejected. Another 2,000 sanitizer mutations of the maximum cover passed without
errors (`build/png-boundary-tests.log`, `build/png-board-boundary-tests.log`,
`build/png-asan-max-image.log`), bringing this revision's mutation count to
62,000 across both runs.
Finally, switching to MENU stopped both supervisor and worker; the installed
Forth PNG demo then loaded through a fresh Frontier-launched service. The board
is left running that demo with a clean log and one worker
(`build/png-final-board-state.log`).

## Audio statistics and measured soak baseline

The optional TIC2 statistics extension adds a sequence/magic at byte 208 and
48 kHz output-slot/underrun counters at byte 216, using previously reserved
space. Registered Gray counters cross through two stages to the DDR clock.
The DDR publisher snapshots them between odd/even sequence writes and leaves
an unchanged snapshot alone. Empty startup before the first PCM pair is
excluded; every empty output slot after playback begins counts as an underrun.
Tests exercise coherent snapshots under stalls, counter identities/rate,
session reset, and unchanged snapshots.

`tic80-runtime-monitor` observes the live session without owning it or changing
control/payload words. Host fixtures exercise torn snapshots, unsupported
statistics, session changes and bounded failures. The ARM monitor sampled the
same registers on the board. A controlled 350 ms supervisor pause produced
17,645 underrun slots (367.6 ms including refill after resumption), while the
session remained intact and playback resumed. This negative control confirms
that the counters detect a real starvation interval rather than merely counting
submitted PCM (`build/stats-underrun-proof-baseline.json`,
`build/stats-underrun-proof-baseline.jsonl`, `build/stats-underrun-proof.log`).

The diagnostics RBF passed all 152 timing checks with minimum slack 0.073 ns,
8,608 ALMs (21%) and 14,143 registers. Its SHA-256 was
`1444fa9b099b9237034de536ab92e8142c10db2f752a4d5f3494644954aa66d9`;
the ARM service retained the PNG milestone hash. Post-fit `report_path` checks
found one routed path per bit in the read/write FIFO pointers, played-sample,
slot and underrun Gray buses at four timing corners. Every maximum delay was
below 90% of a DDR-clock period (`build/audio-cdc-*.rpt`). The Windows build
script now requires this check. It is physical propagation coverage for these
five buses, not full CDC/MTBF or external I/O qualification.

A fresh-saveid native music cartridge completed a 600-second measurement with
3,001 monitor samples, 35,999 adopted game frames, 36,058 carrier frames,
47,999.238 measured audio slots/second and valid CRC-checked saves throughout.
The final save held 36,180 ticks, 603,006 ms elapsed, one BOOT and a largest
tick gap of 37 ms. **The audio acceptance check failed:** 561 underrun slots
already existed at the warm baseline, then 53 more occurred about 1.8 seconds
into the measurement. There were no further gaps. Source evidence is preserved
in `build/stats-soak-baseline.json` and `.jsonl`, with the log in
`build/stats-soak.log`. This establishes a reproducible startup/buffering issue;
it does not establish audio completeness. The run flushed its final save and
returned the board to MENU.

The buffer correction holds the first PCM until at least 1,600 sample frames
(two TIC ticks) have been published. A registered monotonic ready bit crosses
through two audio-clock stages; it does not sample a binary multi-bit count
across clocks. A shorter stream starts after 2,400 empty startup output slots
(50 ms) counted from FIFO availability, so a one-tick CLI can still drain.
No PCM samples are inserted, dropped or reordered. A session reset clears
both the ready flag and timeout. Simulations verify a delayed second/third
tick without starvation, full FIFO behavior, exact stereo order, finite
two-sample/800-frame clips and resetting a half-prefilled session.

All 24 desktop checks passed in 96.53 seconds, including combined stalled-DDR
audio/video/cart/input simulation (`build/audio-prefill-full-tests.log`). A
subsequent monitor fixture verifies stale-heartbeat failure as well
(`build/audio-monitor-heartbeat-test.log`). The corrected RBF passed 152 timing
checks with minimum slack 0.113 ns, 8,699 ALMs (21%), 14,063 registers,
1,572,316 RAM bits, 203 RAM blocks and 31 DSP blocks. All 20 post-fit audio
Gray-bus/corner checks passed; the greatest routed delay was 2.393 ns, below
the 8.5707 ns bound (`build/audio-cdc-audit.log`, `build/audio-cdc-*.rpt`).

Installed corrected RBF SHA-256:
`ba3378cb9a8b7d2d4148c1cf32d791e97b20868e4c296b3ea9d12c59df0038b1`.
The service retains SHA-256
`25b92c9655e7b59f3d2b06aa4b75177acd7de57bb617da6e91f8493d68b86717`.
The real FPGA drained 800, 1,600 and 2,400 sample-frame clips in 0.074, 0.053
and 0.070 seconds respectively (`build/audio-prefill-short-clips.log`,
`build/short-audio-hardware.json`). For this CLI test, the harness temporarily
moves only TIC-80's own launch hook aside to prevent Frontier's automatic
respawn, then restores the unchanged file. The shared daemon is untouched.
A fresh Frontier service subsequently loaded the Forth PNG demo and its
monitor reported zero underruns (`build/audio-prefill-fresh-demo.log`,
`build/audio-prefill-first-monitor.log`). The revised controlled-pause harness
again detected starvation and recovery: 17,714 slots/369.04 ms
(`build/audio-prefill-underrun-proof.log`, `build/stats-underrun-proof.json`).
The corrected build subsequently completed its 600-second soak with 3,001
samples, **zero startup and playback underruns**, 28,799,544 consumed stereo
sample frames, exactly 36,000 adopted game frames and 36,058 carrier frames.
The measured audio rate was 47,999.234 slots/second (about -16 ppm).
CRC-checked saves remained valid throughout. The final save contained 36,180
ticks, 602,997 ms elapsed, one BOOT and a largest tick gap of 29 ms. All harness
acceptance checks passed (`build/audio-prefill-soak.log`,
`build/audio-prefill-soak-result.json`, `build/audio-prefill-soak-samples.jsonl`).
This closes the reproduced buffering regression and the ten-minute music
soak; longer and heavier cartridge/storage stress remains a separate gate.

The post-fit audit also checks the six registered control paths: frame-ready
toggle, bundled bank, source-valid, vblank toggle, consumed toggle and monotonic
audio-prefill ready. All 24 control/corner checks passed for the installed RBF;
the greatest delay was 3.481 ns, below the same 8.5707 ns bound. Source/first
stage register counts and physical data paths are explicit; a clock-to-register
path cannot satisfy the check (`build/control-cdc-*.rpt`). This expands the
build gate to 44 routed crossing/corner checks. Reset release, dual-clock RAM
ownership and the inherited platform still require broader CDC review.

During the corrected music soak, the user confirmed that the picture looked
correct and audio was clear and continuous from both channels, then identified
the connection as HDMI. This completes the basic connected-HDMI picture and
listener checks for this run. Physical input, supported analog modes and
measured latency remain unverified.
After the run, a fresh Frontier service loaded the installed Forth PNG demo.
One supervisor and one worker were present; its log was clean and the final
monitor still showed zero underruns. The original launch hook was executable
and no diagnostic rename remained (`build/audio-prefill-final-demo.log`,
`build/audio-prefill-final-board-state.log`).

## TIC3 full-color transport

The full-color revision removes RGB565 quantization. ARM row packing preserves
the upstream RGBA8888 image exactly; the FPGA ignores alpha and emits all eight
bits of RGB. Frames are 147,456 bytes and each 64-bit DDR word holds two pixels.
DMA and BRAM word addresses expand to 15 bits so the last rows above word
16,383 remain addressable. The whole-frame acknowledgment and blanking-only
bank adoption remain in place.

The incompatible payload layout uses magic TIC3 (`0x33434954`). Buffer zero
starts at `0x100`, buffer one at `0x24100`, audio at `0x48200` and cartridge
staging at `0x50000`; the reserved region is `0x450000` bytes, with a 4 MiB
cartridge capacity. Generated constants validate sizes, alignment and pairwise
non-overlap. `video.h` uses that same definition rather than duplicating sizes.
Old and new players/RBFs must be installed as matching pairs.

All 24 desktop checks passed in 100.68 seconds (`build/rgba-host-tests.log`).
New conversion coverage preserves every byte at all 256 channel levels and
across padded strides. Scanout checks two-pixel ordering and exact 8-bit colors
in both banks, including varying ignored alpha. The combined simulation
exercises the expanded frame DMA under stalled DDR alongside audio/cart/input.
The real Lua raster/overlay/border fixture also checks that the transported
frame exactly equals the runtime's RGBA output. The separate version guard
checks rejection of a moving, geometrically compatible TIC2 fixture without
changing ARM-owned control words (`build/rgba-version-guard-test.log`).
Conversion, real runtime and file-backed transport tests passed on the actual
ARM board. All 14 runtimes' final RGBA frames and 600-tick PCM match desktop
bytes exactly (`build/rgba-board-tests.log`, `build/rgba-offline-parity.json`).
The new live player also rejected the installed TIC2 core while its old
supervisor/session/playback continued (`build/rgba-hardware-protocol-guard.json`).

The first seed-7 placement failed hold at the DDR-to-boot-session input and
setup in the inherited HDMI scaler; it was not installed. Seed 8 passes all
152 checks with minimum slack 0.116 ns. Hold optimization and multi-corner
optimization remain enabled; no timing requirements were relaxed. Resource
use is 8,707 ALMs (21%), 14,202 registers, 2,162,164 RAM bits (38%), 273 RAM
blocks (49%) and 31 DSP blocks. The RBF also passes all 44 routed crossing
checks at the actual -40/100 C slow/fast report corners. The audit recognizes
Fitter-duplicated equivalent source registers while still requiring exactly
one physical data path into each single control first-stage register.
Earlier audio audits used 85 C for their warm routing probe; the current
probe now matches the 100 C corner in the full timing summary.

Service SHA-256 installed for this initial TIC3 milestone:
`4cd6ea07107420acfb941c571462d34931a063b42e46e1dd6b60d98c798b3d2f`.
RBF SHA-256 installed for this TIC3 milestone:
`645633d2cebc566c61848939dfbbd2ee5705af365d0ad086e6eeb233800e611c`.
Evidence: `build/rgba-qualified-timing.log`, `build/audio-cdc-audit.log`,
`build/rgba-installed.json`, `build/rgba-install.log` and corresponding reports.
The previous qualified TIC2 pair is preserved under
`build/tic2-prefill-baseline/`.

An all-levels raster fixture, loaded both natively and as a modern PNG, matches
every live 256x144 RGB byte with zero channel differences. Each live channel
contains all 256 levels. As a negative comparison, the former RGB565 mapping
would differ in 92,415 channel values for this image, so a quantized path cannot
silently satisfy this check (`build/color/result-native.json`,
`build/color/result-png.json`, `build/rgba-live-color-*.log`). The new pair also
loads the installed Forth PNG demo through a fresh Frontier service
(`build/rgba-first-demo.log`). All 14 native demos and all 14 modern PNG demos
also load and produce live captures with zero RGB channel differences from
desktop runtime output (`build/live-languages-rgba.json`,
`build/live-languages-rgba-png.json`, `build/language-capture-parity-rgba.json`,
`build/language-capture-parity-rgba-png.json`). On TIC3, corrupt-image and
oversize-native PNG transfers are
rejected while the cached game resumes and saves. A subsequent native load
retains that PNG's save; a PNG containing exactly 4,194,304 extracted native
bytes runs and saves (`build/rgba-png-recovery.json`,
`build/rgba-live-png-recovery.log`). An initial attempt found the board in MENU
and stopped before executing the test; the recorded passing run uses a fresh
service on the qualified pair.

The qualified TIC3 pair also passes a fresh ten-minute native music/clock/save
soak. Its 3,001 coherent samples cover 600.000386065 seconds in one session.
Both startup and steady-state underrun counts are zero; 28,799,550 stereo
sample frames played. Measured audio rate is 47,999.2191153 Hz (about -16 ppm).
The FPGA adopted 35,999 game frames and observed 36,058 carrier frames.
All periodic save checks pass CRC; the final save records 36,180 ticks,
603,002 ms, one BOOT and a maximum tick gap of 32 ms. The test leaves MENU
after flushing the final save. Evidence: `build/rgba-soak.log`,
`build/rgba-soak-result.json`, `build/rgba-soak-samples.jsonl`.

A fresh-save, human-operated diagnostic records all eight player-one controller
bits through the real Xbox/MiSTer/FPGA/runtime path. Each bit has a recorded
press, the held mask returns to zero, and all controls remain released for more
than two seconds. The user reports that the Xbox controls work correctly.
No input was injected. The attached device is identified by Linux as
`Microsoft Xbox Controller` (`build/physical-input/loaded.json`,
`build/physical-input/result.json`, `build/rgba-physical-input-inspect.log`).
This qualifies the basic connected controller path, not four simultaneous pads
or all controller models. A physical keyboard is unavailable.

The same diagnostic initially receives no mouse events, matching the user's
observation. Linux lists no mouse input device and repeatedly reports USB
descriptor failures (`device descriptor read/64, error -32`) on port 1-1.2
(`build/rgba-physical-mouse-discovery.log`). Reconnection on another USB port
resolved this initial detection failure: Linux now identifies a `PIXART USB
OPTICAL MOUSE` on port 1-1.3, and the user confirms that it responds. The same
diagnostic/save/service then records 262 X-coordinate changes, 144 Y-coordinate
changes, all three mouse buttons, 18 upward wheel steps and 20 downward steps.
Both held masks return to zero and remain released for 1,393 ticks. BOOT stays
at one. The full physical controller/mouse diagnostic passes without injected
events (`build/rgba-physical-mouse-reconnect.log`,
`build/rgba-physical-input-reconnect-inspect.log`,
`build/physical-input/result.json`). This qualifies the connected devices'
basic input path; it does not diagnose the original USB port or establish
compatibility with every input device.

After physical testing, the board returns to the installed Forth PNG demo
through a fresh Frontier service. The log is clean, the read-only monitor
reports zero underruns, and installed player/RBF/launch-hook hashes match the
qualified files (`build/rgba-final-demo.log`, `build/rgba-final-board-state.log`).
The original launch hook remains executable with no diagnostic rename left.

## Selected-core ownership and shutdown

A subsequent MENU-exit baseline with the initial TIC3 player logged
`FPGA frame acknowledgment timed out` and no completed service-exit line
(`build/lifecycle-baseline-exit.log`). The installed Frontier daemon sends
SIGTERM and allows one second before SIGKILL. The previous service could spend
that window waiting for an acknowledgement from an FPGA that had already been
replaced. Its shared daemon was inspected and remains unchanged.

Physical transport now checks `/tmp/CORENAME` before accepting the DDR
identity/session. Another explicitly named core makes transport operations
refuse access, including restart and close, even when valid TIC3 identity and
session words remain in DDR. A failed session handshake no longer clears
control words after selection has changed. Both the session request and ACK
must match the owner, and publication rechecks ownership after payload copies.
The service treats departure as a normal exit, joins its save worker and
flushes its last completed game state. Missing selection, lost transport while
TIC-80 remains selected and failed final saves still return an error.

Main rewrites CORENAME with `fopen("w")`, `fwrite` and `fclose` in the pinned
`reference/main/user_io.cpp`. Missing/empty selection is therefore retried for
at most 50 ms while withholding DDR operations. A transient rewrite preserves
the current VM, BOOT count and session. Malformed selection is rejected.
The explicit selection fixture option is accepted only with a regular-file
DDR fixture and cannot override the physical selection check.

All 25 desktop checks passed in 128.71 seconds, followed by all six affected
checks on the final revision in 50.10 seconds (`build/lifecycle-host-tests.log`,
`build/lifecycle-host-final-targeted.log`). Lifecycle cases cover SIGTERM,
MENU departure while idle and waiting for ACK, another core, transient and
missing names, lost identity, a real stalled transport, and failed final saves.
Transient selection retains one BOOT and the same worker. Normal departures
return zero with a valid final save; true fault cases return one. Final writes
are tested with unsaved state after an earlier autosave, and worker cleanup is
checked. Service color observations now sample actual RGB bytes at the RGBA
stride instead of the historical RGB565 offset.

Desktop and actual ARM transport tests freeze a moving fixture with valid
stale TIC3 data, select MENU, invoke payload/cart/input/restart/close operations
and compare the complete reserved region to prove no DDR word changes. They
also test departure during a pending handshake and a short empty-name rewrite
(`build/lifecycle-arm-native-tests.log`).

The service installed for the first lifecycle qualification is
`e56eb6c2f2f13f1cc2d2b666747c840564dbdfe5172b564263e93b707b3a2e2d`;
the read-only monitor is
`a9ecb0c8389bc88ccadcf36858b24226ba22188eade8a4d59a95a3f0cb03ced8`.
The qualified RBF remains `645633d2cebc566c61848939dfbbd2ee5705af365d0ad086e6eeb233800e611c`.
Installation and staged artifacts were hash-checked
(`build/lifecycle-installed.json`, `build/lifecycle-install.log`). The previous
fully qualified full-color ARM player/monitor are retained under
`build/tic3-color-baseline/`.

Four cold entries and 32 warm FPGA reloads completed with 36 exact BOOTs,
monotonic saved counters, replaced/reaped VM workers and clean MENU exits;
the longest observed exit was 0.399 seconds. Its fresh ten-minute music soak
also passed: 3,001 samples over 600.000147405 seconds, zero startup/steady
underrun slots, 47,999.218207792066 Hz audio clock and a valid final save
with 36,201 ticks and one BOOT (`build/lifecycle-soak-result.json`,
`build/lifecycle-soak-samples.jsonl`).

Inspection of the reload profiles nevertheless showed linear RSS growth:
about 240 KiB per reload in each supervisor. The old 2 MiB allowance across
eight reloads accepted this leak. That revision's artifacts and profiles are
retained under `build/lifecycle-leaky-baseline/`; its functional checks do
not qualify memory stability.

## Runtime allocation and initialization repairs

LeakSanitizer confirmed that the pinned `tic_core_close` omits the 96 KiB RAM
allocation made by `tic_core_create` (`build/runtime-ram-leak-baseline.log`).
The integration closes the VM first, then releases the owned `base_ram`.
This ordering preserves WASM's copy-back from its redirected module RAM.
Failed initialization also used to discard a non-null VM without closing it,
leaking failed Wren and Ruby candidates. Build staging now uses normal VM
cleanup before discarding that pointer. The wasm3 module adapter frees owned
memory/table export names and memory import names omitted by its destructor.

The new live-heap regression warms allocator bins, then closes 192 direct
cores, unused public players and initialized Lua players. Bypassing only the
RAM cleanup produces 18,877,440 bytes of growth and fails the test
(`build/runtime-memory-negative.log`). The normal desktop run grows by only
32 bytes (`build/runtime-memory-host.log`). An initial ARM run also passed
with 80 bytes of growth (`build/runtime-cleanup-arm-native-tests.log`).

Full interpreter instrumentation additionally found a Python initialization
use-after-free: creating easing aliases rehashes the dictionary while its
source value still points into the old table
(`build/runtime-python-alias-asan-failure.log`). The staged binding now copies
that value before insertion. The language test checks all 31 easing aliases'
identity and calls, along with the existing demo and peer isolation checks.
Full interpreter instrumentation then exposed four unused s7 permanent string
allocations (192 bytes total). They are allocated once, have no uses beyond
declaration/initialization, and are omitted in build staging with the original
source/license retained (`build/runtime-scheme-permanent-asan-failure.log`).

The final all-language ASan/LeakSanitizer run passes without invalid accesses
or reported leaks (`build/runtime-asan-extended-result.log`). All 26 desktop
checks pass in 123.72 seconds (`build/runtime-cleanup-host-tests.log`). The
actual ARM allocation regression, transport, Lua/audio and all-language demo/
peer checks pass. Failed candidates are checked for Python, Janet, Wren, Ruby
and Forth. All 28 ARM RGBA/PCM outputs across 600 ticks per
language exactly match the qualified desktop baseline
(`build/runtime-cleanup-arm-native-tests.log`, `build/cleanup-arm-parity/result.json`).
These checks cover the exercised paths; they do not establish universal
cartridge/API or allocation-failure compatibility.

The installed service is now
`69395f8ceb4ad5a4dfa288b8b149bf22ae211162ab011037e1947eea1854dea1`;
the RBF and monitor retain the hashes above
(`build/runtime-cleanup-installed.json`, `build/runtime-cleanup-install.log`).
The stronger hardware reload gate allows initial allocator warmup, then
requires the supervisor's RSS to plateau within 128 KiB. Four fresh supervisors
each completed 16 warm FPGA reloads: all 64 reload snapshots were exactly
3,912 KiB, with five FDs and occasional transient sixth FDs. Each reload kept
its supervisor, replaced its worker, advanced BOOT exactly once and retained
the saved tick counter. Final BOOT was 68 and the valid final save contained
8,629 ticks. Every MENU exit completed normally within 0.251 seconds;
no VM worker remained. The shared Frontier daemon and own launch hook hashes
were unchanged (`build/runtime-cleanup-lifecycle-result.json`,
`build/runtime-cleanup-hardware-lifecycle.log`).
All 14 native and all 14 modern PNG live captures on this installed player
match desktop RGB output exactly: zero mismatching channels in every language
(`build/live-languages-cleanup.json`, `build/live-languages-cleanup-png.json`,
`build/language-capture-parity-cleanup.json`,
`build/language-capture-parity-cleanup-png.json`).

The final player passed a fresh 600.000236103-second music/clock/save soak:
3,001 coherent samples, zero startup/steady underrun slots, 47,999.234445394584 Hz
audio clock, 28,799,552 played stereo frames and 35,999 adopted game frames.
The valid final save contained 36,279 ticks, 604,652 ms elapsed, one BOOT and
a largest tick gap of 32 ms. Across all 21 process profiles, supervisor RSS
was exactly 3,948 KiB and worker RSS exactly 4,804 KiB. The same supervisor
and worker persisted throughout; FDs were five with transient sixths in the
supervisor, and always four in the worker
(`build/runtime-cleanup-soak-result.json`,
`build/runtime-cleanup-soak-samples.jsonl`,
`build/runtime-cleanup-soak-memory.json`).
After the run, the installed Forth PNG demo loads through a fresh service and
VM worker. The log is clean, a read-only monitor reports zero underruns with
advancing playback, and player/RBF/own-hook/shared-daemon hashes match the
qualified artifacts. The own launch hook remains executable, with no diagnostic
rename left (`build/runtime-cleanup-final-demo.log`,
`build/runtime-cleanup-final-board-state.log`). An initial final-read SSH timeout
was followed by successful connection and verification; that failed observation
is retained in `build/runtime-cleanup-final-network-timeout.log`.

Queue analysis of the previous lifecycle soak reveals a separate timing issue:
`serve.c` schedules game ticks from Linux's monotonic clock while the FPGA
consumes 47,999.2182 stereo frames per second. The coherent recording's queue
grows at a fitted 0.781343 frames/second, adding 9.76695 ms of buffered audio
over ten minutes (`build/lifecycle-audio-queue.json`, tied to the sample journal's
SHA-256). The measured growth agrees with the two clocks' rate difference.
Long-run audio/video latency and clock alignment therefore remain open, even
with zero measured underruns. `tools/analyze_audio_queue.py` preserves this
observation separately from the soak's playback acceptance checks.
The repaired player's fresh recording also shows positive fitted queue growth:
0.757827 frames/second, or 9.47300 ms over ten minutes
(`build/runtime-cleanup-audio-queue.json`). Memory cleanup has passed its
qualification; the separate clock-alignment issue still needs a repair and
longer-run verification.

## Playback-driven pacing revision

`src/pacer.c` now spaces startup ticks at 60 Hz until playback begins, then
uses the FPGA's consumed-sample counter to generate a new tick when 800 stereo
frames remain. The playback latch survives counter wrap; a new session resets
it. The backend checks selected-core/session ownership while waiting and fails
a stopped DAC within one second. Signal cancellation interrupts the wait, and
the service saves its last completed worker product without draining audio or
restarting an interrupted VM on shutdown. Video-only CLI patterns keep their
wall-clock pacing.

The independent virtual DAC test runs six hours each at 47,976, 48,000 and
48,024 Hz, with render jitter, sleep overshoot and occasional scheduler stalls.
It preserves every PCM frame and reports zero underruns; ready-to-render queue
depth remains 766..800 frames. A counter-wrap run also passes. Fixed-clock
negative controls expose accumulating queue depth or underruns at the same
clock differences. The new C policy also passes ASan/UBSan/LeakSanitizer
(`build/audio-pacing-sanitized.log`). These are accelerated simulations, not
six-hour hardware recordings.

Service fixtures now model a 48 kHz DAC with two-tick priming and the finite-clip
fallback instead of instantly draining PCM. Register fixtures use aligned
32-bit stores and retain the statistics magic throughout sequence publication.
The old pacing revision fails a deliberate stopped-clock SIGTERM gate in
1.067 seconds (`build/audio-pacing-stop-negative.log`). The revised focused
check exits in 32 ms with a CRC-valid save and no remaining child, and four
additional probes exit promptly (`build/audio-pacing-lifecycle-targeted.log`,
`build/audio-pacing-stop-probe.log`). A cancellation-enabled full-suite run
still observed a 0.9155-second clean exit; its cause is not established and its
record is retained (`build/audio-pacing-cancel-final-save-delay.log`). The final
27-check desktop run at the reset revision passes all gates in 120.49 seconds,
retaining the 750 ms good-exit limit
(`build/audio-pacing-before-pmem-host-tests.log`). Longer shutdown-tail
qualification remains open.

The final ARM pacing/transport checks pass, including cancellation, stopped
clocks and core departure without DDR writes
(`build/audio-pacing-arm-final-tests.log`). Before the cancellation change, the
same runtime sources passed all-language ARM tests and exact RGBA/PCM parity
across 600 ticks per language (`build/audio-pacing-arm-native-tests.log`,
`build/audio-pacing-arm-parity/result.json`).
The installed player is now
`c19217cf496cd6266de2c4462f2bec06670cfc2af3ae186ea6135bdd8161f047`;
the monitor is
`0ada2983093ef6f3b04cde52ceb04cef591dad097ac30ef2c1d109fcbf328199`.
The RBF remains `645633d2cebc566c61848939dfbbd2ee5705af365d0ad086e6eeb233800e611c`
(`build/audio-pacing-installed.json`). Hardware qualification of this revision
is in progress.

The first reload run stopped on a single seven-descriptor snapshot after eight
reloads. The supervisor subsequently had its usual five descriptors and the
same 3,912 KiB RSS (`build/audio-pacing-lifecycle-transient-fd-stop.log`,
`build/audio-pacing-lifecycle-transient-fd-progress.json`,
`build/audio-pacing-lifecycle-fd-inspect.log`). The observer now records three
descriptor samples and their peak, and requires an excessive count to return
within the original baseline-plus-one limit within 500 ms. Persistent growth
still fails; transient and persistent negative controls exercise this same
observer (`build/audio-pacing-fd-observer-controls.log`). This distinguishes
temporary open handles from retained descriptors without increasing the
settled-count limit. The memory plateau and clean-exit gates are unchanged.

A subsequent run passed its first 16 warm reloads, then stopped when Frontier
terminated the second supervisor during a later same-core reload. The runtime
had exited cleanly with error zero. Frontier's log explicitly records
`'TIC-80' -> ''` as the cause of SIGTERM
(`build/audio-pacing-lifecycle-launcher-failure.log`,
`build/audio-pacing-lifecycle-launcher-failure-progress.json`,
`build/audio-pacing-lifecycle-respawn-previous.log`,
`build/audio-pacing-lifecycle-launcher-cause.log`).
`tools/patch_frontier.py` creates a hash-guarded compatibility patch: only an
empty selection after TIC-80 receives a bounded 50 ms re-read. Named departures
and other cores retain their dispatch behavior. Shell fixtures exercise
transient and persistent name loss, MENU departures, unchanged other-core
reads, and hot-restart preservation; the installed shell also passed syntax
validation (`build/audio-pacing-frontier-tests.log`). The controlled restart
skips startup sweeps and boot-time mount cleanup. Every `.s0` file and
`user-startup.sh` hash was preserved. The original daemon remains in a verified
rollback copy. Installed daemon SHA-256:
`6c89bdd8c2754c9b10c85fe0b086e395b7e74c9a3bad0e18bc2636d72f6321ad`
(`build/audio-pacing-frontier-installed.json`). Hardware checks are being
repeated against this exact launcher revision.

The first retry against the patched daemon stopped on an SSH channel-read
timeout, while the same supervisor and its new worker continued ticking with a
clean log (`build/audio-pacing-lifecycle-ssh-observation-timeout.log`,
`build/audio-pacing-lifecycle-ssh-observation-progress.json`,
`build/audio-pacing-network-stop-board.log`). Observer commands now drain both
channels with a bounded 45-second deadline and a transport keepalive. Each
command starts once; no mutation is repeated on an observation timeout. The
shared helper passes delayed-output, stderr-failure, timeout and single-start
checks (`build/audio-pacing-ssh-tests.log`). Network observation failures remain
distinct from core/runtime failures.

A subsequent observer run returned before stdout EOF because SSH exit status
arrived first. That produced an impossible zero-descriptor sample. Its apparent
64-reload success is invalid and must not be used as qualification
(`build/audio-pacing-lifecycle-invalid-fd-result.json`,
`build/audio-pacing-lifecycle-incomplete-output.log`). The helper now requires
EOF or channel closure as well as exit status and drained buffers. Its regression
delivers exit status before delayed data. Descriptor observations below three
are rejected; transient handles must still settle within the unchanged limit
(`build/audio-pacing-fd-observer-final-controls.log`).

With complete observations, the next hardware run completed 16 warm reloads
but saved BOOT=18 on MENU departure where BOOT=17 was expected
(`build/audio-pacing-menu-extra-boot-failure.log`). Main asserts reset while
replacing a core. The revised service holds the game until reset release,
displaying a reset screen without ticking the cartridge or scheduling its
autosave. Release restarts the cached cartridge once; departure saves its last
completed state. The old player fails the held-reset departure regression with
BOOT=2 (`build/audio-pacing-reset-release-negative.log`); the revised player
passes that regression in the 27-check run. A separate real-service regression
also verifies that a cartridge transferred while reset is held remains pending
and loads once on release, with CRC-valid old/new saves
(`build/audio-pacing-held-transfer-tests.log`).
The held-transfer and two SSH/descriptor observer regressions are now registered
with CTest; all three targeted additions pass in 4.54 seconds
(`build/audio-pacing-additional-tests.log`). The later storage revision passes
the complete suite of 31 registered checks, including these additions.

Main's command line can also point to MENU before CORENAME changes. Frontier
misclassified that interval as a same-console RBF swap and restarted TIC-80's
handler. The pending compatibility patch lets TIC-80's own supervisor handle
same-console reloads and keeps normal named departures and other-core RBF swaps.
Shell fixtures cover this ordering (`build/audio-pacing-frontier-tests.log`).
The installed player SHA-256 was
`91a895ffd888f1802ca5e8aec0bfdc862557820f63180498be153bcbea3e966d`;
the installed daemon SHA-256 is
`931f3afd0adfc7bdcd8aaba43de91d894d90bdec23a3b1632c769b6026608f0a`.
An earlier install stopped before mutation when a different game was selected;
the later authorized installation succeeded (`build/audio-pacing-installed.json`,
`build/audio-pacing-frontier-installed.json`). The latest revision has not yet passed
hardware reload, full language-capture or ten-minute pacing qualification.

The new soak samples every 197 ms to cover tick phases, and requires at most
2 ms of fitted queue change over a run of ten minutes or longer. A synthetic
constant-envelope recording produces 9.74 ms of false fitted growth at 200 ms
but only 0.0144 ms at 197 ms. Adding real queue growth still fails the new gate
(`build/audio-pacing-sampling-proof.log`). The queue statistic is separate from
underrun and game-cadence checks.

## Persistent-memory fault qualification

The save writer previously reported success if it could not open the parent
directory after renaming a completed save. Injecting that failure reproduces
the incorrect success return (`build/pmem-fault-before-fix.log`). It now requires
the directory to open, reports directory sync/close failures, and advances its
saved snapshot only after the supported flush steps succeed. The existing
compatibility treatment of unsupported directory fsync remains unchanged.

`tests/pmem_fault_test.c` links wrappers around the production writer and uses
real temporary files. It covers EINTR, short and zero-byte writes, partial-write
failure, temporary-file creation, file flush/close, rename, and directory
open/flush/close failures. Pre-rename failures preserve the committed old save.
Post-rename errors return failure while leaving a complete new file; retry
then succeeds. Restoring both forms verifies the file bounds, version and CRC.
Expected fault diagnostics are retained in
`build/pmem-fault-fixed-targeted-details.log`.

A deliberately blocked file fsync leaves the caller free to queue 100 newer
snapshots in less than 100 ms. The worker commits only the original snapshot
and the newest replacement; final save joins it and preserves that replacement.
Background errors propagate to the caller, and a final retry after removing
the fault succeeds. SIGKILL at observed boundaries immediately before and
after rename leaves the old or new complete save, respectively. The pre-rename
kill can leave a temporary file; the fixture records that fact and removes only
its own temporary files. This checks process death on a live filesystem, not
physical card removal or power loss.

The full desktop suite passes all 31 checks in 133.95 seconds
(`build/pmem-recovery-baseline-host-tests.log`,
`build/pmem-durability-host-test-details.log`). The focused persistent-memory
tests also pass AddressSanitizer and LeakSanitizer
(`build/pmem-durability-sanitizer-tests.log`). The first ARM fault-fixture run
missed the libc `mkstemp64`/`open64` calls and failed its short-write assertion;
that is retained in `build/pmem-durability-arm-fixture-failure.log`. The fixture
now wraps both ordinary and 64-bit file entry points. Its updated desktop and
sanitizer checks pass (`build/pmem-fault-largefile-tests.log`), and the corrected
fault fixture passes natively on the MiSTer's ARM processor
(`build/pmem-durability-arm-tests.log`,
`build/pmem-durability-arm-binaries.json`). Previously passed pacing, transport
and ordinary save tests are retained by exact unchanged binary hash; their
source journal is identified in `build/pmem-durability-arm-resume-evidence.json`.
These ARM tests use temporary files and do not select the TIC-80 FPGA core.
This earlier player was `0144de83d8abd0045b3ebf5e5aca4efa00c01bae6fda859964969891f76c7d56`.
This does not establish SD-card
power-loss durability or live-core qualification.

## Transient autosave recovery

The background worker previously latched any write failure permanently and
refused to queue later snapshots. A one-shot fsync failure therefore stopped
autosaves even after storage recovered. The low-level negative control times
out trying to recover an unchanged snapshot (`build/pmem-recovery-before-fix.log`).
A real-service negative control keeps the cartridge running but cannot obtain
later autosaves within nine seconds despite only one injected failure
(`build/pmem-service-recovery-before-fix.log`). Its first fixture attempt failed
to preload a library from the workspace path containing spaces; the corrected
fixture copies its library to its own temporary directory and rejects loader
errors (`build/pmem-service-recovery-preload-fixture-failure.log`).

The worker now records the result of its most recent completed write. An
autosave call still reports an outstanding error, but also queues the newest
snapshot for a background retry, including unchanged data that was not saved.
A successful retry clears the error. Persistent failures remain visible and
do not cause an automatic retry loop; retry happens when the caller schedules
another autosave. The service can retain its warning pause and resume saving
without replacing the cartridge VM or incrementing BOOT.

The low-level tests exercise one-shot recovery, unchanged-snapshot retries,
and persistent errors. They pass AddressSanitizer and LeakSanitizer
(`build/pmem-recovery-sanitizer-tests.log`). The new service regression requires
the same VM PID and BOOT=1 throughout, observes the warning pause, reads later
CRC-valid autosaves while the service is running, and checks the final flush
and worker cleanup. The complete desktop suite passes all 32 checks in 145.85
seconds; the service-recovery gate passes in 5.25 seconds
(`build/pmem-recovery-host-tests.log`, `build/pmem-recovery-host-details.log`).
Native ARM pacing, transport, ordinary save and fault/recovery tests also pass
(`build/pmem-recovery-arm-tests.log`, `build/pmem-recovery-arm-binaries.json`).
The installed recovery player was `91a895ffd888f1802ca5e8aec0bfdc862557820f63180498be153bcbea3e966d`.
These checks do not replace the pending live-core reload/capture/soak run.

## Main initialization ordering

The first live run of the recovery player passed 16 reloads and a clean MENU
exit. In its second cycle, the observer caught two candidate workers; the
settled save then contained BOOT=35 instead of 34 under the same supervisor.
This is a real extra BOOT, not just a transient process-observation failure.
Evidence is preserved in `build/audio-pacing-lifecycle-two-workers-failure.log`,
its progress, board and save snapshots, and daemon log.

A controlled service fixture reproduces an early cached-cartridge restart
when a fresh FPGA session arrives before Main's later initialization reset:
the old player produces BOOT=3 rather than 2
(`build/reset-initialization-before-fix.log`). The pinned Main source rewrites
CORENAME after releasing that reset. The candidate waits for an observed reset
release or a new CORENAME generation before restarting a cached cartridge.
Nanosecond mtime/ctime and file identity detect a same-content rewrite; reading
the generation also validates the named selection without accessing DDR.
Departure during that wait stops cleanly and flushes the last completed tick.

A diagnostic build of the previous player (`9ff79a24595611028cf6bcbe15be2cd14affaec76a1a414dfb9165271f77e4be`)
passed four cycles of 16 hardware reloads, BOOT=68, and clean MENU exits in
0.196–0.440 seconds (`build/reset-trace-lifecycle-result.json`,
`build/reset-trace-lifecycle.log`, `build/reset-trace-cycles`). The traces show
the marker rewrite after reset release. They did not reproduce the intermittent
extra BOOT; instrumentation can change scheduling, so this is diagnostic
evidence, not qualification of the readiness fix.

The readiness candidate passes all 33 desktop checks in 143.87 seconds,
including delayed reset, release without a marker rewrite, a marker rewrite
with a missed reset pulse, and MENU departure during the wait
(`build/reset-initialization-host-tests.log`). The first departure fixture
incorrectly compared the final tick with an older asynchronous autosave;
the corrected fixture compares it with the last traced completed cartridge
tick (`build/reset-initialization-departure-fixture-failure.log`). Native ARM
checks detect same-content CORENAME rewrites and reject a different selection,
and retain the pacing/save/fault checks (`build/reset-initialization-arm-tests.log`).
The player is `07f5e4807d9dd863a7ac8f005cd969ac5cf0482e389bd6b5418fc4c742b51591`,
the monitor is `53cbba3b2b27fe7e5a7943c2cabe0ff5b0349d7e18fb9d4aa5a2599a0de0df08`,
and debug reset tracing is disabled. Both are installed with the same TIC3 RBF
(`build/reset-initialization-installed.json`, `build/reset-initialization-install.log`).
Source and installed binary hashes are recorded in
`build/reset-initialization-provenance.json`. That player passed 64 reloads,
BOOT=68, flat 3,980 KiB supervisor RSS and MENU exits in 0.185–0.203 seconds
(`build/reset-initialization-first-lifecycle-result.json`,
`build/reset-initialization-first-hardware-lifecycle.log`). Its native language
run passed six demos before Squirrel reported six prior underrun slots; all
155 samples over the subsequent 2.002-second observation retained that count
(`build/audio-reserve-squirrel-underrun.json`,
`build/reset-initialization-first-native-underrun.log`). The run stopped there;
PNG parity and the new ten-minute soak were not performed for that player.

Review also exposed a second initialization case: reset held in the old FPGA
must not count as a release in the replacement FPGA. The controlled negative
produces BOOT=3 with the first readiness candidate
(`build/reset-prior-session-negative.log`). Session recovery now refreshes reset
history from the new session. The expanded initialization fixture passes all
five cases (`build/reset-session-history-targeted.log`).

The audio reserve is now 1,600 sample frames (two TIC ticks), adding 16.7 ms
over the previous reserve. Playback still controls pacing, so the reserve does
not accumulate clock drift. An independent DAC model with up to 4 ms of render
time and occasional 18 ms scheduling delays underruns with the old reserve
(`build/audio-reserve-before-fix.log`). The new reserve survives six virtual
hours at 47,976/48,000/48,024 Hz without underruns or lost PCM; the post-pacing
queue is 1,323–1,600 frames and steady ticks retain at least 1,551 frames.
Counter-wrap and fixed-clock negative controls also pass.

The first full desktop run passed 31 of 33 checks; the two failures were fixture
assumptions: a 1 ms inode timestamp update and the previous queue lower bound
after a longer scheduling pause. Corrected pacing/transport fixtures pass,
including ASan/LeakSanitizer (`build/audio-reserve-first-host-tests.log`,
`build/audio-reserve-focused-tests.log`, `build/audio-reserve-sanitizer-tests.log`).
The remaining 31 checks use the same unchanged candidate runtime code from
that full run. Native ARM pacing/transport/save/fault checks pass
(`build/audio-reserve-arm-tests.log`, `build/audio-reserve-arm-binaries.json`).
The player `74728bc8023b85026b6c0841c4c6a0186bf078c1b9a60ef8c6c677156486221e`
and monitor `9c7f6723816cf59aa854bad946fd5e9ae2af34ede0aca3269683663b4e25720b`
are installed (`build/audio-reserve-installed.json`, `build/audio-reserve-install.log`).
This candidate passed 64 warm reloads across four supervisors with BOOT=68,
flat 3,980 KiB warm supervisor RSS, bounded descriptors and clean MENU exits
in 0.181–0.304 seconds (`build/audio-reserve-lifecycle-result.json`,
`build/audio-reserve-hardware-lifecycle.log`). All 14 native and 14 PNG demos
have exact live RGB parity and zero recorded underruns, with at least 155
monitor samples per demo (`build/audio-reserve-live-languages.json`,
`build/audio-reserve-live-languages-png.json`,
`build/audio-reserve-native-parity.log`, `build/audio-reserve-png-parity.log`).
The 800/1,600/2,400-frame finite clips drain every published stereo sample
(`build/audio-reserve-short-clips.json`). The user confirms correct HDMI picture
and clear, continuous stereo audio on this exact build during its music soak.
The fresh soak passes over 600.062 seconds with 3,047 samples, zero startup or
playback underruns, 28,802,484 played stereo sample frames and 36,002 adopted
game frames (59.997 Hz). Measured audio consumption is 47,999.169 Hz and fitted
queue change is -0.0616 ms. All CRC-checked saves remain valid, BOOT stays at 1,
and final MENU exit flushes tick 36,341. The largest game-tick gap is 30 ms.
Across all 21 process profiles, supervisor RSS stays at 3,840 KiB and worker RSS
at 4,868 KiB with one worker, bounded descriptors and no steady growth
(`build/audio-reserve-soak-result.json`, `build/audio-reserve-soak-samples.jsonl`,
`build/audio-reserve-soak-memory.json`, `build/audio-reserve-soak.log`).
The outer PowerShell wrapper returned status 1 solely from removing the already
cleared password environment variable a second time; the inner qualification
completed all stages, printed its successful result and copied all three final
soak artifacts. The final-demo wrapper uses conditional environment cleanup.

Tetris is left running through the normal launcher under one supervisor. Its
additional ten-second check records 771 samples, zero underruns and advancing
playback (`build/audio-reserve-final-demo.json`,
`build/audio-reserve-final-demo-hardware.log`,
`build/audio-reserve-final-demo-audio.jsonl`). The native carrier heartbeat
averages 60.097 Hz versus the adopted game's 59.997 Hz; precise motion cadence
and display latency remain qualification work alongside the broader gates below.

The fitted integer video PLL uses a 50 MHz reference, M=189, N=10 and C=44,
giving 21.4772727 MHz. `emu_body.svh` enables pixels every fourth clock;
`tic80_scanout.sv` wraps at x=340 and y=261. Its nominal carrier rate is
945,000,000 / (44 × 4 × 341 × 262) = 60.09848 Hz. This explains the observed
rate and is independent of the corrected audio queue pacing. The roughly
0.1 Hz excess offers one extra scan per ten seconds; visible repeat cadence
is an inference that still needs a motion test. The qualified RBF remains
unchanged. A timing correction must preserve active-image sampling, sync
geometry and frame-bank ownership, pass RTL and routed timing checks, and
receive fresh hardware qualification before installation.

The final read-only audit verifies the installed player/RBF/monitor and launch
hook, the patched Frontier daemon and its rollback copy, and unchanged startup
and every `.s0` configuration hash. The board remains on TIC-80 with Tetris
running (`build/audio-reserve-final-audit.json`,
`build/audio-reserve-final-audit.log`).

## Shared video/playback clock candidate

The candidate now clocks video from the existing 24.576 MHz playback PLL.
The pixel-enable ratio 44,671 / 204,800 gives 5.36052 MHz on average while
retaining the 341×262 raster and existing sync/active-image geometry. Each
frame takes exactly 409,600 clocks, or 800 audio sample periods. Pixel spacing
varies between four and five clocks; analog sampling quality therefore remains
an explicit hardware check. Clock enables are supported by the MiSTer interface;
its [fractional enable guidance](https://mister-devel.github.io/MkDocs_MiSTer/developer/snippets/#fractional-clock-enable-generator)
also cautions that their effect depends on the application.

Four focused checks pass in 37.10 seconds: an independent ten-second enable
audit verifies 600 exact frame periods and every pixel interval; scanout checks
RGB, blanking, sync and frame-bank transitions at fractional spacing; integration
checks stalled DDR with the shared video/audio clock; the analysis gate handles
counter wraps and rejects ±100 ppm clock skew and reset sessions. The new
analysis rejects the prior 600-second hardware samples, whose relative phase
spans 47,862 sample periods (`build/video-clock-local-tests.log`,
`build/video-clock-baseline-negative.log`). This is new evidence for the
candidate; it does not qualify its hardware behavior yet.

At the user's request, both remapper name lists now show **Z, X, A, S**, the
pinned TIC-80 keyboard equivalents of button IDs 4–7. The order follows
`src/input.c`; physical controller mapping and keyboard conversion are unchanged.
The first candidate compilation was deliberately stopped before completion to
include these names. Its partial log is preserved in
`build/video-clock-before-key-labels-quartus.log`. The replacement compilation
includes the updated lists. That seed-8 build misses HDMI scaler accumulator
setup timing by 0.247 ns at 100°C and 0.494 ns at −40°C; it was not installed
(`build/video-clock-seed8-timing-failure.log`,
`build/video-clock-seed8-timing.summary`,
`build/video-clock-seed8-cold-critical.rpt`). Seed 9 was compiled with
the same timing constraints and RTL. That run passes all 140 reported timing
checks with minimum slack 0.041 ns, all 44 routed crossing checks, and the
48-register shared-clock audit (`build/video-clock-build.log`,
`build/audio-cdc-audit.log`, `build/video-clock-audit.log`).

A post-fit audit on the seed-8 candidate verifies all 18 enable-generator bits,
10 raster-X bits, nine raster-Y bits and nine DAC divider bits use the identical
playback PLL clock (`build/video-clock-seed8-shared-clock.log`). The audit's
first version incorrectly assumed an exact nominal period and newer TimeQuest
query options. Quartus 17 derives 40.682 ns for its timing model while the fitted
PLL reports 24.576 MHz; the guard allows its <500 ppm approximation and verifies
each register's actual destination clock using timed paths. These audit fixes
do not relax any design timing constraint or qualify the failed bitstream.
The final build command now runs the same shared-clock audit after setup/hold
and routed crossing checks.

The candidate RBF
`c835cd5ec4fd8fe3d3543d3e248410bc2ead563db15744581783ee43ca3521a7`
was installed with the unchanged `74728bc8...` player and `9c7f6723...` monitor.
The preceding qualified RBF remains at
`/tmp/tic80-mister-dev/TIC80-audio-reserve.rbf` and locally under
`build/video-clock-baseline/`. Installed and source hashes are recorded in
`build/video-clock-installed.json` and `build/video-clock-provenance.json`.
This candidate passed 64 reloads across four supervisors and all 14 native
language loads with exact RGB capture parity. Seven PNG loads completed before
qualification stopped. These internal checks did not establish HDMI acceptance:
the user reported **"mode not supported"** on the connected TV. The remaining
tests were stopped and the previously verified `645633d2...` RBF restored,
with Tetris launched under parent 15203 (`build/video-clock-rollback.json`).
The player's and monitor's hashes are unchanged. The rollback RBF predates the
new Z/X/A/S remapper labels. The candidate is rejected for display qualification;
its evidence is retained in the `video-clock-*` files. The ten-minute soak and
live motion captures were not run on this candidate.

Read-only recovery inspection finds the configured default `video_mode=10`
and `vsync_adjust=0`; the pinned Main source defines mode 10 as 1366×768 at
60 Hz. The restored core's Linux framebuffer reports 1366×768 too
(`build/hdmi-recovery-inspection.json`). That framebuffer report does not prove
the actual HDMI signal or TV acceptance. Cached EDID readback identifies the
connected Samsung TV and contains checksum-valid base/CTA blocks advertising
720p/60 (VIC 4) and 1080p/60 (VIC 16). A TIC-80-only `[TIC-80]` override now sets
`video_mode=0` and retains `vsync_adjust=0`; the original INI is preserved at
`/media/fat/MiSTer.ini.tic80-before-hdmi-20260930` and the added section leaves
the original bytes intact. Main's framebuffer changes to 1280×720. The user
confirms **"Yes, picture is back"** on this output with the restored RBF
(`build/hdmi-recovery-mode-result.json`, `build/hdmi-recovery-edid.json`).
The identical shared-clock candidate was then retested at 720p/60. The user
again reported an unsupported-mode message and confirmed that the second
rollback restored the picture. It remains rejected. The first short monitor
was incorrectly started before the delayed MGL cartridge transfer and stopped
on the expected session change. A subsequent stable-session Tetris sample
contained 1,045 accumulated underrun slots; that sample fails acceptance
(`build/video-clock-720p-smoke.log`, `build/video-clock-720p-smoke-audio.jsonl`).
These failures are preserved rather than treated as a completed qualification.

Eight read-only ADV7513 measurements per build show why lock/VIC alone are
insufficient: both the working and rejected RBFs report detected VIC 4 and PLL
lock. Automatic CTS is 74,250 with the verified RBF and 148,500 with the rejected
RBF, with N=6,144 in both. At nominal 48 kHz audio, the
[ADV7513 clock-regeneration relation](https://www.analog.com/media/en/technical-documentation/user-guides/adv7513_programming_guide.pdf)
therefore gives 74.25 MHz and 148.5 MHz respectively. The rejected image keeps
the initial HDMI clock instead of changing it for 720p/60, producing twice the
requested refresh rate (`build/hdmi-hardware-comparison.json`). This diagnostic
does not establish a complete PLL/controller fault mechanism.

The fitted HDMI PLL moved from X0/Y1 to X0/Y32 when video began sharing the audio
clock. An attempt to preserve X0/Y1 fails periphery routing because a destination
global clock driver is in the wrong region; it produced no new bitstream
(`build/hdmi-pll-location-quartus-failure.log`). The next source revision adapts
the existing HDMI write interface to the complete bundled Intel reconfiguration
IP instead of the reduced controller (`fpga/sys/pll_cfg/pll_cfg_hdmi.v`). The
seed-9 fit fails setup and hold timing; seed 10 improves to a single failing
cold HDMI setup group, with worst slack -0.131 ns and TNS -0.181 ns. Its critical
path is the inherited scaler's `o_div[0][20]` to `o_div[1][20]`. Neither image
was installed. Reports and images are preserved under
`build/hdmi-full-reconfig-seed9/` and `build/hdmi-full-reconfig-seed10/`.
Seed 11 passes all 140 timing checks with minimum slack 0.087 ns, the 44 routed
crossing checks and the four shared-clock audit groups. Its RBF SHA-256 is
`438facc3c251dc4d5caed128b85d9a0ebec1179d6e06cdd32c11f7c8c3e6505e`.
The accepted local reports, source provenance and RBF are preserved under
`build/hdmi-full-reconfig-seed11/`. After the human explicitly directed
**"Switch to TIC-80 now"**, the hold was released and this RBF installed with
the unchanged player and monitor. Eight live automatic-CTS measurements infer
74.249–74.250 MHz (maximum error 13.47 ppm), and a 771-sample, ten-second Tetris
smoke check has zero underruns and video/audio phase span 800 sample slots.
The user confirms **"Picture is correct and stable"** at 720p/60. These gates
pass; reload, language capture, finite audio, motion and long-soak qualification
are now in progress (`build/hdmi-full-reconfig-720p-clock.json`,
`build/hdmi-full-reconfig-720p-smoke.json`).
The first lifecycle attempt passes one 16-reload cycle, then stops on an SSH
channel-opening timeout during the second cycle. The board still has one
supervisor and one worker with a healthy service log. The SSH helper now gives
channel negotiation the caller's existing bounded command budget, includes it
in the deadline and never retries execution. Focused SSH/hold CTest passes
(2/2, 0.32 seconds). A fresh lifecycle run passes all 64 reloads across four
supervisors, BOOT=68, flat 3,980 KiB parent RSS, stable descriptors, CRC-checked
persistent counters and MENU exits of 0.196/0.196/0.240/0.247 seconds
(`build/hdmi-full-reconfig-lifecycle-result.json`). The interrupted run remains
preserved under `build/hdmi-full-reconfig-lifecycle-interrupted/`.
All 14 native language demos then pass live loading, advancing playback with
zero recorded underruns and exact full-RGB reference parity. The first observer
connection resets after nine completed demos; a fresh read-only probe finds
the same healthy supervisor and worker. A checked manifest-prefix continuation
validates the artifact/audio-journal hashes and finishes the other five demos.
The interrupted log and nine-entry manifest remain preserved separately;
`build/live-languages-hdmi-reconfig.json` and
`build/language-capture-parity-hdmi-reconfig.json` contain all 14 accepted
results. This is cartridge-by-cartridge acceptance, not an uninterrupted
observer-session result.
All 14 PNG language demos also pass loading, advancing playback without recorded
underruns and exact RGB reference parity (`build/live-languages-hdmi-reconfig-png.json`,
`build/language-capture-parity-hdmi-reconfig-png.json`). Finite CLI clips drain
800/1,600/2,400 stereo sample frames exactly in 0.112/0.104/0.079 seconds
(`build/hdmi-full-reconfig-short-audio-result.json`).

The first motion attempt never reaches capture because the short-clip diagnostic
stopped the service while its hook was disabled, and its rapid MENU/re-entry
was not observed by Frontier's one-second poll. The motion harness now retains
MENU for two seconds before re-entry, checking core ownership afterward.
With normal startup restored, all 16 animated captures have exact complete RGB
frames, but the subsequent audio monitor finds 622 previously accumulated
underrun slots; two ten-second steady observations add none. A second motion
run observes audio from before its first screenshot through the whole burst:
it starts at zero and ends at 2,163 missed slots, while every frame remains
intact. This fails the capture/audio stress gate; evidence is preserved under
`build/hdmi-full-reconfig-motion-audio-failure/` and
`build/hdmi-full-reconfig-motion-instrumented-failure/`. A separate fresh
600-second music/cadence/save soak was attempted with the installed, unchanged
`74728bc8...` player. It remained underrun-free for eight minutes, then recorded
182 missed slots at the 510-second check and 186 by its last observation.
The SSH connection reset after 582.135 seconds, before the final status and save
flush. This run is failed/incomplete, preserved under
`build/hdmi-full-reconfig-soak-failed/`; it is not a new accepted soak result.

An additional source candidate prevents streaming video acknowledgment waits
from consuming the last 800 queued audio samples: it omits the current picture
without modifying the pending frame or publication, so the service can produce
the next PCM tick. The finite CLI retains mandatory frame acknowledgment.
Its focused pacing, exchange and DDR transport
tests pass (3/3, 4.89 seconds), including immutable pending payload/control data,
prompt urgent streaming return, finite wait/timeout and stale-ACK rejection.
It was subsequently installed as player `9cc107f8...`, but failed the live
capture/audio stress test with 1,785 missed slots despite 16 intact frames.
The failure is preserved under `build/video-audio-backpressure-motion-failed/`.

A subsequent player, `fe230601...`, retains the latest deferred picture in an
owned RAM buffer while the FPGA acknowledges its pending picture. Audio pacing
can pump that private picture after acknowledgment without blocking PCM
production or modifying either pending DDR bank. Local transport/pacing tests
pass (3/3), as do service/observer checks (10/10) and the ARM transport fixture.
The journaled hardware motion run captures 16 exact frames but records 363
missed audio slots (about 7.6 ms), so this candidate also fails qualification.
Its evidence is preserved under `build/video-audio-deferred-motion-failed/`.

Read-only monitors can now write a bounded remote journal independent of the
SSH connection. Observer reconnection retries only reads, never core loads,
capture requests or monitor starts. Partial appended lines are reread without
loss or duplication; terminal status is published atomically. Host regression
tests verify these properties. Motion coverage checks actual elapsed duration
and bounded gaps rather than assuming every requested 13 ms wake occurs on
time; the zero-underrun requirement is unchanged.

The live process snapshot shows Main pinned to CPU 1, while the player and its
interpreter can run on both CPUs. A new source candidate pins physical playback
and its spawned interpreter to CPU 0 to avoid competition with Main during
screenshot compression. File-backed host fixtures retain their normal CPU
affinity. Player `85828e73...` is now installed with the same `438facc3...` RBF
and `9c7f6723...` monitor. Transport/pacing checks pass (3/3), service/observer
checks pass (10/10), and the ARM transport fixture passes. The fresh hardware
motion stress passes: 16 complete exact RGB frames, 3,230 coherent audio
samples over one minute, zero underruns, and both playback processes confirmed
on CPU 0 (`build/playback-affinity-motion-result.json`). Two fresh supervisors
then pass 16 reloads each (32 total), BOOT=34, valid persistent counters,
4,048/4,052 KiB initial RSS followed by a flat 4,192 KiB plateau after the
private picture allocation, stable descriptors, and MENU exits of
0.244/0.227 seconds (`build/playback-affinity-lifecycle-result.json`).
All 14 native and all 14 PNG language demos then pass loading, advancing audio
with zero recorded underruns, and exact complete RGB reference parity
(`build/live-languages-playback-affinity.json`,
`build/live-languages-playback-affinity-png.json` and the corresponding
`language-capture-parity-playback-affinity` records). The finite CLI clips drain
800/1,600/2,400 stereo sample frames exactly in 0.097/0.099/0.108 seconds
(`build/playback-affinity-short-audio-result.json`). The restored, unchanged
launch hook is rearmed through MENU before starting a fresh music fixture.
The fresh synchronized-video soak uses
`tic80-soak-4c358606a8c240429536f3f294a18784` and passes over 600.062 seconds:
3,047 coherent samples, zero startup or playback underruns, 47,999.300 Hz
measured playback clock, approximately 59.990 game frames and 59.999 carrier
frames per second, and -0.020 ms fitted audio-queue change. Video/audio phase
span is 800 sample slots. Every save has valid CRC; the final save has BOOT=1,
36,327 ticks and a largest tick gap of 52 ms. Steady parent/worker RSS is flat
at 4,228/4,932 KiB, with at most five/four descriptors. The monitor was started
once, its complete remote journal was collected, and no observer reconnect was
needed (`build/playback-affinity-soak-result.json`,
`build/playback-affinity-soak-samples.jsonl`).
During that run the user confirms **"Picture and stereo audio are good"** on
HDMI at 720p/60 (`build/playback-affinity-human-hdmi.json`), explicitly covering
the current CPU 0 player rather than the earlier candidates.
The board is then returned to Tetris through the normal launcher. Its fresh
ten-second monitor has 771 coherent samples and zero underruns; the final
eight automatic-CTS measurements all infer the requested nominal 74.25 MHz
HDMI clock (`build/video-clock-final-demo.json`,
`build/playback-affinity-final-hdmi.json`). An aggregate consistency check binds
the accepted motion, reload, 28 language captures/audio journals, soak and human
confirmation to the current player/RBF/monitor hashes and exact installed C
sources (`build/playback-affinity-qualification.json`). This qualifies the
documented HDMI development profile, not the broader release gates below.
The original display-rejected RBF and
fitted reports are preserved under `build/video-clock-rejected/`.

`tools/test_live_hdmi.py` now gates the actual CTS-derived clock as well as lock
and detected VIC, using repeated read-only register samples. Its baseline run
passes with eight 74.25 MHz measurements (`build/hdmi-720p-baseline-gate.json`).
Three host regression cases replay the measured working/rejected register
captures and check loss of lock/automatic measurement. Captures alone do not
prove TV acceptance or external I/O timing.

Device access was held for the user's authorized MUGEN tests in **Open Mugen**.
The TIC-80 chat stopped device writes, loads/reloads and automatic restoration
and left MENU untouched until the user directed another switch
(`build/hardware-access-hold.json`). One diagnostic rollback was refused after
the selected core changed during its guarded file operation; a later observed
MENU state allowed restoration of the verified RBF. Concurrent access explains
that ownership interruption, and fresh qualification requires exclusive access.

The shared SSH command helper now checks this hold before starting a remote
command. Focused CTest passes the SSH observer, hardware hold, HDMI clock
analysis and FPGA provenance checks (4/4, 1.28 seconds;
`build/hdmi-local-safety-tests.log`). The final FPGA build step records exact
RBF/source/timing-report hashes and fresh post-fit audit hashes in
`build/fpga-build.json`; the installer requires matching evidence. Five local
provenance regression cases pass, including rejection of the observed negative
setup slack, changed sources, stale audits and incomplete crossing evidence.
The MUGEN chat subsequently reported its bounded tests finished with the board
back on MENU. That report did not release the hold: a TIC-80 switch or
restoration required the human to direct another switch. The later
explicit human instruction above releases the hold for TIC-80 testing.

An animated diagnostic now combines a static checkerboard, a moving four-pixel
bar and a 24-bit frame ID in its top/bottom bands. Its independent RGB oracle
checks the complete 256×144 raster, including borders, and rejects mixed
frames or a single changed RGB byte. Desktop TIC-80 frames 0, 237 and 599
match exactly, including the bar's wrap at frame 237
(`build/motion/desktop-verification.log`, `build/motion/desktop-result.json`,
`build/motion/oracle-tests.log`). The current CPU 0 player's 16 hardware
captures match exactly, with zero underruns across their one-minute observation
(`build/playback-affinity-motion-result.json`). Pipeline captures verify frame
integrity; they do not establish panel latency or
physical analog sampling quality.

## MiniScript keyboard API repair

The next compatibility review finds that both specific-key `keyp(code)`
overloads in the pinned MiniScript binding call the gamepad `btnp` API.
A controller-only press reproduces a false keyboard-A event in both the direct
runtime and supervised worker (`build/miniscript-keyp-reproduction.log`).
For keycodes beyond 31, the repeat overload also accesses the wrong 32-button
hold array. The generated integration adapter now routes both overloads through
`keyp`, preserving the pinned reference checkout and its ordinary controller
binding.

The new `miniscript_keyboard` contract test uses independently specified edge
and repeat results while keyboard and controller states deliberately disagree.
It covers no-argument and specific-key queries, keyboard A, F12 and numpad
period, a three-tick hold/two-tick repeat, release/repress, and absence of
controller-triggered keyboard events. Four focused host checks pass, including
all-language interleaving, supervised VM parity/fault containment and service
fault recovery (`build/miniscript-keyp-local.log`). The keyboard contract passes
in ARM QEMU and on the MiSTer's native ARM CPU
(`build/miniscript-keyp-arm.log`, `build/miniscript-keyp-native-arm.json`).
These are software keyboard snapshots, not a physical-keyboard qualification.

Player `ea67db84...` was installed, retaining RBF `438facc3...` and monitor
`9c7f6723...`. Focused native and PNG MiniScript loads pass exact RGB reference
parity, advancing audio and zero recorded underruns
(`build/live-languages-miniscript-keyp.json`,
`build/live-languages-miniscript-keyp-png.json`). The live language harness now
allows explicit subsets; the image comparator requires exactly the requested
ordered runtime coverage. Default scope remains all 14, so a focused pass
cannot be presented as full language coverage.
Tetris is restored through the normal launcher. Its ten-second playback check
has 771 coherent samples, zero underruns and video/audio phase span 800 slots.
The final eight CTS measurements infer the nominal 74.25 MHz HDMI clock
(`build/miniscript-keyp-final-demo.json`, `build/miniscript-keyp-final-hdmi.json`).
The focused evidence is bound to this player in
`build/miniscript-keyp-qualification.json`.

The preceding 32-reload, 28-capture and 600-second results remain evidence for
player `85828e73...`; they are not relabeled as results for the new binary.
The following asset-bank review adds a separate API contract; wider API
conformance remains pending alongside the gates below.

## Asset banks and Wren video-bank repair

`runtime_asset_banks` runs the same four-phase contract in all 14 runtimes,
with independently specified expected bytes and RGB values. Its 42 cases cover
cartridge banks 0, 1 and 7: loading tiles, sprites, map, flags and both palettes;
writing changes back with `sync`; restoring those changes after clearing RAM;
and loading a different bank without cross-bank contamination. Each phase also
checks final composition of the two video banks, background transparency and
the 1,600-value stereo audio buffer. WASM is generated directly as a small
module, avoiding a separate SDK requirement.

This contract exposed the pinned Wren adapter's unconditional legacy `OVR()`
path. Every game inherits an empty `TIC.OVR()` method, but entering the legacy
callback clears video bank 1 before calling it. Modern `vbank(1)` drawing was
therefore erased even when the cartridge defined no overlay callback. Linking
the new ARM fixture against the preserved pre-fix Wren library reproduces the
missing pixel (`build/wren-overline-reproduction.json`).

The integration adapter now compares the game method's actual closure with
the empty base method, using the pinned Wren dispatch table. It skips only
that inherited default. Direct and supervised interleaved VM cases verify
ordinary `OVR()` overrides, overrides inherited through a parent, explicit
empty overrides, restoration of the active video bank and comments/strings
that contain `OVR()` without defining it. The pinned upstream checkout remains
unchanged.

Five focused host checks pass (`build/wren-overline-local.log`), and the
42-case contract plus callback cases pass under ARM QEMU and on the MiSTer's
native ARM CPU (`build/wren-overline-arm.log`,
`build/wren-overline-native-arm.json`). These API fixtures use RAM and synthetic
input; they do not establish physical-input or live DDR timing behavior.

Player `09951069...` was installed with the same RBF `438facc3...` and monitor
`9c7f6723...`. Focused native and PNG Wren cartridge loads match the full RGB
reference and each have 155 coherent audio samples with zero underruns
(`build/live-languages-wren-overline.json`,
`build/live-languages-wren-overline-png.json`). A fresh 600.062-second soak
passes for this exact binary: 3,047 coherent samples, no startup or steady
underruns, one BOOT, CRC-valid saves and 0.173 ms fitted audio-queue change.
Steady player/worker RSS is flat at 4,208/4,932 KiB; the player has five open
descriptors plus the observed temporary save descriptor, and the worker has
four. Video/audio phase span is exactly 800 slots; the longest game-tick gap
is 45 ms (`build/wren-overline-soak-result.json`). Earlier full reload,
motion and all-demo capture results remain bound to player `85828e73...`;
the focused MiniScript repair evidence remains bound to `ea67db84...`.
Tetris is restored at 720p: its final ten-second observation has 771 coherent
samples, zero underruns and an 800-slot video/audio phase span. Eight final CTS
measurements infer the nominal 74.25 MHz HDMI clock
(`build/wren-overline-final-demo.json`, `build/wren-overline-final-hdmi.json`).
`build/wren-overline-qualification.json` binds that profile's API, focused Wren,
soak and final playback evidence to its installed hashes.

## WASM return-slot repair

The next ABI review finds that six pinned WASM imports promise `i32` results
but declare one-byte C return types: `peek`, `peek1`, `peek2`, `peek4`, `pix`
and `vbank`. wasm3 writes only the declared C width into a reused result slot.
The rest of the 32-bit value can therefore come from a previous call. An
ordinary cartridge reproduces `peek()` returning `0x7ead00f1` rather than
241 after a full-width `pmem()` result. This also corrupts zero, nibble, bit,
pixel and video-bank results (`build/wasm-return-reproduction.log`).

The generated integration adapter now writes full `int32_t` results for these
six imports, with explicit checks against the pinned source declarations.
The original interpreter checkout remains unchanged. `wasm_return_slots`
checks ten ordinary direct cartridge probes, forty supervised probes across
four frames, and forty direct linked-import calls after four different
full-width seeds. No interpreter stack memory is modified by the fixture:
the seed values are produced by real `pmem()` calls. Pixel setters and
video-bank switches also retain their intended state changes.

All six focused host checks pass, including all-language asset banks, worker
fault containment and service recovery (`build/wasm-return-local.log`). The
current contract passes under ARM QEMU and on MiSTer's native ARM CPU, along
with all 42 asset-bank cases (`build/wasm-return-arm.log`,
`build/wasm-return-native-arm.json`). Relinking the current ARM contract against
the preserved pre-fix WASM library reproduces all ninety failures
(`build/wasm-return-arm-reproduction.json`). The export helper's WASM save tags
were corrected to the adapter's `--` comment syntax before the live test; the
fixture was rebuilt and checked again on host and native ARM.

Player `a49b30b5...` is installed with RBF `438facc3...` and monitor `9c7f6723...`.
Focused native/PNG WASM loads match the full RGB reference and each record
155 coherent audio samples with zero underruns
(`build/live-languages-wasm-return.json`,
`build/live-languages-wasm-return-png.json`). The same diagnostic module also
runs through the actual MGL launcher, supervised player and DDR transport.
Its ten API results match in CRC-valid live and final persistent-memory saves;
the five-second observation shows advancing playback and zero underruns
(`build/wasm-return-live-api.json`). The preceding 600-second soak remains
evidence for `09951069...`; this focused ABI repair is not a new long-run,
full-demo or physical-input qualification.
Tetris is restored at 720p. Its final ten-second observation has 771 coherent
samples, no underruns and a 789-slot video/audio phase span. Eight CTS samples
infer the nominal 74.25 MHz HDMI clock (`build/wasm-return-final-demo.json`,
`build/wasm-return-final-hdmi.json`). The focused evidence is bound to the
installed hashes in `build/wasm-return-qualification.json`.

## Studio console/editor integration

The optional Studio library now builds the pinned console, all editors,
project formats and local SURF UI without SDL. Its host CTest contract passes
console commands, Shift/Caps Lock/keypad/repeating text, undo/redo, copy/cut/paste,
keyboard navigation through six editor views, and mouse sprite painting with
undo/redo and separate edits in banks 0 and 7. Native `.tic`, PNG and Lua project saves each reload in a fresh Studio,
retain code, both painted pixels and a bank-7 map marker, and run/return through
the normal keyboard shortcuts. The same contract passes under ARM QEMU
(`build/studio-ctest.log`, `build/studio-arm-qemu.log`). It also passes on
MiSTer's native ARM CPU with 1,195 Studio ticks, each producing 800 stereo
sample frames (`build/studio-native-arm.json`). This test runs at low priority
offscreen and preserves the selected core and installed player's PID and hashes.
Rendered host screenshots
are in `build/studio-screens/`. This is offscreen library integration, not
evidence of Studio input or output through the FPGA. The installed player remains
`a49b30b5...`; Studio is not yet connected to the live service. The current
asynchronous HTTP backend now passes transport and filesystem tests on the host
and under ARM QEMU: binary/empty responses, redirects, HTTP errors, truncation,
size limits, a real 15-second timeout, encoded filenames, bounded queues,
callback chaining, cancellation, descriptor isolation and child/FD cleanup.
Catalogue parsing, cache reuse/recovery and failed/malformed download notification
also pass. Invalid loads preserve the edited cartridge. Networking closes before
the callback owners are freed. AddressSanitizer and leak checking pass the editor
and HTTP contracts after repairing a 4,335,960-byte leak on each PNG save
(`build/studio-http-local.log`, `build/studio-http-arm-qemu.log`,
`build/studio-http-asan.log`). A pinned Mozilla CA bundle provides verified HTTPS
on MiSTer installations with an incomplete system trust store. The native ARM
HTTP contract also passes with MiSTer's
bundled curl, and verified HTTPS reads return the real version response and
catalogue (two folders, thirty cartridges). The test downloads `bouncy_ball.tic`
(37,779 bytes) and validates its payload, without executing it. Its SHA-256 is
`4413b49000fdfee7150ae4d803197271426802db4a3b7a20896dc14225bb66d0`.
The selected core, player supervisor PID and installed binary hashes are
preserved (`build/studio-http-native-arm.json`). These are offscreen network
checks; public SURF presentation and interaction through the FPGA remain open.
The final HTTP run starts and ends on MENU with no player process; the separate
editor run starts and ends on TIC-80 with supervisor PID 6668. Their contexts
are recorded individually; neither test changes the selected core.
`build/studio-http-qualification.json` binds the current fixtures, source,
CA bundle and offscreen evidence. The earlier `build/studio-qualification.json`
describes the pre-network foundation and is not a current-build qualification.
Microphone FFT is still unavailable. Supervision, bounded decoding for remaining
local load paths and durable editor saves
must be integrated before deployment.

## Controller keyboard actions

The remapper now exposes W (Up), A (Left), S (Down), D (Right), Enter and Esc
after the existing Z/X/A/S actions. Xbox profiles retain the existing face
buttons and D-pad, assign left-stick Linux axes 0/1 to WASD, Start to Enter
and Back/Select to Esc. Both MiSTer name and position defaults identify the
four face buttons, Start and Select; axis bindings are installed as core-specific
v3 profiles and remain editable in the remapper. Global controller maps remain
unchanged, and backups of the installed profiles, RBF and player are retained.

The ARM input converter merges these keyboard actions from all four controller
slots. Controller A/S directions do not press TIC gamepad actions 6/7; physical
keyboard A/S retains the upstream gamepad defaults. Host and ARM QEMU checks
exercise all six actions in all four slots, release, modifiers, diagonal WASD
and independent D-pad input. Real Lua `key()`/`keyp()` checks verify held/released
state and one press edge per action. Eight focused host tests also pass transport,
cartridge service, lifecycle, reset, initialization and fault recovery
(`build/controller-keys-tests-final.log`, `build/controller-keys-arm-tests-final.log`).

The menu RBF `c1369ad3...` passes 140 timing checks with minimum reported slack
0.103 ns, the 44 existing audio/control crossing checks and four shared-clock
checks. Player `befd3feb...` and profiles for Xbox identities `045e:028e` and
`20d6:2062` are installed (`build/controller-keys-installed.json`). This local
and installation evidence is followed by a native ARM input run and synthetic
Xbox `20d6:2062` events through Main's actual remapper, HPS and FPGA DDR transport.
The running Lua diagnostic records all six keys, correct `keyp()` counts,
released state and an independent diagonal-plus-D-pad combination, without
unintended A/S gamepad actions (`build/controller-keys-live/result.json`).
The initial synthetic `045e:028e` probe is rejected because the user's saved
global map lacks analog-axis definitions; Main consequently suppresses negative
stick directions for that identity. No global map is changed to make the test pass.
The second probe uses the user's actual Xbox identity and its default global
axis definitions. This remains synthetic input evidence, not a physical test.
Tetris is restored afterward. Eight live HDMI CTS measurements infer exactly
the nominal 74.25 MHz clock for 720p/60 (`build/controller-keys-hdmi.json`).
An initial post-diagnostic Tetris session has a pre-existing count of 639
underrun slots, unchanged during its ten-second observation. A fresh ordinary
Tetris launch then records 771 coherent samples, advancing video/audio and
zero startup or steady underruns (`build/controller-keys-tetris.json`,
`build/controller-keys-tetris-fresh.jsonl`). The diagnostic-transition count
is retained in `build/controller-keys-tetris.jsonl`; it is not erased or
relabeled as zero. The current evidence is bound to sources and installed hashes
in `build/controller-keys-qualification.json`.
Physical-controller and panel acceptance still require observation; prior
qualifications remain tied to their earlier hashes.

## Q/E controller keyboard actions

The remapper additionally exposes Q and E, with L/R name and position defaults.
The two installed Xbox profiles bind them to their left/right shoulder actions;
LT/RT can be selected instead in the remapper. WASD, Enter, Esc, face buttons,
D-pad bindings and global controller definitions retain their preceding behavior.

Player `93eb3ac3...` and RBF `b294be00...` are installed with rollback backups.
Eight focused host checks, ARM QEMU and native ARM input tests pass all eight
keyboard actions and releases. Synthetic Xbox `20d6:2062` events pass Main's
actual remapper/HPS/DDR path and Lua `key()`/`keyp()`, with independent D-pad
input and no unintended A/S gamepad actions. The RBF passes 140 timing checks
with minimum slack 0.099 ns, 44 audio/control crossing checks and four shared
clock checks. Eight HDMI CTS observations infer exactly 74.25 MHz. A fresh
Tetris session records 771 coherent samples over ten seconds, advancing audio
and video and zero underruns. Evidence and artifact/source hashes are recorded
in `build/controller-qe-qualification.json`. After the standalone Studio fixture
fails at startup, a later observation of the same Tetris session has 48,478
underrun slots, unchanged during its one-second observation. That journal is
retained in `build/controller-qe-after-studio.log`; it does not establish the
time or cause of the stall. The initial clean observation is preserved in
`build/controller-qe-tetris-initial.json` and its journal. A fresh Tetris launch
then passes another ten-second observation with zero underruns. This remains
a narrow input/clock qualification; longer runtime and resource-contention
checks remain open. Physical shoulder/trigger and panel observation remain
separate; Tetris is left running.

The initial builds failed when C: filled and Quartus exhausted memory. Generated
artifacts were retained on D: behind a junction at `build`, Quartus parallelism
was reduced to two, temporary files were moved into the build directory, and a
fresh FPGA database resolved a subsequent fitter crash. Failure logs remain in
`build/controller-qe-*-resource-failure.log` and
`build/controller-qe-quartus-fit-crash.log`. No failed bitstream was installed.

## Studio worker supervision foundation

Studio now has a separate, bounded worker and a private parent checkpoint of
the last completed cartridge, asset banks, persistent memory, cartridge name,
saved hash, clipboard and display/audio output. A hung or killed worker is
replaced in the preceding editor mode. Recovery keeps the unsaved-change flag
and suppresses held launch controls until release. Partial cartridge and pmem
changes from a hung tick are discarded. Recovery initialization first completes
the console's deferred demo load so it cannot overwrite the restored cartridge.

The host, AddressSanitizer with leak detection, and ARM QEMU tests pass cold and later game hangs, bank preservation,
unsaved code edits, clipboard recovery, SIGKILL, malformed loads, run/return,
held-trigger suppression and clean child exit. The 600-tick simple RUN benchmark
averages 0.177 ms on the host and 1.187 ms under QEMU
(`build/studio-supervision-perf-host.log`,
`build/studio-supervision-perf-arm-qemu.log`). Console/editor and HTTP regressions
also pass. The original native startup failure is retained in
`build/studio-supervision-native-failed.json`. The diagnostic ran at nice 19 on
CPU 1, where MiSTer's Main continuously occupies the processor. Repeating the
same fixture on CPU 0 passes the full recovery contract
(`build/studio-startup-196883db.json`); CPU placement caused the startup timeout.

With Main on MENU and the player stopped, a standalone CPU 0 test runs the real
Tetris cartridge for 600 measured ticks. At foreground nice -10 it averages
11.646 ms, with p95 13.465 ms, p99 14.351 ms and maximum 16.829 ms. One tick
exceeds the 16.667 ms frame budget (`build/studio-native-70e2cab0.json`). The
background nice 19 comparison has 13 over-budget ticks and a 48.821 ms maximum
(`build/studio-native-cf22d4e4.json`). These timings exclude FPGA transport and
frame conversion; they do not establish 60 Hz Studio playback. The cartridge
checkpoint is 1,445,320 bytes. The foreground test restores a fresh Tetris
session with advancing playback and zero underruns in its one-second check.
This library is not connected to the installed live service.
Live performance, network/process-group cleanup,
editor undo/cursor state and durable editor saves remain to qualify and integrate.

The phase profiler isolates the native RUN cost into 5.491 ms for Studio's tick,
0.116 ms for sound and 5.902 ms for checkpoint publication. The checkpoint
breakdown shows 5.260 ms scanning the complete cartridge, 0.052 ms amortized
hashing and 0.231 ms copying output (`build/studio-native-0a38e872.json`). Only
two publications require a cartridge hash; recomputing it is not the bottleneck.

Checkpoint equality now uses NEON on ARM, comparing all bytes of every page,
including all asset banks, code and binary data. It handles unaligned ranges
and partial final pages without reading past them. Every-byte mutation,
independent alignment, vector/tail and protected-page tests pass on the host,
ARM QEMU and native board. The native scan averages 2.531 ms and checkpoint
publication 3.171 ms (`build/studio-native-af875983.json`). The normal fixture,
without phase instrumentation, then runs 600 real Tetris ticks at an 8.835 ms
mean, 10.233 ms p95, 11.394 ms p99 and 13.148 ms maximum, with no over-budget
tick (`build/studio-native-8fac597e.json`). Both fixtures pass the complete
supervision recovery contract and restore advancing Tetris playback with zero
underruns in their one-second checks. Four host editor/network/supervision/
comparison tests and two AddressSanitizer/leak checks also pass. Artifact,
source and evidence hashes are recorded in `build/studio-neon-qualification.json`.
This supplies additional timing headroom; live FPGA transport is still excluded
from these measurements and remains the next integration step.

## Supervised Studio live transport

The separate `tic80-studio-live` frontend now sends acknowledged Studio output
through the same DDR frame transport, 48 kHz stereo ring and playback-driven
pacer used by the player. It receives physical input snapshots and retains the
worker/checkpoint boundary. A bounded launch API enters RUN without depending
on console keyboard-shortcut readiness; its regression also covers a hung first
BOOT call before the console finishes startup. File-backed tests render the
console and a real Lua game, distinguish controller button and remapped W-key
input/release, preserve pending frames and stop both processes without further
DDR writes after core departure. Both host integration and supervision tests
pass (`build/studio-live-host-test-final.log`). The initial color oracle sampled
the mouse cursor rather than the game background; its corrected location is
outside the cursor. ARM QEMU also passes the expanded supervision contract.

Native fixture `766aec0d...` runs Tetris through the actual FPGA for 900 ticks in
15.056 seconds. Its measured processing cost includes Studio, input, frame
publication and audio submission: mean 11.286 ms, maximum 19.730 ms, with six
ticks over 16.667 ms. The playback reserve absorbs these observed delays. All
501 coherent samples over ten seconds retain one session, advance audio/video
and show zero underruns (`build/studio-live-30d5394b.json`). The test uses a
unique RAM-backed Studio folder and a temporary one-shot handler. It restores
the original handler hash `7c317c3f...` and the normal installed player
`ef3eb76f...`, then requests the usual Tetris MGL. A separate one-second check
confirms advancing restored playback with zero underruns. The installed RBF is
unchanged. Evidence and snapshots are bound in
`build/studio-live-qualification.json`.

This is a transport milestone, not a completed Studio launcher. Shared durable
save handling, OSD cartridge loading/reset, physical editor input qualification,
network-helper scheduling, and recovery while keeping the audio ring supplied
remain integration work. The synchronous recovery path can still exceed the
audio reserve on a hung tick; the finite Tetris test does not cover that case.

## Playback scheduling during transfers

A phase trace of the preceding player isolates 9,251 new underrun slots during
an 11 MB fixture transfer and checksum. The following 30-second Studio test
adds no underruns (`build/studio-startup-f2ec1073.json`). A temporary priority
probe of that same player repeats the transfer/checksum with zero new underruns
after raising only its supervisor and VM to nice -10
(`build/playback-priority-latest.json`).

Player `ef3eb76f...` now sets CPU 0 playback to CFS nice -10 before spawning the
VM. The persistent-memory writer lowers its own thread to nice 19. The native
test verifies both process priorities and the save-thread priority, then runs
three 11,032,848-byte uploads and five CPU 0 checksums alongside Tetris. All
1,501 coherent observations over 30 seconds show advancing audio/video and
zero underruns, without replacing the supervisor
(`build/playback-priority-native.json`). Nine focused host lifecycle, save and
supervision tests pass. Eight HDMI CTS observations still infer exactly
74.25 MHz (`build/playback-priority-hdmi.json`). The RBF remains `b294be00...`;
Q/E and the other controller mappings are unchanged. The previous player is
retained on the card for rollback. Source and evidence hashes are bound in
`build/playback-priority-qualification.json`.

This test covers the measured transfer/checksum contention. Broader storage,
cartridge and long-session audio qualification remains open. Studio is still
an offscreen library, with performance headroom required before live integration.
The build and reference directories now use D: storage through NTFS junctions;
verified reference leftovers remain on C: as `reference-move-leftovers`.

## Studio asynchronous transport candidate and WSL recovery

The separate Studio frontend now queues a worker tick and polls for its
acknowledgment while continuing FPGA transport. Pending ticks retain the last
acknowledged picture and supply silence, rather than repeating an old music
block or copying partial worker state. A persistent helper thread reaps and
starts a replacement after a timed-out or dead worker. Recovery restores the
last acknowledged cartridge and editor state; outstanding requests reject
load, run and clipboard mutations.

A first implementation performed replacement startup on the playback thread.
Its host fault test measured a 49.471 ms stall and 797 lost sample slots.
Moving startup to the helper passed the host fault tests and a native hang
test with zero underruns (`build/studio-live-3e73aebf.json`). However, polling
only at frame boundaries inserted 52 silent fallback frames during an ordinary
900-frame native Tetris run. A fixed 12 ms wait still inserted one fallback
frame (`build/studio-live-2308503d.json`). Both ordinary-playback results fail
the continuous-audio gate despite their zero DAC underrun counters.

The current candidate derives its wait budget from queued PCM, retaining
800 frames for publication and capping waits at 30 ms. The pacing model tests
10,000 pending ticks at nominal and +/-500 ppm rates, including counter wrap,
with no modeled underruns. Ordinary native playback must produce zero fallback
frames; that gate has not yet been established for this candidate.

After the WSL disk was replaced, the source workspace, clean pinned reference
repositories, build artifacts and qualification records were verified present
on Windows storage. CMake, the ARM C++ compiler, Ruby/rake and Verilator were
restored in Ubuntu. All four focused regular host tests and all three focused
AddressSanitizer tests passed (`build/studio-async-credit-host-tests-final.log`,
`build/studio-async-credit-asan-tests.log`). An earlier regular run ended without
its final result; the subsequent complete run is the passing evidence.
The restored ARM build also passes the complete Studio supervision contract
under QEMU and the audio pacing model (`build/wsl-recovery-arm-supervision.log`,
`build/wsl-recovery-arm-pacer.log`). The first emulator attempt could not spawn
ARM children; syscall tracing identified `execve` returning `Exec format error`.
Installing `qemu-user-binfmt` and reloading its registrations resolved that
environment failure. Sources, fixtures and recovery evidence are snapshotted
in `build/wsl-recovery-record.json`.

The native test could not connect to `192.168.1.176` after this recovery and
made no device changes. The candidate remains separate from the installed
launcher. Shared durable Studio saves, OSD loading/reset, physical editor input
qualification and broader fault and network qualification remain open. Kernel I/O
stalls and blocking process cleanup are not covered by the measured recovery
tests. Compiler temporary files and generated artifacts stay on D:.

## Studio mouse coordinates through the MiSTer input converter

The pinned Studio's toolbar, bank selector and sprite panel use game-area
coordinates. The FPGA's 0..239 / 0..135 pointer therefore covers these controls.
`tm_input_convert` adds the 8/4 border margin expected by `tic80_input`, and
the upstream `tic_api_mouse` subtracts it before widget hit testing. No FPGA
coordinate or protocol change is required for the tested editor interactions.

The expanded supervision regression passes mouse snapshots through that
converter, IPC, the actual Studio widgets and the acknowledged cartridge
checkpoint. It clicks the top-left pixel of all five editor tabs at y=0,
selects sprite bank 7 with the toolbar, picks palette color 5, paints a pixel
and verifies bank 0 was preserved. Ctrl+Z and Ctrl+Y remove and restore that
pixel in the acknowledged bank-7 cartridge. A release frame is followed by
the additional frame on which upstream applies toolbar tab requests.

Host and AddressSanitizer supervision tests pass
(`build/studio-mouse-host-tests.log`, `build/studio-mouse-asan-tests.log`).
ARM QEMU passes the same regression and the existing fault/checkpoint contract
(`build/studio-mouse-arm-qemu.log`); the optional ARM profiling target also
builds. Sources, binaries and logs are bound in
`build/studio-mouse-qualification.json`.
The existing Verilator input contract also passes with unchanged RTL,
including pointer clipping, signed wheel accumulation, modifier sides and
OSD clearing (`build/studio-mouse-rtl-input.log`). Broader physical Studio
mouse and keyboard qualification remains open. The user reported being at
work and unavailable for hardware testing; local work continues.

## Editor bank selection after worker recovery

A regression launches a hanging cartridge from the sprite editor after
selecting bank 7 and painting a pixel. The earlier worker restores the cartridge
and editor mode, but its fresh Studio instance resets bank selection to zero.
The subsequent mouse edit fails the bank-7 assertion. That failed binary,
test source and log are retained under `build/studio-bank-baseline/` and in
`build/studio-bank-baseline-failed.log`.

The acknowledged checkpoint now includes the four sprite/map/SFX/music bank
indexes and the bank selector's visible/chained flags. Parent publication
validation rejects indexes outside the cartridge's bank count before changing
its private checkpoint. The replacement applies the selections after loading
the acknowledged cartridge and before returning to the editor. The staged
adapter checks the pinned index-array size at compile time; the pinned upstream
checkout remains unchanged.

The regression now returns from the hung run to the sprite editor, picks a new
palette color and paints in bank 7 while verifying bank 0 is preserved. Host
supervision and live transport tests, three ASAN console/editor/supervision/
transport checks, and ARM QEMU supervision pass
(`build/studio-bank-host-tests.log`, `build/studio-bank-asan-tests.log`,
`build/studio-bank-arm-qemu.log`). The ARM frontend and both normal and profiling
supervision fixtures build.
Sources, fixtures and evidence are bound in `build/studio-bank-qualification.json`.
The native harness now checks the ARM fixture hash and compiled source hashes
against `build/studio-local-qualification.json` before performing setup.

This fixes the measured selected-bank loss. Broader editor cursor/selection
positions and undo history across recovery still need integration and
qualification; sprite preferences are covered below. The revised Studio candidate has not been tested on hardware;
the user is unavailable and no device changes were made.

## Sprite preferences after worker recovery

The extended regression exposes a second recovery loss: after painting with
color 5 and returning from a hung run, drawing the next pixel without changing
the palette does not use color 5. The failed binary, source and log are retained
under `build/studio-sprite-baseline/` and in
`build/studio-sprite-baseline-failed.log`.

The acknowledged state now carries an 11-byte view for each of the eight sprite
editors: selected tile coordinates, canvas size, brush size, primary/secondary
colors, bit depth, tile/sprite sheet bank and page, drawing tool, and advanced/
hex-index/palette-bank flags. Restore validates every view before applying any
of them. It reconstructs the tile-sheet segment and data pointer from the new
worker's local objects. Active drags, animation pointers and selection buffers
are not copied between processes.

The regression continues painting with the prior color after recovery. It then
selects sprite 5, palette color 11 and the fill tool, launches another hanging
run, and verifies that the subsequent fill changes the entire selected sprite
while preserving sprite 1. A separate offscreen contract supplies a valid early
view and an invalid final view with zero bit depth; restore rejects the array
and preserves all prior preferences.

Three focused host tests and three ASAN tests pass, covering console/editors,
supervision and live transport (`build/studio-sprite-host-tests-final.log`,
`build/studio-sprite-asan-tests.log`). ARM QEMU passes the expanded supervision
contract (`build/studio-sprite-arm-qemu.log`), and the normal/profile ARM
supervision fixtures and Studio frontend build. Evidence and snapshots are
bound in `build/studio-sprite-qualification.json`; the current native harness
gate is `build/studio-local-qualification.json`.

The view serializer covers additional preferences whose broader UI combinations
still need qualification. Code/map/music editor positions, active selections,
palette/flag text-field focus and undo histories across recovery remain open.
Native timing and playback of this candidate are still pending. The user is
unavailable for hardware testing and no device changes were made.

## Code cursor, selection and scroll after worker recovery

A new regression reproduces code cursor/selection loss: it selects the final
three characters of a comment after scrolling through forty lines, launches a
hanging cartridge, then pastes a replacement. The previous worker recovery
inserts the replacement at the beginning of the source. That binary, test
source and failure log are retained in `build/studio-code-baseline/` and
`build/studio-code-baseline-failed.log`.

The acknowledged checkpoint now includes a 24-byte code view. Cursor and
selection positions are byte offsets into the cartridge's code buffer;
selection -1 represents no selection. It also stores the preferred vertical
column, horizontal/vertical scroll and font/shadow preferences. The parent
validates the view against the complete acknowledged code before changing its
private checkpoint. Restore validates again, rebuilds local pointers and
syntax/delimiter/status state, then reapplies the preferred column and scroll.
Search/sidebar animations, mouse drags and other pointer-bearing editor state
are not serialized.

The expanded regression requires both scroll axes to be nonzero, compares the
entire view after recovery, and verifies that pasting replaces the selected
suffix. It then moves from a long line to a short line, hangs again, and checks
that moving down returns to the preferred column. Killing the worker with
SIGKILL also preserves the view and subsequent typing appends at the expected
position. Offscreen contracts reject out-of-range cursor/selection offsets,
negative selection/column/scroll and unknown flags without changing the view;
they also round-trip font/shadow preferences and selection endpoints.

Three host and three ASAN tests pass for console/editors, supervision and live
transport. The normal/profile ARM supervision fixtures and frontend build, and
the expanded supervision contract passes under ARM QEMU. Logs and snapshots
are bound in `build/studio-code-qualification.json`; the current native test
gate is `build/studio-local-qualification.json`. QEMU results do not qualify
native playback or recovery timing. Hardware testing is deferred while the
user is unavailable; no device changes were made.

Bookmarks, code popup/sidebar state, broader map/music/SFX preferences,
selections in other editors and undo histories across recovery remain open,
along with shared saves and OSD integration.

## Persistent-memory snapshot interface for Studio integration

The installed service uses upstream cartridge identity keys and 1,036-byte
`TMPM` files with a version and CRC. Before the managed integration below, Studio's pinned RUN screen
loads/saves raw upstream persistent-memory files through its own filesystem.
Studio and the service therefore had separate persistence paths.

The checked writer now exposes `tm_pmem_open_values`, `tm_pmem_schedule_values`
and `tm_pmem_save_values` for a supervisor's private 256-value array. The prior
runtime-pointer functions delegate to this same implementation. Open validates
the complete file before changing the supplied values; schedule takes a private
copy; final save drains queued work and commits the latest supplied snapshot.
This provides the save primitive needed to persist only acknowledged Studio
state without manufacturing a runtime object or reading a live worker buffer.

The expanded persistent-memory test crosses between the runtime and snapshot
interfaces, checks full-width values at index 255, changes the source array
immediately after scheduling, drains the writer and verifies the originally
queued values. It also verifies a newer final snapshot wins over queued work.
A corrupt file leaves the supplied array unchanged and closes its writable
context; subsequent schedule/save operations reject it.

Five host checks pass: persistent memory, injected storage faults, cartridge
service, autosave retry recovery and PNG cartridge service. The same five
checks pass under ASAN (four ordinary tests and the preload-based autosave test
separately). Initial ASAN service runs failed before cartridge initialization:
the VM's 128 MiB virtual-address limit prevented ASAN's tracking allocation.
Like Studio, the VM now omits that limit only when compiled with ASAN;
ordinary builds retain it. Core-dump suppression applies to both builds.
The preload-based ASAN autosave test uses
`ASAN_OPTIONS=verify_asan_link_order=0` to allow the injected filesystem shim;
address instrumentation remains enabled. Save behavior and
storage fault assertions were not relaxed.

ARM `pmem_test` passes under QEMU and the ARM service builds. Source/binary
snapshots and logs are bound in `build/pmem-snapshot-qualification.json` and
`build/pmem-snapshot-fixtures/`. Native performance, slow/failing physical SD
storage, power-loss durability and shared Studio save integration remain open.
This milestone does not connect Studio to the service save directory, and no
device changes were made.

## Managed Studio saves using the service format

`tic80-studio-live --saves EXISTING_DIRECTORY` and
`tm_studio_session_open_saved` enable shared persistent memory. The directory is
resolved and validated before creating the worker. Unmanaged/offscreen Studio
retains its upstream storage. The optional RUN loader computes the service's
save identity (explicit `saveid`, otherwise bank0 MD5), validates the checked
file and populates RAM before BOOT/TIC. A restart of the current identity uses
the preceding acknowledged values even when its disk write is pending.

Managed ticks suppress upstream's raw persistent-memory writes. The worker
publishes a validated identity and completed values; the parent keeps a private
copy and queues autosaves roughly every sixty publications, on identity changes
and when leaving RUN. Cancelling or recovering a hung tick retains the prior
acknowledged copy. Loading a cartridge preserves the preceding save identity's
values until its next RUN, so reset RAM cannot overwrite that save. Closing
flushes the last accepted values. Background failures queue retries and expose
save error state; a failed identity transition rejects further execution and
close reports failure.

The initial shared-save regression exposed a RUN error-path problem: calling
the existing error handler from RUN initialization could be overridden by the
enclosing mode transition. The staged Studio transition now keeps a rejected
save in the console; the programmatic RUN request returns an error before
BOOT/TIC. The initial failing log is `build/studio-save-host-tests.log`.

The C contract seeds a service-format save through the runtime API, loads it in
Studio and verifies BOOT/TIC counters and all 32 bits at index 255. It restarts
before an autosave interval, switches A/B/A identities, recovers a partial
`pmem(0,9999)` hang, cancels an outstanding request, and verifies the exact
acknowledged values after final flush. It rejects a corrupted identity without
replacing its file, and tests bank0 identity fallback through the runtime API.
The paced frontend fixture additionally checks the saved counter equals one
initial RUN tick plus its reported completed ticks, a valid header/CRC, full
index-255 precision, no upstream raw save and zero modeled DAC underruns.

Six host checks and the same six ASAN checks pass: console/editors, supervision,
shared saves, live transport, persistent memory and injected storage faults.
ARM shared-save and supervision fixtures pass under QEMU, and the normal/profile
ARM fixtures and frontend build. Evidence is bound in
`build/studio-save-qualification.json`; the current native harness uses
`build/studio-local-qualification.json` and a private RAM-backed save directory.
Only the ARM frontend and ARM shared-save fixture are archived alongside source
and logs; other tested binary hashes are recorded without extra binary copies.

At this milestone, identity changes flushed the previous writer and initialized the next
context synchronously in the parent; the worker also reads a new identity's
file synchronously. These transition/close I/O paths need asynchronous handling
and slow/failing-storage playback qualification. Launcher adoption, editor
project save paths, OSD loading/reset, native timing and physical storage
durability remain open. No hardware tests or device changes were made while
the user was unavailable. The parent I/O stall is addressed by the following
milestone; shutdown and physical storage qualification remain open.

## Background save-identity preparation and paced slow-storage behavior

The fault contract delays the old identity's save-file fsync for 500 ms while
launching another identity. The prior implementation blocks `poll(0)` beyond
the contract's 100 ms limit. Its failed fixture/source are retained in
`build/studio-save-latency-baseline/`, with the failure log in
`build/studio-save-latency-baseline-failed.log`.

Identity preparation now runs on a persistent background thread at nice 19.
After validating the worker's completed ACK, the parent returns PENDING while
that thread flushes the old identity and opens/validates the new one. The
previous private ACK remains visible. A release/acquire completion handshake
allows the parent to accept the new publication and queue its values; the
thread does not queue unaccepted values. A newly prepared context retains its
on-disk baseline until acceptance, preventing cancelled or invalid ACKs from
writing the prior identity's values into the new file.

The worker execution deadline applies until its complete ACK. Storage waits
after that ACK are independent: asynchronous callers continue pacing audio
and retaining the previous picture instead of recovering a worker that already
finished. A persistent helper avoids joining an I/O thread in the poll path.
Close cancels unaccepted work, stops/joins the helper and flushes the accepted
save context; final durability can still block shutdown.

The expanded C contract uses a 200 ms execution timeout with the 500 ms disk
hold, checks that pending getters retain the prior values and requires every
zero-wait poll to return within 100 ms. It then cancels a transition and checks
that A's accepted values and B's existing values remain distinct. A failed old
flush rejects the new ACK, preserves the prior view, reports save error and
leaves both previously valid files unchanged. Host max poll measured 0.078 ms;
the ARM QEMU run measured 0.800 ms. These are fixture measurements, not native
board latency guarantees.

The paced frontend fixture interposes exactly one 500 ms fsync delay. Its Lua
cart changes bank0, and physical Ctrl+R in the DDR keyboard snapshot restarts it
under a new identity while the original write is delayed. Host and ASAN runs
each observe 21 paced waiting frames, zero modeled DAC underruns and no worker
recovery. Both resulting TMPM files have valid version/CRC, and their combined
tick counter equals the initial tick plus the frontend's accepted completed
ticks. Pending frames contain deliberate silence; this is a loading-transition
qualification, not proof of continuous music during a normal run.

Eight host and eight ASAN checks pass, covering the new C latency/fault test and
slow-save transport in addition to editors, supervision, shared saves, normal
transport and persistent-memory/fault contracts. The preload-based ASAN fixture
sets `verify_asan_link_order=0` for its filesystem shim; address instrumentation
remains enabled. ARM latency/fault, shared-save and supervision contracts pass
under QEMU, and the frontend plus normal/profile fixtures build. Evidence is
bound in `build/studio-save-latency-qualification.json` and the current native
gate `build/studio-local-qualification.json`. Only three ARM fixtures are
archived with source/logs; other tested binary hashes are recorded.

The worker still performs RUN save reads synchronously inside its supervised
request, and shutdown waits for final storage work. Broader read stalls, kernel
I/O that never returns, save-error presentation/retry policy, real SD-card
performance and durability, native continuous-music timing, launcher adoption,
editor project paths and OSD integration remain open. No device changes were
made while the user was unavailable.

## Save failure recovery and in-session retry

A failed old-identity flush now rejects the candidate ACK and replaces the
worker from the private checkpoint, returning to its home editor instead of
stopping the frontend. The selected cartridge and acknowledged persistent
values survive. A failed parent open of the next save retains the old context,
so recovery can publish the old identity without repeatedly reopening the
rejected file. RUN retries preparation in the same session. A permanent final
flush failure still makes close report an error; keeping an editor alive does
not establish durability.

The parent checks background writer status without filesystem I/O. Active
writes remain pending until completion, and a queued retry does not clear an
existing warning prematurely. The worker displays "Save failed; edits kept.
Retry RUN." while the error persists. The popup is refreshed periodically so
its animation can unroll, rather than restarting it every frame.

The C fault contract covers failed old flushes, a parent-only failed new-file
open, restored values, continued editor ticks, in-session RUN retries, distinct
A/B files and permanent-failure shutdown. The paced transport contract adds
failed writes during identity preparation, recovery, a later physical Ctrl+R
retry after storage resumes and two CRC-valid save files. These remain local
models; physical SD durability and native playback qualification are open.

Nine host and nine ASAN checks pass. The failed-storage paced fixture records
one recovery, a successful retry and zero modeled DAC underruns; the normal
slow-storage case remains at zero recoveries. ARM fault/retry, shared-save,
supervision and persistent-memory fault contracts pass under QEMU. A captured
256x144 console image shows the warning fully inside the viewport; a pixel
contract also checks that its banner has actually unrolled after 25 ticks.

Current source/binary hashes and test logs are recorded in
`build/studio-save-retry-qualification.json` and
`build/studio-local-qualification.json`. No device changes were made while the
user was unavailable.

## Supervised save-read rejection and live retry

The previous live frontend exited at startup when Studio rejected a corrupt
save, even though the Studio session itself had preserved the file and returned
to the console. The failing paced fixture and prior executable/source are in
`build/studio-read-baseline/`; `build/studio-read-baseline-failed.log` records
zero published ticks and the fatal frontend exit.

`TM_STUDIO_SAVE_ERROR` now distinguishes a refused RUN save load from a fatal
supervisor error. Both programmatic startup RUN and physical Ctrl+R keep the
live frontend open on this result. The worker rejects before BOOT/TIC and
publishes its console result without replacing the worker. Read warnings are
tracked separately from background write failures, so a successful autosave
of the previous identity cannot clear a still-unresolved read warning. A later
acknowledged RUN clears the read warning after its load succeeds.

The new C worker fault contract injects EACCES, EIO, short reads, fclose failure
and a 500 ms delayed open. Physical Ctrl+R requests use a 100 ms execution
deadline; `poll(0)` retains the private checkpoint while pending. Explicit read
failures keep the worker and console alive; a delayed request times out and is
replaced. Cartridge comparison and persistent-value checks verify BOOT/TIC did
not run. Sixty-five continued editor ticks retain the warning, both on-disk
identities remain intact and each same-session retry succeeds after the fault
is removed. These are injected userspace stalls, not proof that kernel I/O
stuck in uninterruptible sleep can be bounded.

The expanded live transport fixture rejects the same corrupt save at startup
and on physical Ctrl+R. It verifies the bytes remain unchanged until the
fixture supplies a repaired CRC-valid save, then retries RUN successfully,
persists subsequent acknowledged values and exits cleanly on core departure.
The modeled DAC has zero underruns. Ten host and ten ASAN checks pass; ARM
read-fault, write-fault, shared-save and supervision contracts pass under QEMU.
Evidence and current source/binary hashes are in
`build/studio-read-qualification.json` and
`build/studio-local-qualification.json`. Only three ARM binaries are archived;
all tested binaries have recorded hashes. No device changes were made.

Native playback, physical SD-card behavior, frontend pacing through longer
read stalls, shutdown durability waits and non-returning kernel I/O remain
open alongside launcher/editor project/OSD integration and the gates below.

## Paced worker read stalls, timeout recovery and departure

Four new live frontend fixtures inject a one-shot delay in the worker's save
open after the cart changes bank0 identity. They use physical Esc and Ctrl+R
snapshots, the actual 48 kHz audio-ring protocol and the same frontend pacing
loop used on the board. A tone before the stall and after successful RUN
confirms the fixture can distinguish nonzero audio from silence.

The 500 ms read from the console completes without recovery. A ten-second
read from the console is cancelled at the five-second execution timeout, then
a later RUN succeeds in the restored session. The same delayed read triggered
from RUN is cancelled at its 250 ms deadline. A fourth case switches CORENAME
to MENU while the console request remains blocked; shutdown does not wait for
the injected ten seconds and leaves DDR byte-for-byte frozen after departure.
Only the acknowledged old identity is saved in that departure case.

Host observations show 30, 301, 16 and 119 waiting frames for slow, console
timeout, RUN timeout and departure respectively. All four have zero modeled
DAC underruns, retain the acknowledged picture during sampled pending
intervals and submit silent pending PCM rather than repeating an old tone.
The success/retry cases leave two CRC-valid save identities; departure leaves
one. The fault's worker and replacement workers are gone after frontend exit.
These numbers describe the local fixture, not native latency or continuous
music during loading.

The ordinary recovery popup previously exceeded the 240-pixel viewport. A
new margin contract fails on the old text, with source/fixture evidence in
`build/studio-read-transport-baseline/` and
`build/studio-read-transport-baseline-failed.log`. It now reads "Run stopped;
completed edits kept", with both margins visible in the captured console
frame. The separate save-failure warning remains intact.

Fourteen host and fourteen ASAN checks pass, including the four new paced
fixtures, read/write faults, shared saves, editors, supervision and transport.
ARM read/write fault, shared-save and supervision contracts pass under QEMU.
The preload ASAN fixtures use `verify_asan_link_order=0`; address
instrumentation remains enabled. Source/binary hashes, logs, baseline and the
recovery screenshot are recorded in
`build/studio-read-transport-qualification.json` and
`build/studio-local-qualification.json`. No device changes were made.

Native CPU/clock and HDMI/audio behavior, physical SD-card durability and
latency, shutdown storage waits and uninterruptible kernel I/O remain open.
The injected reads are interruptible userspace sleeps. Launcher adoption,
editor project/OSD integration and the broader completion gates below are
still required.

## Studio OSD cartridge transport and asynchronous load/RUN

The separate Studio frontend now consumes the existing CART_META/CART_ACK
protocol. The backend acknowledges staging only after making a private copy.
The frontend retains at most the latest queued payload, then submits load and
RUN as separate supervised asynchronous requests while continuing picture and
audio pacing. Decode/validation takes place in the worker. A rejected cart
returns `TM_STUDIO_CART_ERROR`, keeps the previous cartridge and lets the
frontend continue. Load/wait/save-rejection phases submit silence rather than
replaying the previous product's audio.

The prior frontend ignored OSD tickets. Its executable/source are retained in
`build/studio-osd-baseline/`; `build/studio-osd-baseline-failed.log` records the
unacknowledged first ticket while the prior console continues running.

The paced OSD fixture sends seven tickets: native A, PNG B, malformed bytes,
a bridge-rejected transfer, a cart that hangs after a partial pmem mutation,
then two selections during that pending run. Each accepted bridge ticket is
privately copied before ACK. The fixture immediately overwrites the staging
header after ACK to exercise ownership. Four expected games run, malformed
and failed transfers preserve B, the latest pending selection wins, and the
superseded cart never executes or creates a save. Per-identity counter and BOOT
checks exclude the hung partial mutation, and all retained saves have valid
CRCs. The modeled DAC has zero underruns and DDR remains frozen after core
departure.

The session contracts now also cover asynchronous malformed-load rejection,
the one-outstanding rule and asynchronous RUN after read-fault repair. Valid
asynchronous loads run in shared-save and supervision tests on host, ASAN and
ARM/QEMU. Supervision frees the caller payload immediately after begin_load,
then verifies the selected cart/name and bank contents after its ACK. Fifteen
host and fifteen ASAN suite checks pass; the strengthened asynchronous helper
contracts and ARM read/write fault contracts pass independently as well.

Source/binary hashes and logs are recorded in
`build/studio-osd-qualification.json` and
`build/studio-local-qualification.json`. The installed handler and player were
not changed. This integrates the cartridge data path in the separate frontend;
native OSD testing, reset/FPGA reload lifecycle integration, original filename
and project-path handling, editor switching policy, maximum-size payload copy
timing and launcher adoption remain open. The OSD protocol currently carries
bytes rather than a filename; Studio uses "MiSTer cart.tic" as its working name.
No device changes were made while the user was unavailable.

## Studio reset hold/release

The separate frontend now observes STATUS bit 0. A reset hold cancels the
outstanding Studio command and asynchronously restores the last private ACK
in its home editor. It issues no BOOT/TIC or editor ticks while held and keeps
feeding silence to the paced audio transport. Incoming OSD bytes are copied
and acknowledged while held; only the latest selection is retained. A LOAD
already in progress retains its private payload so cancellation cannot lose
an acknowledged ticket. Release loads and runs the chosen cart once, or
restarts the last acknowledged Studio cart with its accepted edits and pmem.
Startup `--run` also waits when reset is already asserted.

`tm_studio_session_begin_pause` discards unaccepted worker products. If a
save-identity helper owns storage, cancellation waits asynchronously for its
completion before starting restoration. The prepared identity keeps its disk
baseline; neither the cancelled tick nor old-identity values enter its save.
Execution deadlines and storage ownership remain separate.

The pre-change executable in `build/studio-reset-baseline/` fails the paced
switch regression by executing A during the hold and booting B before release.
`tests/studio_reset_test.py` checks selection during a hold, cancellation of
an acknowledged in-flight LOAD, repeated reset, initial reset and departure
while held. It checks exact BOOT counts, pmem CRCs, zero modeled DAC underruns,
frozen DDR after departure and worker cleanup. Session contracts additionally
cancel a partial hung tick and a valid LOAD; the blocked-fsync contract pauses
during a save-identity transition and checks that A and B retain separate values.

Twenty-one host and twenty-one ASAN checks pass across the suite and additional
LOAD/persistent-memory checks. ARM/QEMU supervision, shared-save and read/write
fault contracts pass. The ARM 600-tick synthetic RUN sample has mean 1.126 ms,
p99 2.014 ms, maximum 2.177 ms and zero ticks above 16.667 ms; these are emulator
measurements, not native playback qualification. Hashes, source, logs and three
ARM candidate snapshots are recorded in `build/studio-reset-qualification.json`
and `build/studio-local-qualification.json`.

This closes local reset handling only. Full FPGA reload/session generation,
native reset and OSD testing, continuous audible playback, editor project
policy and launcher adoption remain open. No device changes were made while
the user was unavailable.

## Studio FPGA reload and initialization lifecycle

Studio now recovers session loss detected while pacing, publishing, taking
input or requesting work credit. It cancels unaccepted Studio work, retains
the last ACK and any privately acknowledged OSD selection, and attempts a new
backend handshake while TIC-80 remains selected. When FPGA identity is absent,
it polls worker restoration without publishing DDR payload/control words.
Reconnection and Main initialization have a bounded ten-second wait (a backend
handshake can take up to one further second). Missing initialization returns
an error and closes with the last acknowledged persistent memory.

A new FPGA session alone does not authorize RUN. The frontend establishes
fresh reset history and waits for a new session's reset falling edge or a
changed matching CORENAME generation with reset released. An old session's
held reset cannot become a false initialization edge. Reconnection also clears
the input converter and cached backend snapshot. The latest queued OSD cart
runs after readiness; otherwise the accepted Studio cart restarts once.

The saved pre-change frontend exits at the first session loss after fifty
modeled frames (`build/studio-reload-baseline-failed.log`). The new paced fixture
covers delayed reset release, a missed pulse detected by file generation,
old-session reset, departure during initialization, temporary and permanent
identity loss, a queued OSD selection, initialization timeout, and two reloads.
It checks accepted tick/BOOT counts, CRC-valid per-cart saves, an editor picture
during the wait, zero modeled DAC underruns per connected session, no DDR
mutations while identity is absent, and frozen DDR after departure.

Twenty-nine host and ASAN suite checks pass, plus the additional permanent-loss
check on each platform (thirty checks each). All nine reload scenarios also
pass using the ARM frontend under QEMU. Source/binary hashes, logs and three
ARM snapshots are recorded in `build/studio-reload-qualification.json` and
`build/studio-local-qualification.json`; the previous reset qualification is
retained as evidence for unchanged session/save contracts.

No device changes were made. Native reload/reset/OSD and audible playback,
project/editor switching policy and launcher adoption remain required gates.

## Studio OSD selections and unsaved changes

The OSD frontend now uses `tm_studio_session_begin_select`, while internal
startup/test loads retain the explicit force-load API. The worker validates
the native/PNG payload before asking a question. A clean cartridge is selected
directly; a modified cartridge opens the upstream `confirmLoadCart` dialog
with NO selected. Controller A on the default answer or Back/ESC cancels;
Down then A approves YES. Cancelling retains editable bytes, the cart name,
dirty state and the original running VM/pmem where applicable. Only a
`TM_STUDIO_CART_SELECTED` publication authorizes RUN of the candidate.

The parent retains the private candidate until the answer. New tickets remain
in the latest queue and never overwrite the dialog's IPC payload. A queued
newer selection supersedes an approved candidate before it boots. Reset or
worker death restores the last accepted edits, cancels the old callback and
recreates confirmation with NO selected. It never converts an interrupted
question into approval. Departure closes without booting a pending candidate;
unsaved editor durability across application exit remains a separate gate.

The initial ASAN contract exposed a 24-byte upstream confirmation-context leak
when ESC bypassed its answer callback. The staged adapter now tracks that
owned context, resolves cancellation as NO, and releases it during replacement
or teardown. The pinned checkout remains unchanged. The earlier OSD frontend
silently replaced edited A with B; its failed cancel fixture and original
source/executable are preserved under `build/studio-selection-baseline/`.

During the broader transport run, a core-departure snapshot changed. Backend
inspection found a missing session renewal after acquiring video/audio space.
Both copies now renew selection/session validity immediately before touching
DDR. A deterministic fixture changes CORENAME after a reader observed TIC-80,
while leaving stale DDR identity/session words valid. The original backend
fails the frozen-DDR check; the revised backend rejects both copies without
mutating DDR (`build/studio-selection-backend-baseline-failed.log`).

Local checks cover clean/invalid selections, default-NO and ESC cancellation,
saving retained edits, explicit YES, latest queuing, reset/SIGKILL recreation,
departure, and resuming a dirty running game without another BOOT. These
supplement existing read/write faults, OSD/reset/reload and cartridge-service
regressions. The autosave fault fixture now permits its intentional preload
shim ahead of libasan without disabling address instrumentation.

All forty-seven host checks pass. Forty-six ASAN checks pass in the broad run;
the preload-order correction passes the remaining autosave check separately.
The strengthened dirty-RUN contract also passes on host, ASAN and ARM/QEMU.
Six ARM session/backend contracts and seven ARM paced selection scenarios
pass. Source/binary hashes, failure/repair logs and three ARM candidates are
recorded in `build/studio-selection-qualification.json` and
`build/studio-local-qualification.json`.

Original OSD filenames/project paths, other editor/dialog state, native dialog
and reset/reload/OSD input, continuous playback and launcher adoption remain
open. No hardware changes were made while the user was unavailable.

## Studio source paths and cartridge saves

`tic80-studio-live --cart` now reads native/PNG files in its supervised worker
and records the canonical source path, including sources outside `--folder`.
Invalid, oversized or nonregular files keep the accepted cartridge and its
destination. Browser/project loads resolve Studio's virtual root before
recording their source. Default Save uses that recorded directory, whereas an
explicit Save name follows the current browser directory. Path-length checks
reject truncated destinations and roots that exceed the pinned filesystem's
buffer, including its trailing separator.

Cartridge Save writes a temporary file in the destination directory, flushes
it, renames it over the destination, and flushes the directory. Existing file
aliases remain aliases, and an existing regular file's permissions are kept.
Write, pre-rename flush and rename failures preserve the original source and
the editor's modified state. A failure after rename can leave the new file on
disk while the editor still reports failure; retry remains available. A killed
worker can leave a temporary file, and recovery cannot undo an already
completed rename. These checks do not establish physical SD power-loss safety.

The contract covers nested/external sources, directory changes, PNG and text
project roundtrips, aliases, malformed/FIFO rejection, bounded paths, partial
writes, sync/rename faults and recovery during Save. The original selection
fixtures expected a copy under the browser root; their repaired cancellation
checks now require the CLI source to contain retained edits and reject such a
copy. Initial failure logs are preserved alongside final qualification logs.

All 51 host checks and 51 AddressSanitizer checks pass. Six ARM/QEMU contracts
and two paced cancellation scenarios also pass. Source hashes and
three ARM candidate snapshots are recorded in
`build/studio-project-qualification.json`. These are local results. Main's
cartridge transport supplies extension bytes, not a filename; its optional
file-selection log still needs freshness/payload association and native
qualification before OSD Save can use original paths. Native playback,
launcher adoption, broader editor state and the gates below remain open.
The installed MiSTer was unchanged while the user was unavailable.

## Transfer-bound OSD source filenames

Main's optional `/tmp/FULLPATH` log is not used as Save authority. It is separate
from the cartridge ticket, is rewritten non-atomically, and can describe a
later selection before ARM consumes an earlier transfer. Identical carts in
different folders cannot be disambiguated by payload comparison alone.

The local candidate instead sends a `TSN1` metadata packet on reserved file
index `0xfe` immediately before F0/TIC or F0/PNG. It contains the target index,
bounded absolute source path and a terminating NUL. The loader captures it,
consumes it for exactly one matching cart, and writes a source tag containing
that cart's complete ticket before publishing CART_META ready. Source and cart
staging both remain protected until that ticket is acknowledged. Malformed,
truncated, oversized, wrong-index and reset-interrupted metadata cannot attach
a stale path to a later cart. Other file slots invalidate pending metadata.

The optional `TSN1` capability is in the upper identity word; the existing TIC3
magic and all old offsets stay unchanged. Source storage occupies unused space
between the PCM ring and cart staging. Existing ARM players ignore the added
capability, and old Main sends ordinary carts with no source. Patched Main's
new packet is restricted to TIC-80; old RBFs ignore its reserved index. Both
mixed-version arrangements preserve cart loading, without original filenames.

The backend copies the source and bytes together before ACK. Queueing,
confirmation, reset and worker-recovery paths retain their paired ownership.
After explicit approval, the supervised worker requires an exact regular-file
byte match before using a canonical source name/path. Changed, missing, FIFO
and virtual archive sources retain the supplied working destination. NO keeps
the previous edits and source. Identical payloads/basenames in different
folders remain distinct because the path arrived with the transfer itself.

Filename suffix matching is length-checked and case-insensitive. Uppercase
native/PNG source names retain their format and path on Save, and short names,
an exact `.tic` name and names with multiple extension-like substrings are
covered. The staged filename builder checks capacity before copying or
appending and leaves oversized names for destination validation to reject.

`tools/stage_main_source.py --output build/main-source` stages the guarded Main
change, companion packet encoder, protocol header and patch without modifying
the pinned transfer source. Its complete `user_io.cpp` translation unit
cross-compiles for ARM. A linked Main image and its target ABI are still
unqualified. A line-ending-only delta in Main's unrelated miniz ChangeLog was
observed and left untouched; TIC-80 and PICO-8 pinned sources remain clean.

Cart-loader and combined DDR simulations verify source ownership and stalled
bus writes. All 59 host and 59 AddressSanitizer checks pass. Six ARM/QEMU
contracts and five paced source scenarios pass, including latest queuing,
reset/SIGKILL re-prompting, cancellation and immediate staging reclamation.
The new RBF passes 140 timing checks (minimum reported slack 0.117 ns), the
existing audio/control CDC audit and shared-clock audit. These are local
results, not native HDMI/audio/input qualification.

A regression run with simulated DDR on Windows-mounted temporary storage
produced underruns. The same ARM source-selection binary passed with its DDR
fixture on Linux `/tmp`, with mean tick time 0.26 ms instead of 18.46 ms and
zero underruns. Final paced fixtures use Linux temporary storage; the failed
run and storage probe logs are retained. Assertions were unchanged, and these
simulation results do not qualify physical SD-card behavior.

The candidate, accepted-RBF backup, source hashes, Main object/staged patch and
test/build evidence are recorded in `build/studio-source-qualification.json`.
The installed launcher, player, Main and RBF were unchanged. Native source
packets and mixed-version behavior, continuous Studio playback, a linked Main
build, launcher adoption and the broader gates below remain open.

## Complete Main filename-protocol candidate

The complete pinned Main source now builds and links with GNU Arm GCC 10.2.1,
using the toolchain selected by its pinned setup script and the
[MiSTer build guide](https://mister-devel.github.io/MkDocs_MiSTer/developer/mistercompile/).
`tools/build_main_source.py` exports the committed tree into a fresh directory,
applies the guarded TIC-80 packet patch, fixes the version date and caps the
build at four jobs. It never calls upstream deployment scripts. The compiler,
compressed installer, sources and outputs reside under the D: build junction;
the complete compiler installation including its archive uses about 591 MiB.

An unpatched control uses the same compile/link flags and unchanged objects,
replacing only the patched `user_io.cpp`. The candidate and control have
identical ARM architecture, interpreter, required libraries, symbol-version
requirements and architecture attributes. The source-filename change adds no
ABI requirements to this Main pin.

Comparison with its latest committed release, `MiSTer_20260912`, retains the
ARM architecture and existing dependency graph. The candidate adds a direct
`libdl` dependency which that release's pinned Imlib library already requires
indirectly. Both current-source builds import `pow@GLIBC_2.29`, raising the libm
floor above the release's GLIBC 2.4 imports. GLIBCXX 3.4.18 is also newly listed,
but stays below the release's existing GLIBCXX 3.4.21 floor. Neither difference
comes from the filename patch. The board was previously documented as glibc
2.31; its actual library providers still need revalidation before use.

QEMU's dynamic-loader trace resolves the candidate using the compiler sysroot
and pinned MiSTer libraries without invoking Main's entry point or hardware
initialization. An earlier manual loader invocation faulted in QEMU; that log,
the initial ABI comparisons and the final trace remain available. The packet
encoder also passes its ARM/QEMU contract when built with this GCC 10.2.1.
Native target ABI and functional behavior remain unqualified.

`build/studio-main-qualification.json` records the complete Main binary,
unstripped ELF, clean control, original/staged source hashes, compiler archive
digest, build/ABI/loader logs and the preceding Studio/FPGA qualification.
The installed player, launcher, Main and RBF were unchanged. The next native
checks must verify library providers, initial-cart and Main-restart behavior,
source packets and mixed versions, reset/reload/dialogs, and continuous Studio
playback before launcher adoption. The remaining gates below still apply.

## Main/HPS transport restart regression

The new transport harness executes Main's staged filename block, its actual
index/download/data/info helpers, `spi.cpp`, and GPIO SPI acknowledgement loops
against the pinned PICO-8 `hps_io.sv` and `sys_top.v` GPIO synchronization and
acknowledgement logic. DDR writes stall repeatedly. It checks exact cartridge
and source bytes, ticket ownership and source publication before cart readiness.
This covers the real software-to-HPS packet sequence in simulation; it does
not execute Main's complete file-opening/core-lifecycle branches or qualify
the physical GPIO bus.

The first restart cases failed: an interrupted metadata or cart download left
HPS download asserted, so starting fresh metadata without a stop lost the new
filename. Main now selects the unsupported metadata index, sends an abort byte,
stops the old transfer and starts fresh metadata. The abort byte also makes the
accepted older loader reject an incomplete cart: that loader only notices an
index change when another data byte arrives. The revised loader separately
tracks the active cart's exact index and rejects changes even without new data,
including TIC-to-PNG changes. Partial prefixes cannot become ready cartridges.

All eight combinations of normal transfers, interrupted source metadata,
interrupted cart data and delayed acknowledgements pass with current and
accepted older loaders. Cases include native/PNG indices, odd-sized payloads,
UTF-8 and maximum-length paths, missing-source clearing, other-core/slot guards
and preserved staging while an earlier ticket awaits acknowledgement. All eight
also pass through CTest. Cart-loader, DDR and combined integration regressions
pass, including index changes with no further data. Initial failures are
retained alongside final logs and generated harness/input hashes.

Verilator-only staging declares inert names from the disabled PS/2 branch and
suppresses its mixed-assignment diagnostic inside unused UIO/video measurement
blocks. Video clocks and UIO selection are tied off. Download parser and GPIO
handshake logic are unchanged. These adjustments do not alter FPGA build inputs.

The complete companion Main was rebuilt with the restart sequence. Its ABI
still matches the compiled unpatched pin and resolves through the ARM loader
trace. The release comparison still requires `pow@GLIBC_2.29`; native provider
and functional checks remain open. The seed-12 RBF passes all 140 timing checks
with minimum reported slack 0.099 ns, the 44 audio/control crossing checks and
four shared-clock audit groups. Its SHA-256 is
`ec1aadb673176af5b3761431fc0622a09ee0f642ffc24162446472b08a76e637`.
The first seed-11 placement failed cold-corner HDMI setup by 0.274 ns (TNS
-2.151 ns), from the inherited scaler's `o_h_poly_t.g0[11]` into green output
pixel registers. Its RBF, timing summary, critical paths, source and build logs
are retained under `build/hps-transport-seed11-timing-failure/`. The timing
constraints and multicorner checks remain enabled during placement repair.
`build/studio-transport-qualification.json` links the preceding Studio evidence
and archives the new Main, RBF, source and regression evidence. The installed
player, launcher, Main and RBF were unchanged while the device was unavailable.

## Separate working copies for unverified OSD sources

An OSD cart without a byte-verified original source now retains an empty source
path and shows "Save creates a working copy". Its first default Save uses the
browser directory and selects a fresh filename. Occupied names, including
dangling aliases, are skipped with numbered suffixes before the extension.
Publication uses `renameat2(RENAME_NOREPLACE)` so a file appearing after the
name check remains intact. Successful Save records the chosen basename and
path; later saves update that copy independently of browser directory changes.
Verified source saves and explicitly named saves retain their existing behavior.

Tests cover two carts sharing the generic working name, preservation of an
earlier copy and a dangling alias, recovery before first Save, later Save to
the chosen copy, and a regular file created precisely before publication. A
rejected no-replace operation leaves the editor modified and unbound; the
operation can be retried. Native filesystem support and physical durability
remain unqualified. As with existing saves, a failure after rename can leave a
new file committed while the editor remains modified; retrying an unbound
first Save may create another copy. File-name search is bounded to 9,999 names.

The Studio binaries are rebuilt for host, AddressSanitizer and ARM. Main and
the seed-12 RBF are unchanged from the transport milestone above. This work
does not adopt Studio in the launcher or change the unavailable device.
All 43 host and 43 AddressSanitizer Studio checks pass. Four ARM/QEMU contracts
and five paced ARM source scenarios pass. The cross-compiled CTest tree does
not register paced scenarios; its empty-match log is retained, and the final
five cases run through the existing Python transport fixture. Initial builds
reported subsecond future timestamps on generated dependency files; follow-up
builds complete without those warnings, and the tested binaries are hash-bound.
Evidence and candidate source/binary hashes are recorded in
`build/studio-working-copy-qualification.json`; broader native gates remain open.

## Retain the published Save destination after a reported sync failure

The first injected directory-sync failure reproduced a working-copy bug: the
file reached disk, but Studio retained an empty source path. A later default
Save would choose another copy. `cart_write` now distinguishes publication
from successful durability checks. After a successful rename, the Studio Save
helper records the chosen basename/path even if directory sync or close later
reports failure. Save still reports an error and does not update the saved
hash. A pre-publication failure retains the prior destination.

The regression covers an unbound first Save, an explicitly named new
destination, retention of the earlier file, retry without another numbered
copy, and the accepted private checkpoint after a failed Save. Worker
replacement after that acknowledged failure preserves both the new destination
and modified editor state; the next Save updates the same copy. Initial failure
logs and the parent source are retained with the rebuilt candidates in
`build/studio-sync-qualification.json` and `build/studio-sync-fixtures/`.
All 43 host and 43 AddressSanitizer Studio checks pass, along with four
ARM/QEMU contracts and five paced ARM source-selection scenarios. The generated
Studio adapters are unchanged; only the Save helper and its API documentation
change in production sources. Rebuilt outputs, source hashes and five ARM
snapshots are bound to the new qualification record.

This closes the reported-error retry gap in the preceding working-copy
milestone. It does not reconcile a worker killed or hung after rename but
before an acknowledged reply. That case can still leave a committed file and
an older checkpoint; physical filesystem support, shutdown waits and SD-card
durability also remain open. Main, the RBF and the installed device are unchanged.

## Maximum cartridge copy ownership and transport departure

The new guarded-copy fixture reproduced five baseline failures: an already
departed core, session replacement during the copy, a changed transfer ticket,
core departure during the copy, and core departure while rejecting an oversized
transfer. The earlier implementation could expose an old source path on failure
or acknowledge rejected staging after departure. `tm_backend_cart_source` now
clears valid output arguments before checking the session, holds the filename
in private memory, and renews session/ticket ownership before every staging ACK.
The caller receives payload and source outputs only after that check succeeds.

`maximum_cart_copy` places a protected page immediately after the mapped region.
It checks 4, 4,097, 65,535, 4 MiB minus one and exactly 4 MiB payloads, plus a
4 MiB plus one rejection. Each accepted transfer checks every copied byte and a
255-character filename after immediately overwriting staging. Fault injection
checks empty outputs and no ACK after lost ownership. A separate managed worker
receives a valid 4 MiB cartridge through a deliberately deferred request: caller
payload and path are overwritten before request delivery, and the worker still
loads the original private data and verified source. This uses the real worker
process and shared-memory request path.

The final checks pass: 43 Studio tests and two transport contracts on both host
and AddressSanitizer, four Studio and two transport contracts under ARM/QEMU,
five paced ARM source-selection scenarios, and five host player-service
scenarios. Initial instrumentation and cleanup failures are retained separately
from the five reproduced production failures and successful final runs.
`build/cart-copy-qualification.json` binds source and binary hashes, logs,
per-size timing samples, generated adapters and seven ARM snapshots in
`build/cart-copy-fixtures/`. It updates the current local Studio gate without
altering preceding qualification records. Main and the RBF remain unchanged.

Timing samples measure simulated RAM and private worker-request copies. The
backend session is established by the fixture; the IPC measurement excludes
request delivery and worker decoding/loading. Host or QEMU measurements do not
qualify physical DDR timing, scheduler bounds or continuous audible playback.
Native 4 MiB copy timing and audio queue impact remain hardware gates. The
installed player and launcher were unchanged while the user was away.

## Recover a Save published before the worker's final acknowledgement

The final project regression reproduces the lost working-copy destination with
the preceding archived `studio_rom.c` and `studio_session.c`: rename succeeds,
the worker dies before its reply, and the replacement restores an empty source
path. Its input hashes, compiler/link commands, executable and failure log are
retained under `build/studio-publish-baseline/`. The initial reproduction is
also retained. This extends the earlier reported-directory-sync repair to
unacknowledged publication.

Before closing a flushed temporary file and renaming it, the managed Save
writer records the request sequence, prior and proposed names, resolved target,
device/inode/size, and MD5 content/cartridge digests in its private shared
transport. It publishes the completed intent with a release store. After the
old worker is reaped, its replacement compares that intent with the restored
acknowledged cart and prior name, opens the target nonblocking, checks that it
is the prepared regular file, verifies its digest and stable read metadata,
and checks that the logical Save path still names that inode. Only a matching
publication supplies a recovered destination in the replacement worker's
normal complete ACK. It preserves the saved hash and editable checkpoint;
Save remains modified until a successful retry. The next request clears old
intent, and sequence/name/cart checks reject stale records.

Verification and file reads occur in the bounded replacement worker, leaving
the frontend's asynchronous poll path independent of filesystem reads. A
post-rename stall is tested through `poll(..., 0)`: getters retain the old
checkpoint throughout PENDING and change only after RECOVERED. Other cases
cover SIGKILL immediately after rename, death before rename, same-inode and
same-size content modification, replacement by a foreign cart or FIFO, an
explicitly named uppercase PNG Save, preservation of the old source, same-file
retry without another numbered copy, and rejection of stale intent after
another OSD selection. Foreign files remain untouched.

The full 43-test Studio suite and two transport contracts pass on host and
AddressSanitizer; four Studio and two transport contracts pass on ARM/QEMU,
with all five paced ARM source-selection scenarios. The final strengthened
same-size content-mutation project check also passes on all three platforms.
`build/studio-publish-qualification.json` and `build/studio-publish-fixtures/`
bind the current source, binaries, baseline and logs. The earlier copy
measurements are refreshed from the current transport runs. Generated Studio
adapters, Main, the RBF and player binaries are unchanged, and no device was
contacted while the user was unavailable.

This recovers destinations whose saved cart matches the last acknowledged
checkpoint. It does not adopt unacknowledged cart changes or mark a Save durable
after a worker interruption. Intent exists only in the live supervisor's
memfd; frontend death, power loss, orphaned temporary files and physical SD
durability remain separate work. Native filesystem support, playback, library
providers and launcher adoption remain open.

## Reclaim recorded Save temporaries after interruption or cancellation

The interrupted-write regression reproduced a private `.tic80-cart-*` file
remaining in the source directory after worker timeout. The managed writer
now records the canonical temporary path, request and device/inode immediately
after creation and before permissions, writes or sync. Registration verifies
that the pathname and descriptor name the same regular file. Completed intent
is released atomically in the existing supervisor transport.

After reaping an interrupted worker, its replacement opens the recorded name
with no-follow and nonblocking flags, verifies that the descriptor and current
name still match the recorded regular inode, and removes that temporary.
It does not enumerate directories. Ordinary failed-write cleanup now checks
inode ownership too. Other same-prefix files, replacement inodes, symlinks and
FIFOs are retained. A completed rename leaves no temporary name to remove;
the preceding Save destination recovery still verifies and binds the published
file independently.

Closing a pending Save also needs cleanup, without starting another editor.
After cancelling/reaping the old worker, Close runs a separate cleanup-only
worker and waits for its acknowledgement with a one-second userspace budget.
That worker does not initialize Studio or run a cartridge. Kernel-blocked I/O
and existing background-save shutdown waits remain outside this guarantee.

Project regressions cover timeout before the first write, SIGKILL before any
bytes, SIGKILL after a partial write, death immediately before rename, pending
Close, preservation of a same-prefix decoy, and replacement of the tracked
temporary by a foreign cart, symlink or FIFO. Retries retain the edits and
correct destination. The archived production Save/session sources reproduce
the interrupted-write leftover; a separately labelled control restores only
the previous pending-Close branch and reproduces its leftover after passing
the recovery cases. Initial cancellation probes used the directory-sync marker
instead of the write-stall marker and timed out waiting for the intended stall;
those harness failures are retained separately from the corrected control.

All 43 Studio tests and two transport contracts pass on host and
AddressSanitizer, four Studio and two transport contracts pass under ARM/QEMU,
and all five paced ARM source-selection scenarios pass. Per-platform run
manifests bind the compiled inputs and executables before/after those checks.
Sources, logs, baseline/control binaries, refreshed simulated copy timings and
seven ARM snapshots are recorded in `build/studio-temp-qualification.json`
and `build/studio-temp-fixtures/`. Main, the RBF, generated Studio adapters and
player binaries are unchanged; no device was contacted.

This reclaims registered temporaries while the supervisor survives. The gap
between `mkstemp` and complete registration, supervisor death, power loss,
cleanup I/O errors and physical storage/durability remain open. These results
do not qualify an SD filesystem or bound kernel I/O waits. Native playback,
source/reset integration, library providers and launcher adoption remain work.

## Preserve code undo/redo through worker recovery

The final supervision regression reproduces lost redo with the preceding
archived production supervisor: paste two edits, undo the second, kill the
worker, recover, then redo. Text and cursor recovery alone retained the first
edit but discarded the redo chain. The final test linked with that archived
supervisor fails at the post-recovery text comparison. Its source inputs,
common-library hashes, compiler/link commands, executable and failure log are
retained in `build/studio-history-baseline/`.

The pinned history implementation is staged with the same linked XOR-diff
operations and an added change revision. When code history changes, the worker
streams its current packed state, historical state, diff ranges and current
list position into a memfd. It seals the completed file against writes, growth,
shrinkage and changes to its seals. A normal frame acknowledgement passes the
descriptor through SCM_RIGHTS. Unchanged history sends no descriptor, including
ordinary RUN ticks. The supervisor validates the sealed envelope and request,
then replaces its previous history descriptor only after validating the whole
frame checkpoint. Pending or rejected acknowledgements retain the previous
history. Recovery passes that descriptor to the new worker; a private parser
validates all ranges and lengths before modifying its code state.

The receive buffer covers the full 253-right Linux message limit with extra
capacity for ABI control-header conversion. That limit is defined in the
[Linux 6.18 SCM header](https://raw.githubusercontent.com/torvalds/linux/v6.18/include/net/scm.h).
The supervisor accepts at most one history descriptor and closes every right
in an invalid reply. Normal Close explicitly clears the preceding frame's
history-change flag before acknowledging shutdown.

Recovery tests continue undo and redo after SIGKILL at an undone position and
after a hung RUN. Standalone serializer tests retain a 257-edit chain, restore
its undone position, redo forward and undo back to the original data. They also
verify producer ownership, immutability, branching after undo, corrupt metadata
and diff ranges, truncation, trailing bytes, oversized sparse files and non-file
descriptors. Failed restoration leaves the supplied data unchanged. Protocol
faults cover missing rights, two/eight/253 rights, wrong reply opcode, an
unsealed file and a wrong request sequence; each retains accepted edits and
undo while returning the descriptor count to its baseline after Close.

The initial full suites exposed a stale history-change flag in the close-only
reply, which broke immediate Close after recovery or load. Initial ARM probes
also needed the test wrapper to cover glibc's time64 sendmsg symbol. Once that
fault was actually injected, QEMU 8.2 leaked four rights truncated by the small
receive buffer. A diagnostic executable/log retains that observation; receiving
the complete bounded set allows explicit cleanup. These initial failures are
retained separately from the final qualification runs.

This qualifies code history recovery locally. Snapshots have a 512 MiB envelope
limit; native worker memory limits still apply. Native serialization latency,
memory use, editor/audio pacing, other editors' undo histories, bookmarks and
broader modal/view state remain open. Supervisor death or power loss does not
retain these in-memory checkpoints. No MiSTer was contacted or changed.

All 44 Studio checks and two transport contracts pass on host and
AddressSanitizer, five Studio and two transport contracts pass under ARM/QEMU,
and all five paced ARM source-selection scenarios pass. Before/after run
manifests bind the compiled inputs and executables. The source, generated
adapters, binaries, negative control, diagnostic failures, refreshed simulated
copy timings and eight ARM snapshots are recorded in
`build/studio-history-qualification.json` and `build/studio-history-fixtures/`.
The current local gate advances to that record. Main, the RBF and player
binaries are unchanged; the installed launcher remains the player.

## Preserve every bank's asset undo history

The preceding code-only supervisor retained sprite pixels and drawing
preferences after a hung RUN, but recreated an empty sprite undo chain.
The final input-driven supervision regression, linked with that archived
production supervisor and its compatible single-code history helpers,
reproduces the loss in bank 7. Compiler/link commands, common-library and
source hashes, the executable and its failure log are retained in
`build/studio-assets-baseline/`. The initial reproduction is retained too.

The managed worker now exports one sealed version-2 bundle containing all
41 histories: code, then sprite, map, sample, waveform and music for each of
eight banks. Its bounded table records each member's expected size, offset
and byte length. Each member retains the preceding pointer-free history
format, including current state, historical state, XOR differences and list
position. The worker uses the latest revision across all histories to detect
changes; ordinary unchanged RUN frames still send no history descriptor.
The supervisor validates the sealed bundle table and request before adopting
it with the complete frame acknowledgement.

Recovery privately parses every member before replacing any history or
external data. A malformed final member discards all earlier parsed copies.
Only code's private packed editor state receives its captured current bytes.
Asset histories attach to the restored acknowledged cartridge without
overwriting its asset data. This distinction matters when RUN has changed
cartridge assets without adding editor undo nodes: recovery keeps that
acknowledged change, while an explicit later Undo uses the earlier historical
state, matching the pinned editor operations.

The input-driven test creates two edits in every bank's sprite, map, SFX
sample, waveform and music editor. It undoes the second edit, kills the worker,
recovers and exercises redo and both earlier undo steps. Every comparison
checks the whole cartridge, including all other assets. Once all 40 histories
contain edits, a hung RUN recovers them together and the test revisits each
bank/editor to undo and redo independently. A subsequent acknowledged Lua RUN
uses `mset` and `sync(4,7,true)` to replace bank 7's map, then dies. Recovery
preserves that full map; only explicit Undo restores the preceding editor
state. The map expectation accounts for RUN initializing RAM from bank 0.

Standalone bundle tests cover all 41 list positions, immutable ownership,
retaining external asset bytes, corrupt/overlapping tables, wrong member sizes,
reserved fields, oversized/truncated/trailing files and malformed final-member
metadata/ranges. Rejection leaves all original history pointers and supplied
buffers unchanged. Existing single-code and ancillary-message fault contracts
continue to pass, including full cleanup of rejected 253-right replies.

Initial UI harness failures are retained separately: the first fixture clicked
editor tabs while still in the console, then the map selector was held for one
frame rather than the two it needs. The first RUN comparison expected a
single-byte update although `sync` copies the full map. Corrected input and
expected cartridge data keep the strict comparisons intact.

The new bundle retains the 512 MiB total envelope limit and the native worker
memory limit. Native memory use, serialization latency and editor/audio pacing
remain unqualified, as do bookmarks and broader modal/view state. Supervisor
death, power loss and physical storage durability remain separate gates.
The MiSTer and installed launcher were untouched while the user was away.

All 46 Studio checks and two transport contracts pass on host and
AddressSanitizer; seven Studio and two transport contracts pass under ARM/QEMU,
plus all five paced ARM source-selection scenarios. Per-platform manifests
bind the compiled inputs and executables before and after testing. Network
fixture sources and executables are also enrolled in this record. Sources,
generated adapters, binaries, the negative control, initial failed probes,
refreshed simulated copy timings and nine ARM snapshots are recorded in
`build/studio-assets-qualification.json` and `build/studio-assets-fixtures/`.
Main, the RBF and player binaries remain unchanged.

## Retain cleared code bookmarks across recovery

The pinned code editor adds history when toggling a bookmark, but its
Ctrl+Shift+F1 clear-all operation changes packed CodeState flags directly.
Without a revision change, the managed worker sent no replacement history
checkpoint. A subsequent worker termination restored the preceding marks even
though the last acknowledged editor frame had cleared them.

The build now stages the pinned code editor after checking its complete source
hash, retaining its MIT license. Clear-all advances the snapshot revision only
if it removes a mark. It does not add an undo node, change cartridge text or
mark the cartridge modified. Clearing an already empty set remains unchanged.

The new input-driven default-shortcut regression sets keyboard bookmarks on
two lines, checks next/previous navigation and wrapping after SIGKILL, clears
the marks, and checks that recovery keeps them empty. Undo and Redo then follow
the original toggle history rather than an extra clear-all step. The test also
covers clear-all before a hung RUN, a mouse margin bookmark, and repeated
clear-all followed by termination. Cartridge text and modification status
remain unchanged throughout.

The final test is separately linked with the original pinned code editor and
current common libraries. That negative control passes bookmark recovery and
the clear operation before termination, then fails because the first old mark
reappears after recovery. The original source, object, executable, actual
compiler/link commands, input hashes and failure log are retained in
`build/studio-bookmarks-baseline/` and the qualification archive.

All 47 Studio checks and two transport contracts pass on host and
AddressSanitizer; eight Studio and two transport contracts pass under ARM/QEMU,
plus all five paced ARM source-selection scenarios. Per-platform manifests
bind the compiled inputs and executables before and after testing. The previous
all-bank history, malformed-bundle and ancillary-message contracts still pass.
The new generated code-editor adapters preserve every preceding generated
adapter unchanged. Sources, binaries, logs, simulated copy timings and nine ARM
snapshots are recorded in `build/studio-bookmarks-qualification.json` and
`build/studio-bookmarks-fixtures/`.

This qualifies default-shortcut bookmark recovery locally. Physical keyboard,
native memory/serialization/audio pacing, Vi insert/modal state and broader
editor views remain open. The MiSTer, installed launcher, Main, RBF and player
binaries were untouched while the user was unavailable.

## Preserve Vi modes and unfinished insert groups

Recovery previously recreated the code editor in Vi Normal mode even if the
last acknowledged frame was in Insert, Select or a seek mode. The final Vi
regression linked with the preceding generated Studio adapter reproduces that
loss: Insert typing works before SIGKILL, but typing `l` after recovery fails
to append the expected character.

The code-view checkpoint now carries a bounded Vi mode alongside cursor,
selection, preferred column and scrolling. Its validator rejects out-of-range
modes before changing editor state. Recovery selects the home editor before
restoring the code view because the pinned `setStudioMode` resets Vi mode.
All five pinned modes survive: Normal, Insert, Select, Seek and SeekBack.

The pinned editor defers creating undo nodes until Insert ends. While Insert
is active, managed edit and bookmark operations now pack the current CodeState
and advance the snapshot revision without adding an undo node. Pending text
and bookmark flags therefore travel with the complete frame acknowledgement.
Leaving Insert retains the original single undo group, including edits before
and after recovery. The second final negative control uses the preceding
generated code editor with the current mode checkpoint: continued typing
passes, but a mouse bookmark acknowledged during Insert disappears after
SIGKILL. Both controls retain actual compiler/link commands, source and library
hashes, original generated adapters, objects, binaries and failure logs.

The new supervised input contract exercises unfinished typing and a mouse
bookmark across SIGKILL, the resulting single Undo/Redo step, a second distinct
insert group across explicit pause, Visual selection extension and cut/Undo,
forward and backward seek across worker termination, and continued Insert
typing after a hung RUN. The standalone view contract accepts all five modes
and rejects invalid mode values atomically. Its console/editor contract now
also runs under ARM/QEMU, including text input, clipboard, bank 0/7 sprite
editing and native/PNG/project save-reload.

All 48 Studio checks and two transport contracts pass on host and
AddressSanitizer. Nine Studio and two transport contracts pass under ARM/QEMU,
plus the standalone editor contract and all five paced source-selection
scenarios. The updated standalone test source is newly enrolled among compiled
inputs, with a separate before/after input, object, executable and library hash
manifest on each platform. Existing all-bank histories, default bookmarks,
malformed-bundle and ancillary-message fault contracts continue to pass.
Sources, generated adapters, binaries, both negative controls, refreshed
simulated copy timings and ten ARM snapshots are recorded in
`build/studio-vi-qualification.json` and `build/studio-vi-fixtures/`.

This qualifies Vi modal and grouped-insert recovery locally. Native physical
keyboard behavior, memory/serialization/audio pacing and broader search,
replace, sidebar and other editor views remain open. These checkpoints do not
survive supervisor death or power loss. The MiSTer, launcher, Main, RBF and
player binaries remain untouched while the user is unavailable.

## Retain code search, replace, goto and sidebar views

The previous code-view checkpoint retained Vi mode and text positions but
omitted the code editor's active view and popup state. Its acknowledged FIND
query therefore returned to ordinary editing after worker termination. The
final regression with an ABI-compatible preceding checkpoint verifies the
actual `alp` query and selected match before SIGKILL, then reproduces the loss:
typing the next query character changes cartridge text after recovery.

The code checkpoint now includes all seven pinned view modes, the full bounded
popup buffer, saved ESC origin selection, replacement-stage offset, goto line,
sidebar size/index/scroll and popup animation identity/tick/positions. Recovery
rebinds offsets to this worker's buffers and animations to its own callbacks
and targets. Bookmark and outline lists are rebuilt privately from the accepted
code and history; their count, selected row and scroll must match before any
view state is committed. Invalid metadata and failed preparation leave the
existing editor untouched. Active mouse gestures remain released.

The supervised regression continues a FIND query and navigates matches after
SIGKILL, then verifies ESC returns to the original cursor and selection. GOTO
digits survive and accept further changes; a full popup of nines safely clamps
to the final line. Bookmark selection and outline filtering survive recovery,
including nonzero scrolling in 24-entry and 25-entry lists. Opening and closing
popup animations complete after interruption, and the selected Drag tool can
still be toggled back to Edit. In a Vi session, both REPLACE stages retain the
search/replacement strings and offset; the intended bulk edit and Undo/Redo
continue to work. These are input-driven Lua editor tests, not coverage of
every scripting runtime's outline parser or every shortcut combination.

The pinned REPLACE transition also appended its six-character ` WITH:` label
without checking the remaining 34-byte popup capacity. The preceding editor
with the current recovery helper aborts in fortified libc when the final
maximum-length search regression presses Enter. The worker recovers rather
than completing that transition. The revised operation retains an overlong
search and leaves the user in REPLACE so they can shorten it. A 27-character
search plus label and terminator fits exactly; the fixture recovers at that
boundary and performs an empty replacement. GOTO's decimal parser now
saturates at the last one-based line instead of overflowing `atoi`.

Both negative controls retain the original generated adapters, compatibility
adaptations, actual compiler/link commands, sources, objects, executables and
failure logs. The preceding checkpoint receives canonical defaults for the
new record fields while retaining its old export/restore behavior. The
preceding code editor receives only the current private recovery helper so it
can link against the expanded interface; its original unbounded label append
is retained. These adaptations and their limited purpose are recorded.

The first full host suite exposed a separate transport-fixture race: the
frontend could finish after the fake core notified departure but before the
recorder appended its snapshot. A 250 ms scheduling gap after notification
reproduces the missing-snapshot assertion with a clean 130-frame departure and
the snapshot present after the recorder's final join. The fixture now joins
its recorder before inspecting the snapshot. The identical delayed case passes
with the existing DDR, audio, frame and worker-cleanup assertions preserved.
The initial failed suite, initial passing sanitizer suite, unchanged-fixture
probe and delayed original/revised recorder controls are retained.

All 49 Studio checks and two transport contracts pass on host and
AddressSanitizer. Ten Studio and two transport contracts pass under ARM/QEMU,
plus the standalone editor contract and five paced source-selection scenarios.
Standalone contracts on all three platforms check malformed offsets, labels,
modes, animations, sidebar metadata and goto lines reject atomically, and a
valid replacement pointer is rebound correctly. The new private adapter and
the changed transport fixture are enrolled as compiled/tested inputs; the ARM
suite does not execute that host/sanitizer-only transport fixture. Source,
generated adapter, binary, negative-control and recorder evidence, refreshed
simulated copy timings and ten ARM snapshots are recorded in
`build/studio-code-views-qualification.json` and
`build/studio-code-views-fixtures/`.

Native keyboard behavior, memory/serialization/audio pacing, cross-mode
shortcuts, remaining asset editor preferences and broader project/runtime
coverage remain open. Supervisor death, power loss and physical storage
durability remain separate gates. The MiSTer, launcher, Main, RBF and player
binaries were untouched while the user was unavailable.

## Route code-view Escape before the underlying Vi command

The preceding global shortcut dispatcher returned early for every non-Normal
Vi mode, including when a toolbar search, goto or sidebar was open. Escape
therefore left those views stuck. Its Drag escape also changed straight to Edit
when idle; the editor tick then saw the same Escape and committed an unfinished
Vi Insert group. During the opening animation, that Drag escape did nothing.

The staged Studio adapter now leaves Escape to the ordinary Vi handler only
when the code editor is in Edit. A toolbar view owns its first Escape. Drag
uses the existing hide transition before returning to Edit, releases its
mouse gesture and suppresses new dragging while closing. Ordinary Vi Escape
and the following Normal Escape to console retain their original behavior.
The pinned upstream checkout remains unchanged. Generation verifies the full
pinned Studio and code editor source hashes and the replaced fragments.

`studio_modal_routing` drives real toolbar clicks and keyboard input through
the supervised worker: all five toolbar views with all five Vi modes, followed
by SIGKILL, recovery, cancellation and a working underlying Vi command. Each
Insert case checks typing before and after cancellation is one undo/redo step;
Select extends and cuts its original selection, and both seeks still find
the intended character. Five further cases cancel during opening and terminate
the worker during closing. A held-mouse case checks the closing Drag view does
not move the scroll or caret, and explicit pause and hung RUN cases retain
search cancellation and the unfinished Insert group.

Two negative controls link the final regression fixture separately against
each unmodified preceding generated adapter. The prior Studio adapter leaves
Insert/FIND open after Escape. With the revised dispatcher and prior Code
adapter, the four Insert popup/sidebar cases pass, but Drag also consumes the
same Escape as Vi Insert and commits it. No compatibility shim is required:
the public checkpoint and private view-helper ABI are unchanged.

All 50 Studio checks and two transport contracts pass on host and
AddressSanitizer. Twelve Studio checks and two transport contracts pass under
ARM/QEMU, along with five paced source-selection scenarios. The standalone
editor contract is included in all three suites.

The qualification record and source, adapter, ARM executable, negative-control
and test evidence are retained in `build/studio-modal-qualification.json` and
`build/studio-modal-fixtures/`. Initial parallel runs hit three existing
transport timing assertions: host offline reconnect audio-clock stall,
sanitizer failed-save audio-clock stall and sanitizer backend deadline/timeout.
Their complete logs are retained. Serial reruns use the same compiled inputs
and executable hashes and preserve every assertion; this does not establish
the exact cause of the initial scheduling failures or native audio continuity.

Native keyboard behavior, editor memory/serialization/audio pacing, remaining
cross-mode shortcuts, asset preferences and broader cartridge/runtime coverage
remain open. The MiSTer is unavailable; the installed player, launcher, Main
and RBF were untouched. Studio adoption and hardware qualification remain
separate gates.

## Batch history checkpoint writes without dropping undo

An isolated full-size workload contains the actual code/sprite/map/sample/
wave/music history buffer sizes for all eight banks: 41 histories with 256
edits each, 10,537 nodes including the initial sentinels, and different undone
positions. The preceding serializer performs 21,158 writes to emit its
3,241,512-byte checkpoint. Those repeated small range/payload writes consume
time on every accepted editor change, even when most undo deltas are one byte.

The serializer now shares a fixed 64 KiB staging buffer across the entire
snapshot. Large blocks bypass the buffer after any queued bytes are flushed;
small headers, ranges and deltas are coalesced. It finishes all writes before
sealing the descriptor. Both the standalone v1 history and v2 bundle retain
their exact formats, sizes, ordering and every undo node. This change adds
bounded stack storage rather than allocating a complete snapshot in memory.
History restore and the accepted publication protocol are unchanged.

`studio_history_io` compares all seven emitted checkpoints against an
independently constructed byte stream, checks request/envelope validation,
restores every undone position and walks every history through complete
redo/undo to the original bytes. Interrupted and repeated short writes must
produce the same bundle and standalone snapshots. Early and final bundle
write errors, and a standalone write error, must reject the snapshot and
reclaim its descriptor. The syscall budget verifies coalescing without
using a simulated elapsed-time limit as a native performance guarantee.

The qualification gate runs all 51 Studio checks plus two transport contracts
on host and AddressSanitizer, 13 Studio checks plus two transport contracts
under ARM/QEMU, and five paced source-selection scenarios. The unmodified
preceding writer is also linked separately against the final fixture on all
three platforms. It passes the same format, fault and complete undo checks,
then fails the syscall budget with 21,158 writes. The revised workload emits
the identical 3,241,512 bytes using 21 writes. Timing and peak process RSS are
recorded for each environment; they do not qualify native MiSTer latency,
worker memory or audible continuity. A separate paired run launches each
control and revised fixture through the same subprocess runner. Peak RSS is
process high-water data, including launcher/emulator effects; it does not
isolate the Studio worker's resident memory.

Sources, generated adapters, build/test inputs, unchanged Main/RBF/player
artifacts, negative-control binaries and eleven ARM fixture snapshots are
retained in `build/studio-history-io-qualification.json` and
`build/studio-history-io-fixtures/`. Builds complete before the paced suites
run serially. Native workload profiling and the remaining memory, editor,
hardware and launcher-adoption gates remain open. The MiSTer was untouched
while the user was unavailable.

The initial ASAN closure build retained a sub-second NTFS dependency timestamp
warning for the new fixture. A later terminal build is clean, and its receipt
verifies every tested source/executable and negative-control link input remains
identical. The warning log is retained; no test rerun or assertion change was
needed for that build-metadata check.

## SFX binding contract across all fourteen runtimes

The scalar SFX fixture compares all fourteen language bindings with independent
direct C API calls. Its seeded effects have different preset speeds, pitches,
waveforms and volume envelopes. Thirty-six frames per case exercise explicit
positive, negative and zero speeds, pitch, duration, channel, volume and stopping
sounds. Each frame compares the active SFX state and all 1,600 stereo samples
from both direct execution and the isolated cartridge worker. Unused pitch
values on a stopped channel are ignored; its PCM must still match exactly.

Two additional Wren cases exercise left-only and right-only volume lists, with
the preset speed omitted or an explicit speed supplied. A wrapper checks the
VM slot contract at each list read, in addition to checking exact PCM. The
preceding Wren binding reads destination slot 6 when only six slots exist.
The staged repair reserves one extra slot while retaining the original arity
for optional-argument handling. Ruby's preceding binding overwrites a supplied
speed with the effect preset; the staged repair uses the preset only when the
sixth argument is absent. Both repairs leave the pinned checkout untouched.

The ARM worker suite reaches its parent-death test but QEMU rejects its
`PR_SET_CHILD_SUBREAPER` call. QEMU's [8.2.2 syscall handler](https://github.com/qemu/qemu/blob/v8.2.2/linux-user/syscall.c#L6070)
explicitly returns `EINVAL` for this operation. An external native Linux
supervisor instead runs the unmodified `--orphan-probe`, kills the probe
process and adopts/reaps its execution worker. The worker still uses its real
`PDEATHSIG` path. The original CTest failure remains in the evidence; this
supplemental check does not imply a fully passing ARM CTest invocation.

All 76 host and 76 AddressSanitizer checks pass. The worker fault test
deliberately sends `SIGSEGV` to a child; its single sanitizer signal report
is retained alongside successful containment and reaping. The SFX cases
produce no sanitizer errors. The ARM invocation completes
31 of 32 CTest cases, with the worker suite passing its runtime and fault
checks before that unsupported operation. The external parent-death check
passes on host, ASAN and ARM/QEMU, followed by all five paced ARM source-load
scenarios. Six separately linked controls reproduce the original Ruby speed
and Wren slot failures across all three builds; the repaired bindings pass
the same final fixture. Exact sources, generated adapters, test/build logs,
binary hashes and ARM snapshots are retained in `build/sfx-qualification.json`
and `build/sfx-fixtures/`. Local player binaries change with these shared
binding repairs; the installed player, Main and RBF remain untouched.

This is a bounded audio API contract, not complete cartridge compatibility.
Other omitted-argument defaults, malformed notes and argument bounds, music,
FFT and external I/O require further coverage. Native playback and Studio
adoption remain open. The MiSTer is untouched while the user is unavailable.

## SFX ID/channel boundaries and numeric error reporting

The next corpus covers twenty Janet/WASM calls: effect IDs 0, 63, 64 and
`INT_MAX`; channels 0, 3, 4, -1 and `INT_MAX`; and negative stop IDs -1,
-2 and `INT_MIN`. Each case first starts a valid sound, then makes the
boundary call. A wrapper rejects any engine call with an invalid nonnegative
effect ID or channel, before an unsafe memory access can occur. Direct
execution runs in a child process, so a malformed error formatter cannot
terminate the harness. The same cart runs in the real isolated worker with
complete stereo PCM parity, while an independent Lua worker keeps advancing.

The preceding bindings fail seven malformed-call cases. Janet allows channel
4 to reach the engine and uses `%s` for numeric IDs/channels in its panic
messages, causing signal failures during error reporting. WASM passes invalid
nonnegative effect IDs to the engine. Thirteen valid boundary, stop and
existing no-op cases already pass the preceding fixture. Staged repairs use
Janet's integer format, reject channels outside 0–3 and form its preset
pointer only for a valid nonnegative ID. WASM traps an invalid effect ID,
matching its existing music-ID guard. Negative stop IDs remain accepted;
valid-effect calls with invalid WASM channels retain their existing no-op.
All twenty cases pass the repaired host fixture. The pinned runtime checkout
is unchanged. Broader argument bounds, note validation, omitted defaults and
native playback remain open.

An initial ARM regression run failed its immediate post-`SIGSEGV` worker
death/snapshot assertion. The diagnostic did not wait for termination after
queuing the signal. It now observes the child's terminal status with a bounded
`waitid(..., WNOWAIT)` before asking the supervisor to report the death.
It leaves the child waitable, retains the original last-snapshot and cleanup
assertions, and checks the expected signal (or sanitizer-handled exit).
The preceding source, ARM fixture and failure log are retained. The revised
ARM worker check reaches the separately documented unsupported subreaper call.
Production VM code is unchanged by this diagnostic repair.

A subsequent ASAN run retained a virtual-DAC underrun/audio-clock-stall failure
in the ten-second reload-timeout scenario. A profiled rerun of the unchanged
clock model and original assertions passes with zero underruns and the expected
initialization-timeout error. Its timing history retains 512 samples per clock.
The earlier event's root cause is unproven; logs and exact-artifact receipts
remain in the qualification evidence. Native audio pacing is still unqualified.

The final qualification completes 77 host and 77 ASAN checks, 32 ARM CTest
cases plus the worker checks before QEMU's unsupported subreaper operation,
three external parent-death checks and five paced ARM source-load scenarios.
All twenty boundary cases and the existing sixteen audio cases pass on each
platform. Twelve separately linked preceding-binding calls reproduce the
numeric-diagnostic crashes or invalid engine calls; repaired bindings pass
the same final cases. Sources, generated adapters, diagnostics, binary hashes
and ARM snapshots are retained in `build/sfx-bounds-qualification.json` and
`build/sfx-bounds-fixtures/`. The installed player, Main and RBF are untouched.

## Effect-zero defaults and Lua-family stereo volume

`tests/api_sfx_defaults_test.c` exercises ten sound scenarios in MiniScript,
Lua, MoonScript, YueScript and Fennel. Presets 0, 1 and 63 have distinct notes,
octaves and speeds. Calls cover omitted arguments, a null note with an explicit
duration, explicit note and speed overrides, stereo volume and stopping a
previously started sound. Each cart runs for 24 frames in both direct and
isolated execution. An independent C plan checks channel state and all 38,400
stereo samples per case, plus the cartridge's advancing persistent counter.

The initial twenty-case Lua/MiniScript corpus fails six cases. MiniScript's
`index > 0` condition skips preset pitch for effect 0; the engine already
resolves its default speed sentinel correctly. The staged binding uses
`index >= 0`. Lua's stereo table path assigns `lua_rawgeti`'s return value,
which is the pushed element's type tag. Both numeric volumes become 3. The
staged Lua API reads the pushed number before popping it, preserving a muted
left channel and full-volume right channel. MoonScript, YueScript and Fennel
share this API implementation and are included in the expanded corpus.
The upstream checkouts remain unchanged.

The [upstream SFX documentation](https://github.com/nesbox/TIC-80/wiki/sfx)
describes playing the note assigned in the editor when no note is supplied.
The pinned bindings and engine establish the preset-speed behavior exercised
here. This corpus does not qualify every runtime's defaults, negative numeric
note sentinels, malformed note strings or native playback.

All fifty cases pass on host, AddressSanitizer and ARM/QEMU builds. Full local
regressions complete 78 host and 78 sanitizer checks, plus 33 ARM CTest cases;
the remaining ARM worker case reaches QEMU's unsupported subreaper operation.
External parent-death checks pass on all three builds, as do five paced ARM
source-load scenarios. Twenty-seven separately linked old-binding calls
reproduce the failures and pass with the repairs using the same final fixture.
Exact sources, generated adapters, test/build logs, binary hashes and ARM
snapshots are retained in `build/sfx-defaults-qualification.json` and
`build/sfx-defaults-fixtures/`. The installed player, Main and RBF remain
untouched; native testing and the broader completion gates remain open.

## Named-note parsing and error preservation

`tests/api_sfx_notes_test.c` contains 288 generated cart cases in Lua,
JavaScript, MoonScript, YueScript, Fennel, Scheme, Squirrel, Python, Wren,
Janet, Ruby and MiniScript. Fifteen cases per language cover all twelve
supported note names and octave edges 0, 1 and 8. Nine cover unknown/lowercase
names, unsupported sharps, flats, invalid octaves and empty/short/long names.
WASM and Forth use numeric note/octave arguments and are outside this string
corpus. The [upstream note syntax](https://github.com/nesbox/TIC-80/wiki/sfx)
specifies uppercase note pairs and octave digits 0–8. The pinned common
one-based octave mapping is retained, including its legacy octave-zero result.

Each cart first plays a valid sound and then supplies a named note. Valid calls
must match an independent numeric C plan. Invalid calls must produce a note
diagnostic without reaching the engine; the sound already playing continues.
Both paths compare complete direct/isolated-worker PCM and persistent counters,
while an independent Lua worker keeps advancing. Direct cases run in child
processes and retain signal/exit diagnostics. `tests/note_parser_test.c` checks
all 2,097,152 three-byte ASCII combinations, plus null/length/high-byte inputs.
Failed parsing must leave both pitch outputs unchanged; 108 combinations are
accepted. This checks C-string parsing, not every VM's embedded-NUL handling.

The corrected preceding-binding corpus fails 93 cases. The common parser
returns success for unmatched note pairs and accepts arbitrary octave bytes.
Staged repairs validate the octave and return success only after matching a
supported pair; all ten existing parser targets use this staged source.
Scheme adds the same parser, correcting its independent octave mapping and
its permissive component checks. JavaScript frees its temporary note string
and returns `JS_EXCEPTION` when parsing fails, before calling the engine.
The pinned source checkout remains unchanged.

An initial harness incorrectly required exactly one error callback and retained
only its last message. Wren and Squirrel also emit stack diagnostics, producing
six extra fixture failures. The revised harness accumulates diagnostic text
and requires an error for malformed calls, while retaining strict engine-call,
reference/worker PCM, persistent-counter and independent-peer assertions.
Both the initial 99-failure and corrected 93-failure results are retained.
Other omitted defaults, numeric negative-note sentinels, full VM string-length
checks and native playback remain unqualified.

The initial sanitizer regression completes 79 of 80 checks but finds a
nine-byte leak after the Scheme `unknown` case. Its source/build receipts
and complete log are retained. s7's error-handler stacktrace frame copies the
`stacktrace_walker` heap string into a managed output block without freeing
it; regular frames already release their notes. The staged engine now frees
the copied string immediately after `stacktrace_add_func`. The diagnostic
and strict error/PCM/peer checks are retained. The repaired sanitizer suite
and ARM results below confirm the fix within the tested local profiles.

All 288 named-note cases and the exhaustive ASCII parser checks pass on host,
AddressSanitizer and ARM/QEMU builds. Full regressions complete 80 host and 80
sanitizer checks, plus 35 of 36 ARM CTest cases. The remaining ARM worker case
reaches QEMU's unsupported subreaper operation; separate parent-death checks
pass on all three builds, as do five paced ARM source-load scenarios. Nine
separately linked old-parser/binding controls reproduce 87 failed calls;
every corresponding repaired case passes using the same final fixtures.
The repaired Scheme error case retains its stacktrace without the observed
leak. Exact sources, generated adapters, build/test logs, binary hashes and
ARM snapshots are retained in `build/sfx-notes-qualification.json` and
`build/sfx-notes-fixtures/`. The installed player, Main and RBF remain
untouched. Native testing and the broader completion gates remain open.

## SFX effect and channel boundaries across fourteen runtimes

`tests/api_sfx_all_bounds_test.c` runs sixteen scenarios per runtime, for 224
cases. It covers effect IDs 0, 63, 64 and the signed 32-bit maximum; every valid
channel; invalid channels 4, -1 and both signed extremes; several negative-ID
stop calls; and calls with both arguments invalid. The [upstream SFX API](https://github.com/nesbox/TIC-80/wiki/sfx)
specifies effects 0–63, channels 0–3 and -1 for stopping. The pinned engine and
bindings also accept other negative IDs as stops; that behavior is retained.
The existing WASM import silently ignores invalid channels and checks an
invalid nonnegative effect ID first. Python checks the channel first. These
language-specific error priorities are retained.

Each case warms channel 3, increments a persistent counter, and then makes its
boundary call. A wrapper rejects any unsafe call reaching the engine. A C
plan compares complete PCM and active-channel state against direct execution;
the isolated worker must reproduce that PCM, error status and counter while
an independent Lua worker continues advancing. Diagnostics may include stack
traces, so the harness accumulates messages rather than requiring one callback.
Stopped-channel pitch differs historically between bindings and is inaudible;
the fixture checks stopped indices and exact PCM, with pitch/speed/duration
and tick state checked for active sounds. Values beyond the signed 32-bit API
range, other argument types and native playback are outside this corpus.

The preceding bindings pass 197 cases and fail 27: nine each in JavaScript,
Scheme and Forth. JavaScript sets an exception but continues after an invalid
effect ID; its invalid-channel branch returns a normal value, losing the
diagnostic. Scheme and Forth pass invalid IDs/channels directly to the engine.
The staged JavaScript binding now returns `JS_EXCEPTION` immediately. Scheme
returns a cartridge error before the engine call. Forth consumes all seven
arguments, reports a cartridge error and returns from its native word without
calling the engine; the containing Forth word may continue. Reference sources
remain unchanged.

An initial comparison-script assertion misclassified JavaScript's `bothinvalid`
failure as an unsafe engine call. The preceding channel guard suppresses that
call but returns normally, losing both pending errors. The initial corpus
already records this missing diagnostic. The comparison assertion now checks
that exact failure while retaining the same final fixtures and requiring the
repaired case to pass. The failed comparison log is retained; production
sources and the completed regressions are unchanged by this script correction.

All 224 cases pass on host, AddressSanitizer and ARM/QEMU builds. The full
regressions complete 81 host and 81 sanitizer checks, plus 36 of 37 ARM CTest
cases; only the known QEMU subreaper limitation remains. Separate parent-death
checks pass on all three builds, as do five paced ARM source-load scenarios.
The existing explicit-audio, preset-default, named-note and exhaustive parser
corpora also pass on every profile. Nine separately linked preceding-binding
controls reproduce 81 failed calls across the three platforms, and every
corresponding repaired case passes using the same final fixture. Build/source
closures, exact logs, generated bindings, binary hashes and ARM snapshots are
retained in `build/sfx-all-bounds-qualification.json` and
`build/sfx-all-bounds-fixtures/`. The installed player, Main and RBF are
unchanged; native audio and the broader completion gates remain open.

The initial sanitizer rebuild receipt contains a 14 ms future-timestamp warning
for a generated dependency file. That log and the receipt rejection are
retained. A subsequent full rebuild finishes without clock warnings; every
tested compiled source, generated binding and executable hash is unchanged
before and after that clean rebuild. Test checks and timestamp policies are
not relaxed.

## Stored sound presets and explicit overrides across fourteen runtimes

`tests/api_sfx_all_defaults_test.c` checks seventeen scenarios per language,
for 238 cases. Presets 0, 1 and 63 have distinct stored pitch, speed and
changing waveform/volume envelopes. The corpus covers omitted arguments,
null note placeholders, finite durations, explicit notes including zero,
the documented numeric note sentinel -1, explicit speeds 0, -1 and -4,
the engine's preset-speed sentinel 8, volume calls and stopping. Every case
runs 24 frames, comparing channel state, persistent counters and all 38,400
interleaved stereo samples in direct and supervised-worker execution with
an independent C action plan. Python, Janet and Forth retain their scalar
volume ABI in this corpus; the other eleven exercise stereo volume arguments.

The [upstream SFX reference](https://github.com/nesbox/TIC-80/wiki/sfx) specifies
-1 for the note assigned in the editor and accepts playback speed -1.
Most pinned dynamic-language bindings already use stored speed when omitted.
Forth and WASM have fully specified numeric call layouts. WASM's pinned Rust
and Zig templates supply note/octave -1,-1 with an explicit speed of zero.
This corpus preserves that zero rather than treating it as omitted speed;
an explicit speed 8 separately asks the engine for its preset speed. It does
not qualify other negative notes, partial typed pitch sentinels, all optional
argument types, larger numeric conversions or native playback.

The corrected preceding-binding corpus fails 113 cases. Most numeric note -1
paths overwrite stored pitch, and Scheme also omits preset initialization and
stored speed. Forth/WASM forward the default pitch pair to an engine that does
not resolve it. JavaScript, Ruby, Janet and Python mishandle null note
placeholders. Squirrel's bitwise numeric-type test includes null's shared type
flags; the staged SFX check compares concrete numeric types instead. Python
uses speed -1 for both omission and an explicit playback value. Its staged
signature now defaults speed to `None`, preserving both the editor's speed
when omitted and an explicitly requested -1. Other bindings keep their
existing valid speed overrides and layouts. The pinned checkout is unchanged.

The first fixture placed several stop calls on one line without statement
separators. JavaScript, Squirrel and Wren rejected those scripts, making the
initial corpus fail 141 cases. Newline-separated calls remove those fixture
syntax errors without changing the action plan or C/audio/state assertions.
Both the initial and corrected corpora, source, generated header, executable
and diagnostics are retained.

All 238 cases pass on host, AddressSanitizer and ARM/QEMU builds. Full
regressions complete 82 host and 82 sanitizer checks, plus 37 of 38 ARM CTest
cases; the remaining worker check reaches the known QEMU subreaper limitation.
Separate parent-death checks pass on all three builds, as do five paced ARM
source-load scenarios. Thirty-three separately linked preceding-binding
controls reproduce 339 failed calls across the three platforms, and every
corresponding repaired case passes using the same final fixture. The existing
sound, bounds, named-note and exhaustive parser corpora also pass on every
profile. Exact source/build closures, logs, generated bindings, binary hashes
and ARM snapshots are retained in `build/sfx-all-defaults-qualification.json`
and `build/sfx-all-defaults-fixtures/`. The installed player, Main and RBF are
unchanged. Native playback and the broader completion gates remain open.

## FFT binding dispatch and return values

`tests/api_fft_test.c` replaces the four engine FFT entry points at link time
with distinct, changing numerical results. Each callback records its function
identity, start/end arguments and call count in the calling machine's RAM.
An independent C argument table checks those records and the return value
after each real interpreter call. This avoids accepting a wrong function
merely because the disabled capture backend returns zero for every FFT API.

The corpus covers 322 cases: seven argument scenarios for `fft`, `ffts`,
`fftr` and `fftrs` in eleven runtimes, plus the existing `FFT` and `FFTS`
words in Forth. Single-bin calls exercise an omitted end argument where the
binding supports it; Wren and Forth supply an explicit -1. Range calls cover
ordinary, negative, reversed, full-size and beyond-size arguments. Four
frames check exact routing, signed integer arguments, changing results and
balanced calls in both direct and supervised execution while a separate Lua
worker performs a different FFT call. Forth preserves its existing result
scale of 65535; other bindings return a floating value checked after scaling
by 1024. Values are exactly representable before Forth's documented integer
conversion. Silent synthesized PCM is also checked.

The preceding corpus passes 315 cases and fails all seven Janet `ffts`
scenarios. Janet calls `core->api.fft` rather than `core->api.ffts`, losing
the smoothed result. The staged binding corrects that one dispatch. Its
upstream source hash and the exact generated-source difference are checked;
the pinned checkout is unchanged.

All 322 cases pass on host, AddressSanitizer and ARM/QEMU builds. Full
regressions complete 83 host and 83 sanitizer checks, plus 38 of 39 ARM CTest
cases; only the known QEMU subreaper limitation remains. Separate parent-death
checks pass on all three profiles, as do five paced ARM source-load scenarios.
Three separately linked preceding-Janet controls reproduce 21 failed calls;
every corresponding repaired case passes using the same final fixture. The
existing sound, bounds, named-note and exhaustive parser corpora pass again.
Exact source/build closures, logs, generated bindings, binary hashes and ARM
snapshots are retained in `build/fft-qualification.json` and
`build/fft-fixtures/`. The installed player, Main and RBF are unchanged.

This corpus qualifies binding routing and conversions for the tested
arguments. It does not enable microphone or loopback capture, or verify FFT
mathematics, frequency clamping, wrong optional types, nonfinite values or
numeric values outside the signed 32-bit range. The pinned WASM and
MiniScript bindings expose no FFT APIs; Forth exposes only FFT and FFTS.
Those missing APIs, capture support and native tests remain completion gates.

## Completing the FFT API surface across fourteen runtimes

The staged WASM and MiniScript bindings now expose `fft`, `ffts`, `fftr` and
`fftrs`. Forth adds `FFTR` and `FFTRS` after its existing 52 native words, so
prior word indices remain unchanged. The new words use the same integer
result scale of 65535 as FFT/FFTS. Raw magnitudes can exceed normalized
values; the new raw words saturate at signed 32-bit limits before conversion.
Their NaN result guard returns zero. Nonfinite and negative raw results are
not qualified by the current corpus.

WASM imports belong to `env`, accept two `i32` arguments and return `f64`.
wasm3 spells that signature `F(ii)`. An explicit end argument of -1 selects
a single bin. For C cartridges, declare the matching import, for example:

```c
#include <stdint.h>
__attribute__((import_module("env"), import_name("fft")))
extern double fft(int32_t start_bin, int32_t end_bin);
```

MiniScript uses `tic80.fft(start_freq, end_freq=-1)` and equivalent names
for the other functions. A null end argument also retains -1. It checks
numeric type, finiteness and signed 32-bit range before converting arguments;
ordinary finite fractions within that range are truncated. Full optional
type, fractional and nonfinite behavior across every runtime remains open.

The expanded fixture checks seven scenarios for all four functions in all
fourteen runtimes: 392 direct/worker cases. Its raw result plan also uses
40000, 32768 and 32769. Independent C expectations distinguish Forth's
in-range conversion from saturation while checking exact return values in
the other runtimes. Another forty cases check missing/null/string and
out-of-range MiniScript arguments, and valid WASM modules with incompatible
`f32` or one-argument FFT imports. They must report errors before engine
access, preserve zero FFT call counts, and permit a fresh Lua worker to run
afterward. Valid cases continue to interleave an independent FFT worker.

Both exploratory corpora pass the preceding 322 cases and fail the seventy
missing bindings: 28 each in WASM and MiniScript, plus fourteen Forth raw
calls. The first extended fixture used fractional raw results; the next
adds the raw conversion boundary values without changing those failure
counts. Initial sources, receipts, executables and diagnostics are retained.
The initial source snapshot was recovered byte-for-byte against its pretest
SHA256 receipt. A subsequent misplaced test guard caused a compile error;
the failed source/logs are retained, and the guard was moved to the engine
dispatch wrapper before regression tests.

All 392 normal cases and forty rejection cases pass on host, AddressSanitizer
and ARM/QEMU builds. Full regressions complete 83 host and 83 sanitizer checks,
plus 38 of 39 ARM CTest cases; only the known QEMU subreaper limitation remains.
Separate parent-death checks pass on all three profiles, as do five paced ARM
source-load scenarios. Nine separately linked preceding-binding controls
reproduce 210 failed calls across those profiles; every corresponding added
binding passes with the same final fixture. All nine staged binding files
preserve their preceding non-FFT contents byte-for-byte after removing the
verified additions. Build/source closures, logs, exact generated bindings,
binary hashes and ARM snapshots are retained in
`build/fft-all-qualification.json` and `build/fft-all-fixtures/`. The installed
player, Main and RBF are unchanged.

Microphone and loopback capture remain disabled. This work adds language
bindings and qualifies the tested conversions; it does not qualify FFT
mathematics, frequency clamping, capture devices or native board execution.
The historical twelve-runtime section above describes its earlier fixture;
this section records the expanded API surface.

## Preparing the Linux microphone capture backend

`tic80-fft-probe` builds the pinned miniaudio ALSA capture implementation and
the real TIC-80 FFT/VQT processing independently of the player. `--list`
enumerates capture devices; `--capture device-name-substring` explicitly opens
a matching microphone for a two-second diagnostic. This target is not linked
into the ordinary player or Studio. Those runtimes retain
`TIC80_FFT_UNSUPPORTED`; capture is not enabled or deployed.

`cmake/fft_capture.cmake` stages a repair against the exact pinned FFT and
miniaudio source hashes without editing the reference checkout. The capture
logger now outlives its context and device. Initialization failures release
every successfully initialized resource, and repeated close/open operations
are safe. Enumeration owns a separate context, so it cannot overwrite active
capture. Device teardown completes before clearing shared samples. A requested
name that has no match fails rather than selecting a different microphone;
ALSA-only capture rejects unsupported playback-loopback requests and never
falls back to a null device. Open/close/enumeration belong to one control
thread; callback sample updates and snapshots share the sample spinlock.

The staged query helper also fixes the oversized-start range branch: it
claimed to clamp to bin 1023 but assigned bin zero. Single-bin out-of-range
queries still return zero, wholly out-of-range pairs return zero, and reversed
in-range pairs retain upstream's single-start-bin behavior. The existing
upstream FFT gain and smoothing behavior remains intact, including gain from
the preceding update on the first normalized frame.

`tests/fft_capture_test.c` substitutes only the miniaudio device boundary.
It runs the actual capture callback, kiss FFT, VQT kernels and query helpers.
It injects logger, callback registration, context, enumeration, device and
start failures; FFT allocation failure; and VQT FFT allocation failure with
FFT continuing independently. Tests include replacement/repeated close,
enumeration during active capture, missing-device and unsupported-loopback
rejection, stereo averaging, oversized input blocks, null-input silence,
initial and reopened sample clearing, and coherent snapshots during 200
concurrent full-buffer callbacks. A known three-component signal checks all
1024 FFT magnitudes and subsequent gain/smoothing; VQT must produce finite
nonnegative nonzero results. Closed-form expectations check 52 range queries
across all four APIs, including signed integer extremes.

Full regressions passed 84 host and 84 sanitizer checks, plus 39 of 40
ARM/QEMU checks. The remaining CTest failure is the previously documented
QEMU subreaper limitation; separate parent-death checks passed on all three
profiles. Five paced ARM source-load scenarios also passed. Six separately
linked negative controls reproduce the old initialization leak and range
clamping bug; each corresponding current case passes. A review replaced
generic pointer-array casts in the device test helper with typed lookups.
The three capture fixtures and six controls were rechecked after that change,
with every other tested source, generated file and binary unchanged.
Receipts, source snapshots, build commands, full logs and ARM binaries are
retained in `build/fft-capture-qualification.json` and
`build/fft-capture-fixtures/`. The preceding player/Studio binaries and their
generated sources remain byte-for-byte unchanged.

The real host diagnostic also rejects an unavailable capture request cleanly.
Its log shows that this WSL installation lacks `libasound.so.2`/`libasound.so`;
that run exercises provider-initialization failure, not microphone recording.

Actual microphone samples, native ALSA provider availability/ABI, worker
ownership, cartridge/Studio configuration, capture timing and simultaneous
playback remain unqualified. The device double and ARM emulation do not
establish native capture support. No MiSTer access occurred while the user
was away.

## Opt-in microphone capture in supervised workers

Linux builds now compile the repaired ALSA/FFT/VQT backend into the cartridge
service and Studio. `--fft` selects the default input; `--fft-device name`
selects a matching capture name. Neither frontend opens a microphone by
default. `TM_ENABLE_FFT_CAPTURE=OFF` retains the unsupported backend and an
explicit unavailable status when input is requested. Non-Linux builds default
to this disabled configuration.

The supervisor copies bounded device configuration into its own storage and
worker IPC, validates the acknowledged capture status, and never owns a
capture device. Workers open capture before initial cartridge execution or
Studio RUN and close it before normal shutdown. Unavailable input logs a
diagnostic and preserves cartridge/editor execution with zero FFT input.
Studio recovery reopens the copied device configuration; its cleanup-only
worker does not open a microphone.
Departure may kill a worker whose tick is still pending. The frontend fixture
checks that the capture worker is reaped and its exclusive device lock can be
acquired afterward, including departure without an orderly device-close event.

Before validating a replacement cartridge, the service asks its existing
worker to close capture without discarding Lua or persistent state. The new
worker can then open the same exclusive device. If validation fails, the old
worker reopens its input. The fresh capture resets FFT samples, gain and
smoothing; the cartridge's execution state survives. Both capture commands
use the existing bounded initialization timeout. Default-disabled loads do
not add this IPC handoff.

The new fixtures substitute the miniaudio device boundary while retaining
exec workers, callback processing, real FFT/VQT, script calls and transport.
Exclusive device locks check handoff and OS cleanup after a killed worker.
Coverage includes independent 32/64-bin inputs, caller configuration lifetime,
all four FFT results, default and missing input, initialization timeout,
Studio recovery, service load/rejection/reset/reload, and Studio CLI RUN and
departure. The playback comparison checks every sample and pixel across
16 frames against an identical capture-disabled cartridge, including nonzero
stereo SFX. A separately linked control removes service capture handoff so
the same exclusive device becomes unavailable during replacement.

The earlier capture preparation section describes its original qualification
checkpoint. This integration changes local player/Studio binaries. Native
ALSA availability/ABI, actual microphone PCM, unplug/replug behavior, timing
and simultaneous playback remain unqualified. The WSL ALSA package enables
real provider initialization and unavailable-device testing only. No MiSTer
access or deployment occurs while the user is away.
The ARM/QEMU executables link statically but miniaudio still loads ALSA through
`dlopen`. The linker explicitly reports a dependency on compatible shared
glibc libraries for that path. Static linkage alone does not establish native
microphone compatibility; the retained ARM fixtures are local test artifacts.

Full regressions passed 87 host and 87 sanitizer checks and 40 of 41 ARM/QEMU
checks. The sole reported ARM failure is the existing QEMU subreaper limitation.
Separate parent-death tests passed on all three profiles. Paced ARM service and
Studio capture tests and five source-load scenarios passed. A capture-disabled
build also passed, and three controls reproduced unavailable capture when
service handoff was removed. The first host run exposed the overly strict
Studio orderly-close assertion; its log and original fixture are retained.
The corrected fixtures strengthened departure cleanup and rejected-candidate
coverage, then all regressions were rerun with identical binaries.

Every audio sample and video pixel matched the capture-disabled playback
comparison across 16 frames: 25,600 samples, including 19,786 nonzero samples.
Receipts and source, generated-code, command, log and ARM binary snapshots are
retained in `build/fft-worker-qualification.json` and
`build/fft-worker-fixtures/`. The preceding Main and FPGA candidates and compiler
archive were rechecked. Installed player, launcher and MiSTer state remain
unchanged; this is local qualification of the worker integration.

## FFT normalization, spectrum accuracy and invalid-input recovery

The pinned API describes individual `fft`/`ffts` bins as roughly 0–1. The
original processing loop multiplied magnitudes by the previous frame's gain,
allowing large startup and rising-level spikes. The staged backend now computes
the current spectrum and peak before applying gain. Peak decay (0.995), the
0.01 floor, raw magnitudes and smoothing weights remain independent of this
ordering correction. Individual normalized bins stay within floating-point
tolerance of 0–1; range calls still return inclusive sums and can exceed one.
The previous capture checkpoints intentionally retained the original gain
ordering; this numerical qualification supersedes that behavior.

Capture replaces each NaN/infinite channel sample with silence, preserving a
valid other channel. Stereo averaging uses double precision before converting
back to float, avoiding finite-input addition overflow and retaining subnormal
samples. Magnitudes use `hypotf` instead of squaring components, so large
representable spectra do not overflow intermediate squares. An unrepresentable
individual magnitude becomes zero before peak tracking and smoothing. These
guards prevent invalid samples from poisoning later output; they do not claim
accurate float FFT values beyond the transform's representable range.

`tests/fft_spectrum_test.c` reuses the deterministic device boundary while
running real callback processing, Kiss FFT and API queries. Its independent
direct transform uses long-double trigonometric recurrence and complex sums,
not FFT implementation output. It checks all 1024 bins, normalized and raw
smoothing, and 784 queries across the four FFT APIs over 14 reference frames.
Signals cover silence, rising/falling levels, near-Nyquist and excluded Nyquist
frequencies, DC, an impulse, deterministic noise, off-bin mixed tones,
anti-phase stereo, subnormals and large finite magnitudes. Separate checks
cover invalid channels, overflowing transforms, and finite FFT/VQT recovery in
the same open device. This FFT-only checkpoint left broader VQT numerical,
whitening and binding behavior open; the later VQT qualification is described
below.

Three controls restore previous-frame gain, unsanitized channel averaging or
squared magnitudes independently. Each uses the current numerical fixture,
real DSP and identical build flags, and must fail the corresponding oracle
while the repaired backend passes. Device doubles and ARM emulation do not
establish native microphone accuracy or pacing. No MiSTer access or deployment
occurs while the user remains away.

Full regressions passed 88 host and 88 sanitizer checks. The first full ARM/QEMU
run passed 41 of 42 checks; its worker fixture exceeded the unchanged two-second
intentional-crash observation deadline. Concurrent local tests were active,
but the cause is not established. The original run and driver are retained.
Repeating only that worker check with the same binary passed crash observation
and containment, then reached the existing QEMU subreaper limitation. None of
the other 41 passing checks was repeated or replaced. External parent-death
cleanup tests passed on all three profiles. Paced ARM service and Studio
capture tests, five source-load scenarios and the capture-disabled build passed.
All nine numerical controls failed their intended old calculation and passed
the repaired calculation across host, sanitizer and ARM profiles.

Playback comparison still matched every video pixel and audio sample across
16 frames (25,600 samples, 19,786 nonzero). The unchanged tested inputs and
binaries, independent-oracle results, control sources and executables, failure
and retry logs, build commands and snapshots are retained in
`build/fft-math-qualification.json` and `build/fft-math-fixtures/`. The retained
Main and FPGA candidates and compiler archive are rechecked. Installed player,
launcher and MiSTer state remain unchanged. Native capture and broader console
qualification remain open.

## VQT kernels, whitening and large finite magnitudes

The staged VQT backend uses `hypotf(real, imag)` before the existing gain and
finite-result guard. Squaring large but representable components previously
overflowed and discarded the bin as zero. No upstream checkout is edited;
configuration verifies the pinned VQT source hash and replaces exactly the
magnitude expression. Kernel generation, whitening and API normalization
semantics remain the pinned implementation. In particular, unsmoothed `vqt`
and `vqtw` divide instantaneous magnitudes by the smoothed peak; they can
exceed one during a rise. Smoothed normalized APIs retain their 0–1 clamp.

`tests/vqt_spectrum_test.c` runs production capture, Kiss FFT and VQT through
the existing deterministic device boundary. It independently checks all
491,640 kernel frequency positions against a closed-form geometric-sum
transform of the Hamming-windowed cosine policy. Every retained index and
real/imaginary coefficient is checked, as are all 120 musical center
frequencies. The fixed production policy retains 18,053 coefficients. This
covers the production Hamming configuration, not arbitrary kernel parameters
or the unused Gaussian branch.

A separate long-double radix-two complex transform is checked against eight
direct DFT probes per frame, an analytic impulse across all 8192 bins and
Parseval energy conservation. Qualified float kernel coefficients are matrix
inputs to an independent complex sum; no production FFT output is used by
the audio oracle. Independent prefix sums of log magnitudes check the local
whitening envelope, and a separate recurrence checks raw/whitened smoothing,
peak decay, floors and normalization. This comparison allows finite-precision
DSP rounding; kernel coefficients and sparse selection have their own tighter
checks before they can be used as matrix inputs.

The first ARM control stopped inside the independent transform's direct-DFT
cross-check, before the VQT checks. An output-relative tolerance did not
account for cancellation on ARM's double-precision `long double`. The oracle
now bounds recurrent rotation/accumulation rounding using input L1 norm,
operation count and `LDBL_EPSILON`; the production VQT comparison thresholds
are unchanged. The initial source, fixture receipts, control executables and
failure logs are retained in `build/vqt-first-oracle-fixtures/`.

Each whitening-enabled and whitening-disabled variant checks all eight APIs
at every bin, plus four invalid signed endpoints, across ten frames: 9,920
queries per variant. Audio covers silence, rising/falling tones, low/high
frequencies, off-bin mixtures with deterministic noise, an impulse, DC, large
finite input and subsequent normal input. The disabled variant owns the VQT
symbols compiled with `VQT_SPECTRAL_WHITENING_ENABLED=0` while sharing the same
capture, kernels, FFT and state library. Controls restore squared magnitudes
or independently remove the whitening envelope or raw smoothing. These are
DSP/API checks; real microphone PCM, native provider compatibility, pacing,
broader language conversions and other kernel/window configurations remain
open.
The C-side oracle does not exercise script wrappers. At this DSP checkpoint,
the pinned WASM, MiniScript and Forth bindings lacked VQT exports, and the
fourteen-runtime FFT binding corpus covered FFT calls rather than these eight
VQT calls. The subsequent binding change is described below.

Full regressions passed 90 host and 90 sanitizer checks, and 43 of 44 ARM/QEMU
checks. The sole ARM failure is the existing QEMU subreaper limitation; crash
observation and containment passed without a timing retry. Separate external
parent-death cleanup tests passed on all three profiles. Paced ARM capture in
the service and Studio, all five source-load scenarios and the capture-disabled
build passed. All nine magnitude/envelope/smoothing controls rejected the
altered calculation and accepted the repaired calculation. Capture-enabled
playback still matched every pixel and all 25,600 samples over 16 frames,
including 19,786 nonzero samples.

The qualification, source/generated-code snapshots, ARM executables, control
binaries, flags, logs and initial oracle failure are retained in
`build/vqt-qualification.json` and `build/vqt-fixtures/`. The tested sources and
binaries are checked unchanged before and after the full run. Retained Main
and FPGA candidates and the compiler archive are rechecked. No MiSTer access
or deployment occurs while the user is away; installed artifacts remain
unchanged and native qualification is still pending.

## VQT scripting exports and capture-disabled dispatch

`cmake/runtime_fft.cmake` now stages all eight VQT exports for WASM, MiniScript
and Forth: `vqt`, `vqts`, `vqtr`, `vqtrs`, `vqtw`, `vqtsw`, `vqtrw` and
`vqtrsw`. The pinned reference sources remain intact. WASM imports take one
signed 32-bit bin and return a double (`F(i)`). MiniScript requires a numeric,
finite bin within signed 32-bit bounds and truncates accepted fractions.
Forth preserves the spectrum scale of 65535, maps NaN to zero and saturates
before converting to a signed cell.

`tests/api_vqt_test.c` and `tools/generate_vqt_cases.py` cover 856 valid cases
across fourteen runtimes and eight APIs. Each runtime/API pair has seven
integer cases, including signed limits and bins outside the kernel range.
Nine runtimes also exercise a fractional bin. Each case runs four frames in
both a direct interpreter and a supervised exec worker, with an independent
Lua peer interleaved. Link wrappers check the exact function, bin, call count,
frame and numeric result; an independent coverage matrix rejects missing or
duplicate cases. Forth raw results exercise signed saturation. Another 64
cases check MiniScript argument rejection and incompatible WASM imports,
followed by recovery in a fresh worker.

The routing corpus supplies distinct synthetic API results. It does not
establish nonzero script-to-capture integration. A separate build with all
fourteen runtimes and `TM_ENABLE_FFT_CAPTURE=OFF` uses the same valid and
invalid corpus, but each valid wrapped call invokes the real C backend and
checks that it returns zero. These are distinct checks from the preceding
positive DSP oracles. Broader coercion and invalid-value behavior in existing
wrappers, positive captured spectrum results through every language and
native provider/device behavior were open at this binding checkpoint. The
subsequent capture corpus closes the positive spectrum gate locally.

Twelve controls link each preceding WASM/MiniScript/Forth adapter against the
current test binary in host, sanitizer, ARM and capture-disabled profiles.
The selected VQT call must fail with the preceding adapter and pass with the
current adapter. The first control attempt passed host and sanitizer controls,
then failed before ARM compilation because the runner expected a compiler
cache entry that the ARM toolchain does not set. The corrected runner reads
CMake's generated compiler configuration when needed. The original runner,
logs and control artifacts remain in `build/vqt-bindings-first-control-fixtures/`.
An earlier receipt-path error was corrected before tests and retained in
`build/vqt-bindings-first-receipt-failure.log`. Neither correction changes
runtime behavior or test expectations.

Full regressions passed 91 host and 91 sanitizer checks, and 44 of 45 ARM/QEMU
checks. The sole ARM failure is the known subreaper limitation; crash
containment passed without a timing retry. The sanitizer worker test includes
its intentional child crash, with no other sanitizer errors or leak report.
All three full suites passed 856 valid and 64 invalid VQT cases. The extended
capture-disabled build passed the same corpus through the real unsupported
backend, as well as its capture-disabled lifecycle check. All twelve binding
controls passed. External parent-death cleanup passed on all three profiles;
paced ARM service and Studio capture and all five source-load scenarios passed.
FFT/VQT numerical oracles and capture playback parity also passed in each
profile. Tested source, generated-code and binary hashes were unchanged over
the entire run.

The record and snapshots are retained in `build/vqt-bindings-qualification.json`
and `build/vqt-bindings-fixtures/`. Compiler configuration, compile/link flags,
controls, initial harness failures and test logs are retained alongside the
source and generated adapters. Large sanitizer control binaries are retained
and hashed once rather than duplicated. The independent audit compares live
files with their recorded hashes and snapshots, and reconstructs each preceding
adapter by removing exactly the eight added exports. Native MiSTer tests and
deployment remain pending while the user is away.

## Nonzero VQT capture through all scripting runtimes

`tests/vqt_capture_bindings_test.c` runs the real capture callback, FFT, VQT,
cartridge tick and scripting API in all fourteen runtimes. Only the miniaudio
device boundary supplies deterministic PCM, injected at the existing FFT poll
before each tick's production FFT and VQT calculations. The original C APIs
are not wrapped or replaced. `tools/generate_vqt_capture_cases.py` generates
112 cartridges: each runtime calls each of the eight APIs over every bin,
four invalid bins and a fractional bin in the nine accepting runtimes.

Each case executes ten input frames in both a direct interpreter and an exec
worker, one frame with capture stopped, then the ten frames again after resume.
The corpus compares 586,320 integer results per profile. Input includes silence,
noise, different tones and levels, an impulse and DC mixed with noise. The
expected spectra, whitening, smoothing and peaks come from the preceding
independent long-double oracle with its existing tolerances. A conditional
main guard permits reuse of that test source without changing its calculations.
Forth uses its 65535 scale; other languages store spectra scaled by 1024.
Integer quantization contributes less than one additional cell to the bound.
Invalid bins and silence require exact zero, as do all bins while paused.
Resume must start with fresh capture state while the cartridge's PMEM frame
counter continues. All cartridge playback samples must remain exactly silent.

The first stereo input encoded opposite constant channel offsets. A left-only
control unexpectedly passed because VQT kernels discard DC; that input could
not qualify stereo mixing. The accepted incorrect control and its sources,
binary and logs are retained in `build/vqt-capture-stereo-gap-fixtures/`.
The corrected input uses opposing 1600.25 Hz tones, which must cancel before
the transform. Controls now remove PCM, feed the left channel into both inputs,
or replace `vqt` with `vqts`. All fifteen altered cases must fail numerically
and their current cases must pass in host, sanitizer and ARM profiles.
The initial successful ten-frame smoke and its clock-skew build warning remain
in `build/vqt-capture-first-smoke-fixtures/`; final build closures require no
clock-skew warning.

The targeted regression scope is eight FFT/VQT/capture checks per profile.
It includes this corpus, both numerical VQT variants, the independent FFT
oracle, capture lifecycle, configured exec-worker capture, the existing FFT
binding corpus and the existing VQT routing/rejection corpus. All production
executables must match the preceding full regression byte for byte before
and after the run. The only preceding fixture changes are the numerical VQT
test programs with the new main guard. Native provider ABI and PCM, timing,
unplug/replug, loopback sources and hardware deployment remain open.

All eight targeted checks passed on host, sanitizer and ARM. Each profile
completed 586,320 capture-result comparisons, the 856 valid and 64 invalid
routing cases, both numerical VQT variants and the remaining FFT/capture
checks. All fifteen altered controls failed and their current cases passed.
The targeted sanitizer run reported no memory error or leak. Production
executables matched the preceding full regression; source, generated files
and tested binaries were unchanged between the beginning and end of the run.
`build/vqt-capture-qualification.json` and `build/vqt-capture-fixtures/` retain
the source, executables, generated cartridges, flags, compiler configuration,
controls, logs and the earlier stereo-test gap. The independent audit checks
the historical source reconstruction, exact control modifications and retained
artifacts. No MiSTer access or deployment occurred.

## Spectrum argument conversion repairs

The preceding bindings cast nonfinite values to integers in Lua-family, Wren
and Squirrel spectrum calls. A host float-cast-overflow probe reproduces the
undefined conversions, including Squirrel's conversion inside `sq_getinteger`.
JavaScript previously ignored a failing `JS_ToInt32` conversion and queried
the spectrum after an object's `valueOf` threw. The original sources, binaries,
compile/link commands and observations are retained in
`build/spectrum-args-first-probe/` and its JSON receipt.

The staged adapters now validate floating arguments before casting in all
eight VQT and four FFT functions. Finite fractions truncate before the signed
32-bit range check. Lua keeps `tonumber` coercion, including zero for nonnumeric
values; Wren requires a numeric slot. Squirrel keeps its integer narrowing,
boolean conversion and nonnumeric zero fallback, but checks float arguments
without first calling `sq_getinteger`. Squirrel floats have 32-bit precision:
rejection boundaries use representable values outside the signed integer range.
JavaScript retains ECMAScript `ToInt32` conversion, including modulo narrowing
and zero for nonfinite numbers, while propagating conversion exceptions.
Existing FFT optional-end defaults remain intact. Other numeric APIs retain
their preceding getters and are outside this repair's scope.

`tools/generate_spectrum_args_cases.py` supplies 1,100 valid and 744 invalid
cartridges for Lua, MoonScript, YueScript, Fennel, Squirrel, Wren and JavaScript.
`tests/spectrum_args_test.c` independently checks corpus coverage and conversion
expectations. Wrapped C APIs record exact function/argument/result routing;
this fixture tests bindings rather than the DSP. Valid cases run for three
frames directly and through real exec workers. Invalid cases must report an
error without dispatching a spectrum call, then close cleanly and permit a
fresh worker to run an independent Lua cartridge. Valid and recovery ticks
require exactly silent playback, and every worker must be reaped. The real capture and numerical fixtures
continue to exercise the original unwrapped spectrum APIs separately.

Two initial harness failures are retained. One Squirrel boundary rounded back
into the valid range; the corrected rejection uses the next representable
float below it. Wren reports its diagnostic and stack trace in separate
callbacks; the harness now retains the complete bounded diagnostic sequence.
The first instrumented corpus also completed successfully, but its result
parser mistook Fennel's expected script-error wording for sanitizer output.
The preserved executable, objects, sources, flags and report remain in
`build/spectrum-args-floatcast-first-fixtures/`. Its original two parser lines
were reconstructed after the parser edit; that sequence is recorded explicitly.
The corrected parser recognizes compiler file/line/column diagnostics, and
the instrumented corpus passed again with no float-cast-overflow report.
Build clock-skew warnings are retained separately; the unchanged final closures
require clean logs.

All 93 host and 93 sanitizer checks passed after rebuilding the affected player
and Studio executables. ARM passed 46 of 47 checks; its sole failure remains
QEMU's unsupported `PR_SET_CHILD_SUBREAPER` call after the worker containment
checks pass. External native Linux subreaper probes passed on host, sanitizer
and ARM/QEMU, with execution workers adopted and reaped after supervisor death.
Paced ARM service and Studio capture, plus all five source-load scenarios,
passed. The capture-disabled profile passed the argument corpus and lifecycle
check, and every valid VQT call across fourteen runtimes reached the real
unsupported backend and returned zero.

Each main profile passed all 1,100 valid and 744 invalid argument cases, the
856 valid and 64 invalid VQT routing cases, and 586,320 real capture-result
comparisons. Numerical FFT/VQT oracles and capture playback parity passed.
All 32 controls using preceding bindings dispatched invalid arguments and
failed their check; the repaired cases rejected those arguments and recovered.
The dedicated float-cast-overflow run passed with no compiler sanitizer report.
The full memory-sanitizer regression reported only the expected deliberately
signaled worker SEGV, with no other memory error or leak.

`build/spectrum-args-qualification.json` and `build/spectrum-args-fixtures/`
retain the current sources, generated cartridges/adapters, commands, logs and
new binary snapshots. Unchanged binaries reuse their preceding immutable
archive. Other current sanitizer executables are retained and hashed in place;
the new sanitizer corpus is archived once. Preceding failed harness attempts
and the original undefined-conversion probes remain separately recoverable.
The audit reconstructs every preceding adapter body and checks the source,
generated files and binaries against their pre/post-test receipts. Main, FPGA
and compiler artifacts remain unchanged. MiSTer access and deployment remain
pending while the user is away.

## Forth native-word admission

The original pForth dispatcher entered native TIC words before checking their
operand stack. All sixteen original FFT/VQT missing-operand probes reached the
C API once and only then reported underflow. No allocation-boundary memory
fault was observed in those probes.

The staged TIC word registry now encodes its required depth in the previously
unused CALL_C metadata byte, preserving the zero-argument C function ABI.
The staged interpreter checks that depth before dispatch and raises standard
`THROW -4`, which existing Forth `CATCH` handles. Unmarked bootstrap C-test
aliases, incompatible parameter counts and out-of-range table indices raise
`THROW -13` before dispatch. The bootstrap aliases exist in the generated
dictionary and reuse indices with incompatible TIC function prototypes.

The independent corpus covers all 62 native words: 62 exact-depth and 62
surplus-depth cases, every one of 192 missing-depth combinations both uncaught
and caught, and five incompatible-glue cases. It checks direct interpreters,
exec workers, caller-cell preservation and fresh-worker recovery after each
uncaught failure. Most native C bodies are doubled at this test boundary;
PMEM telemetry, the real interpreters and worker processes run normally.
Separate production API regressions execute the real spectrum calculations.
The original FFT/VQT probes execute the real Forth wrappers and observe their
calls at the spectrum C API boundary; those probes replace the calculation
with instrumentation. General Forth primitive stack bounds and arbitrary
Forth memory access remain outside this admission corpus.

The source audit reconstructs the complete preceding adapter in each profile
and reverses the staged kernel to the pinned vendor source. All original native
function bodies, bootstrap dictionaries, other language adapters and Forth
context handling remain unchanged. Main, FPGA and installed device artifacts
are also unchanged. Native deployment and hardware checks remain pending.

All 94 host and 94 sanitizer checks passed. ARM passed 47 of 48 checks; its
sole failure is QEMU's unsupported subreaper call after worker containment
passes. External native Linux orphan-cleanup probes passed for all three
profiles, as did paced ARM service/Studio capture and all five source-loading
scenarios. The capture-disabled profile passed the argument, Forth and capture
lifecycle checks and the real unsupported VQT backend corpus.

Each profile passed all 508 native-depth cases, five incompatible-glue cases
and 192 fresh-worker recoveries. All sixteen controls using the original
dispatcher failed before unsafe calls could proceed through the test double;
the revised controls passed. Repeating the original sixteen probes in all four
profiles produced 64 actual FFT/VQT C API checks: missing operands now produce
an error with zero engine calls. Memory-sanitizer output contains only the
existing intentionally signaled worker SEGV, with no other memory error or
leak. The other spectrum numerical, argument and real-capture oracles passed.

`build/forth-stack-qualification.json` and `build/forth-stack-fixtures/` retain
the receipts, source/generated-code snapshots, commands, logs and selected
executables. Other large sanitizer executables and controls remain hashed in
place. Initial configure and harness failures are retained, including a test
context-capture mistake and the control driver's missing ARM compiler cache
entry. Reused host/sanitizer control binaries retain their original hashes.
The initial source-audit failure omitted a formatting space while reconstructing
void registrations; its script and output are preserved, and the final audit
restores complete adapters and the kernel's original CRLF bytes exactly.

## MiSTer SDK and real ALSA preparation

An additional ARM profile uses GNU Arm GCC 10.2.1 with the SDK's glibc 2.31.
It keeps libc dynamic so that the application and ALSA share libc, while
linking libgcc and libstdc++ statically. This addresses the earlier uncertainty
around loading ALSA into an executable linked against a newer static libc.
It does not establish compatibility with the board's uninspected providers.

The local ALSA provider is built from release 1.2.4, matching the version and
archive hash in the official
[Buildroot 2021.02.4 recipe](https://raw.githubusercontent.com/buildroot/buildroot/2021.02.4/package/alsa-lib/alsa-lib.mk)
and [hash file](https://raw.githubusercontent.com/buildroot/buildroot/2021.02.4/package/alsa-lib/alsa-lib.hash).
The archive SHA-256 is
`f7554be1a56cdff468b58fc1c29b95b64864c590038dd309c7a978c7116908f7`.
This local build disables Python, alisp and UCM; the board's actual build
configuration is unknown. No provider was installed on the MiSTer or into the
SDK. The fixture runtime root contains copies of the matching SDK libraries.

`tools/check_runtime_abi.py` checks ARM32 little-endian hard-float images,
Cortex-A9 instruction requirements, explicit loader/library dependencies,
strong dynamic imports and a glibc version ceiling of 2.31. It also checks all
66 ALSA symbols loaded by the pinned miniaudio initialization body. Controls
reject a static ARM probe, a host executable, a real GLIBC_2.34 executable and
an incomplete ARM ALSA provider. The SDK probe passes with seven providers.
The import check uses the closure's export union; actual loader tracing also
confirms initialization of the SDK glibc libraries and the built ALSA provider.
Neither check substitutes for the board's provider fingerprints and loader.

The real miniaudio/ALSA path enumerates a file-capture fixture, passes three
capture-and-close cycles, and rejects a missing device selector. A paced
producer supplies stereo F32LE at 44100 Hz, amplitude 0.25 and 689.0625 Hz.
All four observations in each cycle identify FFT bin 32 with raw magnitude
approximately 512. This is synthetic PCM through real ALSA, not a physical
microphone. ALSA's null capture alone does not fill buffers; the file plugin's
input supplies the actual samples. The fixture uses temporary Linux storage:
FIFO creation fails on WSL's NTFS mount, and the same ALSA configuration fails
there while enumeration succeeds from `/tmp`. Both attempts and a controlled
filesystem comparison are retained. Final logs and receipts remain on Windows
storage after the temporary Linux files are removed.

The initial full SDK build failed because MRuby's unquoted compiler command
was truncated at a space in the logical workspace path. Resolving the SDK
junction to its physical path fixes that failure. The next build rejected
MiniScript's anonymous-struct designated initializer. A conditional adaptation
for GNU C++ compilers older than 11 constructs the descriptor with a constexpr
builder. The old initializer fails with GCC 10; the replacement compiles and
executes, has no dynamic constructor, and matches all 37 semantic fields of
the original in a GCC 13 comparison. The interpreter body and vendor checkout
are unchanged. Reconfiguring all four preceding profiles preserves all 123
generated-source hashes and 177 binary fixture hashes.

At this preparation checkpoint, the complete SDK build was blocked at Studio linking. Both
`src/studio_net.c` and `src/studio_session.c` call
`posix_spawn_file_actions_addclosefrom_np`, which this SDK lacks. A replacement
must preserve descriptor closure, the worker's descriptors 3/4/5, its separate
process group and curl's PATH search. The failed link logs and both source
files are retained. No fallback that merely skips closure has been introduced.
Player, live transport and probe executables linked; this preparation checkpoint
does not qualify the complete Studio SDK build or its full regression suite.

Nine SDK CTest checks pass: language registration, capture lifecycle,
MiniScript keyboard handling, FFT bindings, VQT bindings, WASM return slots,
cartridge execution, clock behavior and memory access. The player, live
transport and probe also pass the ABI audit against the same seven local
providers. Sources and those executable hashes remain unchanged across this
partial run. These results do not include SDK Studio supervision, the broader
runtime regressions or worker/service/Studio capture qualification.

`build/mister-sdk-preparation.json` and
`build/mister-sdk-preparation-fixtures/` retain the partial receipts, sources,
selected executables, providers, commands and logs. Unchanged sources refer to
their checked snapshots in the preceding full Forth qualification. The
complete Studio SDK build is explicitly marked incomplete. Hardware is
unavailable at the user's request; the installed
player, launcher, Main and FPGA image were untouched.

## Studio process startup on glibc 2.31

The complete SDK build now links after replacing Studio's two direct spawn
sequences with `tm_spawn_closed`. Newer glibc retains its native spawn API.
The glibc 2.31 path prepares executable candidates before fork, blocks signals
through child setup, preserves the previous signal mask and ignored signals,
and resets caught signal handlers before exec. It maps the requested descriptors
and keeps the worker in its own process group. An error pipe reports setup and
exec failures before publishing the PID, and failed children are reaped.

Descriptor closure happens in the forked child's private table. It first tries
`close_range`; the SDK's missing ARM syscall definition uses the number in the
[Linux ARM syscall table](https://raw.githubusercontent.com/torvalds/linux/v5.9/arch/arm/tools/syscall.tbl).
When the syscall is unavailable, raw `getdents64` enumeration of `/proc/self/fd`
closes the child's descriptors. Parent threads opening new files cannot race
this private-table closure. The error pipe remains close-on-exec; worker
descriptors 3/4/5 retain their intended roles. PATH lookup preserves empty
components, access-denied precedence and direct-path errors. Plain text files
without a script interpreter return ENOEXEC rather than invoking a shell.

Three SDK descriptor tests pass: the normal backend, forced fork and forced
proc enumeration. They exercise the actual `/proc/self/exe` worker path,
concurrent non-CLOEXEC opens, a high descriptor, descriptor contents, both
worker history layouts, process groups, null streams, signal state and PATH
and exec-error cleanup. Studio live, session and network executables each
pass the local seven-provider ABI audit with all 66 ALSA symbols and a glibc
ceiling of 2.31. The SDK, host, sanitizer, static ARM and capture-disabled
profiles build with clean terminal closure logs. An initial SDK closure warning
from filesystem clock skew is retained; no timestamp was changed to suppress it.

Integration regressions for these rebuilt profiles are in progress. This
build milestone does not establish completed SDK Studio, network, capture
or native hardware qualification. Hardware access remains on hold.

All 55 host and 55 sanitizer Studio/spawn integration checks pass. The static
ARM profile passes 19 of 20 checks; its native spawn backend fails the missing
executable error case because QEMU returns success followed by child exit 127.
A standalone control reproduces that result with unmodified `posix_spawn`,
while the host returns ENOENT. This is consistent with
[QEMU 8.2.2's vfork emulation](https://raw.githubusercontent.com/qemu/qemu/v8.2.2/linux-user/syscall.c),
which removes CLONE_VM when converting vfork to fork. This error-reporting case
remains unverified for the static ARM native backend; the assertion is retained.
Both forced fork/proc backends pass, as does the SDK's glibc 2.31 backend.

The first 51-check SDK runtime/Studio selection finished with 49 passes and
two failures: the known QEMU subreaper assertion and a Studio project-path
directory-count assertion. The complete attempt, input receipt and failed
project ELF are retained under `build/spawn-first-sdk-project`. A plain rerun
reproduced the project assertion while a host directory snapshot confirmed
that the replacement private temporary file still existed.

An independent directory control creates 20 small files. With the SDK's
narrow interfaces, `readdir` returns zero entries and `EOVERFLOW`; `readdir64`
reads all 20. Building the same control with `_FILE_OFFSET_BITS=64` makes
ordinary `readdir` read all 20 with no error. The directory offsets exceed the
narrow signed offset range, while file inode numbers and sizes fit in it.
The MiSTer SDK toolchain now applies the wide metadata definition to both C
and C++ through CMake directory definitions, including existing build trees.
The SDK rebuild and terminal closure completed cleanly. The unchanged project
assertions pass in the focused rerun, including preservation of replacements,
symlinks and FIFOs after interrupted private writes. All six rebuilt
executables (player, cartridge service, capture probe, Studio frontend,
Studio session fixture and network fixture) pass the seven-provider ABI audit
with all 66 miniaudio ALSA symbols and the glibc 2.31 ceiling. The 51-check
selection is running again against these exact new ELFs. This does not
establish native filesystem or hardware behavior.

In the wide-profile rerun, the FFT binding batch reached CTest's 240-second
outer deadline after 323 completed valid cases, each reporting exact dispatch,
arguments, result and peer isolation. The fixture requires 392 valid and 40
invalid cases, so this run does not qualify the complete FFT batch. The
completed timeout excerpt and its ELF/input hashes are retained in
`build/spawn-wide-fft-timeout.json`. The full SDK selection subsequently
finished with 49 passes, the known subreaper failure and the FFT timeout; its
complete evidence is retained under `build/spawn-first-wide-timeout`.

The outer FFT batch limit is now 600 seconds when cross-compiling with an
emulator, and remains 240 seconds otherwise. Worker limits and assertions are
unchanged. Reconfiguration and clean terminal closure preserve all 248
fixture/ELF hashes and all 165 generated-source hashes, with only the CMake
test-budget source changing. The focused FFT rerun completed all 392 valid
and 40 invalid cases. SDK HTTP, the separate orphan-worker check and all five
source-packet transport scenarios (accept, latest, reset, crash and cancel)
also passed. All six SDK base-contract checks passed. The original 51-check
run remains recorded as 49 passes and two failures; the complete FFT rerun
qualifies that batch separately, leaving the QEMU subreaper case open.

Real ALSA 1.2.4 capture also passes through the unmodified SDK cartridge
service and supervised Studio frontend using known stereo F32LE PCM at
44100 Hz. A 0.25-amplitude tone at 689.0625 Hz produces rounded raw FFT bin
32 magnitude 512. Cartridge selection, rejected-candidate recovery, reset,
FPGA reload and Studio RUN retain the expected data while paced DDR playback
and clean departure pass. Independent silent-PCM controls are rejected by
the service and Studio FFT oracles. The recorder adaptations replace only
fake-device plumbing; capture and ALSA providers are unchanged. This is
synthetic PCM through real capture code, with physical microphone availability,
exclusive hardware PCM, unplug/replug and native timing still unqualified.
The first failure logs, binaries' input receipt and original test source are
retained; a diagnostic-only change prints the actual return code and PID.

The sealed local record `build/spawn-qualification.json` binds these sources,
fixtures and logs; its independent audit completed 2,815 live/archive hash
comparisons. It records 50 effectively qualified SDK checks from the 51-check
selection after the separate FFT completion, with native subreaper behavior
still open.

During this local follow-up the human availability reply was
"I'm at work right now. Not available." The device hold reflected that reply.
All 24 SSH connection sites in
22 development/install/native-test scripts check the hold before opening a
connection, including reconnects. The generic SSH CLI also checks before a
password prompt. Fake-client tests cover zero held connections and commands,
normal pass-through after release or for another host, and propagation of a
connection failure without retry. AST comparison preserves the original
authentication, timeout and reconnect arguments; all 65 Python tools compile.
The command observer's delayed-output, error, timeout and no-retry tests pass.
No device contact was made for these checks. This tool-only follow-up leaves
the SDK runtime binaries unchanged and does not establish new native results.

## Native SDK qualification resumed on 2026-10-03

The human explicitly renewed device availability with "mister is available
now." The prior hold is archived and the active hold is released. The native
board runs Linux 6.18.38-MiSTer and glibc 2.31. Read-only snapshots retain the
actual seven runtime provider aliases and fourteen companion Main provider
aliases, including symlink provenance. All six SDK image ABI audits pass
against the board's providers, with the glibc 2.31 ceiling and all 66 ALSA
imports. The companion Main audit resolves its full provider closure and
strong imports. These audits qualify compatibility with this filesystem;
they do not establish physical capture hardware.

Twelve unchanged SDK contracts pass on the board: source packets, memory
equality, pacing, video, cartridges, exchange, three spawn backends, worker
lifecycle, Studio project filesystem operations and Studio sessions. The
native worker contract verifies the subreaper behavior unsupported by QEMU.
The unchanged static ARM native POSIX-spawn fixture also passes its missing
executable/error/PID and cleanup assertions. These separate hardware results
resolve the two emulation limitations for the tested fixtures; the historical
QEMU failures remain recorded. The first pacer attempt reached an overly
short harness deadline; its later complete native run passes with the same
ELF and assertions and a 120-second outer allowance.

Native staging uses an isolated directory on the SD card rather than the
251-MB `/tmp` tmpfs. An initial partial upload exhausted tmpfs before execution;
only that run's owned partial files were removed. Original installed files
have verified local and SD rollback copies. Replacing a running Main by rename
made its lazy `/proc/self/exe` pathname end in ` (deleted)`. Its next restart
could not exec that pathname and triggered Main's reboot fallback. Recovery
restored the original files and Tetris. The corrected handoff enters MENU,
stops the exact inspected Main PID and explicitly starts the replacement from
`/media/fat/MiSTer` before issuing an MGL core load. Merely starting Main with an
RBF argument does not reprogram the FPGA. This handoff and verified rollback
both pass without rebooting the board.

The candidate Main (`f83eb786...`), RBF (`ec1aadb6...`) and SDK service
(`acbd070c...`) run together. A read-only native source-packet check verifies
TSN1, matching acknowledged cartridge/source tickets and the exact original
Tetris filename. Reads use `devmem`'s mapped word access; ordinary `/dev/mem`
`read()` fails on this reserved DDR range. That initial instrumentation failure
and a stale startup-log race are retained, with successful rollback. Fresh
startup logs plus a live player PID resolve the race. The ten-second transport
check has 52 coherent samples and zero underruns. Eight ADV7513 automatic CTS
measurements all infer the requested 74.25-MHz pixel clock for 720p/60.

The exact candidate combination passes a 600.062-second music soak with 3,047
coherent samples, zero startup or steady playback underruns, a measured
47,999.141-Hz playback clock and 36,003 presented game/carrier frames.
Fitted audio-queue change is -0.080 ms and video/audio phase span is 800 sample
slots. Steady supervisor/worker RSS is flat at 4,704/5,576 KiB, with five/four
descriptors. SSH reset during the final memory observation, and the immediate
observer reconnect timed out. A later read-only connection found the same
supervisor and worker, with the original bounded monitor completed at status
0. The full remote and local journals have identical sample values; their
text hashes differ because the observer reserializes JSON and uses Windows
line endings. The monitor was started once and never rerun. The acceptance
block is reused unchanged, including queue drift, clock, video alignment,
memory and descriptor limits. The last completed memory observation is at
570 seconds. Earlier save CRC/monotonic checks executed during the original
run; their in-memory history was lost with the observer. The recovery record
retains pre/post-MENU final saves. The final CRC is valid, BOOT is one and the
largest game-tick gap is 37 ms. The cartridge kept running during observer
recovery, so the final save extends past the measured ten-minute interval.

The human confirms both "Picture is correct and stable" for Tetris and
"Picture and stereo audio are good" for the music test on this exact SDK
candidate at HDMI 720p/60. This does not qualify analog output or capture.

The SDK service also passes four cold-entry/MENU cycles, each with sixteen
same-core FPGA reloads. All 64 reloads replace the worker while preserving
the supervisor, retain CRC-valid saves and increment BOOT exactly once.
The final BOOT count is 68. Descriptor counts remain at five; the unchanged
128-KiB post-warmup memory plateau check passes in every cycle. Complete
service/worker departure takes 0.191–0.207 seconds after MENU. The Frontier
daemon and candidate hook hashes remain unchanged.

A separate bounded native SDK Studio Tetris run completes 900 frontend ticks
in 15.067 seconds, with 899 completed asynchronous worker ticks, no waiting
frames and no recovery. Mean frontend work is 3.097 ms, maximum is 14.719 ms
and no frame exceeds the 60-Hz budget. Its ten-second transport observation
contains 501 coherent samples and zero underruns. The temporary Studio hook
is restored to the exact candidate service hook afterward. This qualifies
the bounded run, not the untested native OSD/dialog/reset paths or launcher
adoption.

The companion bounded hang test also completes 900 frontend ticks, with one
worker recovery, 36 waiting frames and 862 completed worker ticks. It returns
to mode 1 after the hung cart, with no frontend error and zero underruns in
501 transport samples. This fault interval includes fifteen frames exceeding
the frontend work budget and a 29.334-ms maximum; it is recovery evidence,
not a claim of uninterrupted 60-Hz editor work during a hang. The service
hook is restored and candidate Tetris restoration is requested afterward.

The subsequent native fourteen-language sweep completes Lua, JavaScript,
MoonScript, YueScript, Fennel, Scheme and Squirrel. All seven live 256x144
captures match a retained desktop pose exactly across every eight-bit RGB
channel, and each two-second playback journal has zero underruns. The Python
MGL selection then fails with `FPGA heartbeat stopped` before a ready player
appears. The published heartbeat, playback counter and audio-slot counter
remain frozen in six read-only observations, while the source/cart tickets
match and the SDK service file hash is unchanged. The player has exited.
Four transmitter CTS measurements still infer the correct 74.25-MHz HDMI
clock; a correct HDMI clock does not imply that the core is making progress.

Main also fails to process a MENU request. A targeted restart of the inspected
candidate Main, omitting the failed Python MGL, does not recover command
handling; the next FIFO write times out and is not retried. Exact original
Main/launcher files are restored only after stopping the inspected candidate
process and verifying both backup hashes. Original player/RBF hashes were
unchanged throughout. One clean recovery reboot is dispatched after verifying
all four original installed hashes. The native/Python and PNG sweeps are
incomplete, the freeze's cause remains unresolved, and this candidate is not
accepted for permanent installation. The earlier bounded successes remain
qualified at their stated scope; they do not override this failure.

After recovery boot, all four original installed hashes and the actual running
Main executable match the baseline. Tetris is restored through the original
launcher. A fresh two-second monitor confirms an advancing heartbeat and
playback counter with zero underruns, and four automatic CTS measurements
confirm 74.25 MHz. The human confirmations above concern the candidate's
earlier Tetris/music tests; final restoration is verified automatically.

`build/native-sdk-qualification.json` retains the separate native contracts,
provider snapshots, measured soak/reload/Studio successes, seven exact demo
captures, failed-state registers and recovery records. Its source and evidence
archive is independent of the preceding immutable local qualification. It
explicitly marks overall candidate acceptance false because the MGL freeze
is unresolved. Known-PCM local capture evidence is not converted into a
physical microphone pass.

The human reports "No capture device available." `/proc/asound/cards` lists
only Dummy. Physical microphone FFT, exclusive capture, unplug/replug and
native capture/playback timing therefore remain unqualified. Known-PCM local
ALSA tests remain separate evidence and are not physical microphone tests.

## Diagnostic follow-up

A subsequent diagnostic rerun uses Main
`15d310b846e7da027d246279a6f53197601416c6cb81a18f7ef6aff28a9d3b00`,
with unchanged SDK service `acbd070c...` and FPGA `ec1aadb6...`. Its isolated
source diff contains only the version date, unbuffered startup output and
repeated stderr reports when a normal SPI handshake stalls. It preserves the
handshake wait and all hardware acceptance assertions. The diagnostic Main
resolves all strong imports against the fourteen captured board providers.
Main's configuration redirects most regular stdout to `/dev/null`; the
diagnostic stderr reports remain available independently of that setting.

The focused Squirrel-to-Python selection and a subsequent full fourteen-language
native sequence complete. Every live 256x144 capture matches a retained desktop
pose exactly across all eight-bit RGB channels. Each two-second playback journal
has zero underruns and more than 90,000 advancing PCM frames. No SPI stall report
is observed in the retained diagnostic log. This demonstrates that Python can
run on this service/FPGA profile; it does not identify or fix the earlier freeze.
PNG coverage and production Main switching qualification remain incomplete.

After the reruns, the first rollback connection times out before any remote
command executes. A separate read-only connection also times out before command
execution; both failures are retained in the follow-up receipt. The next rollback
connection succeeds normally, with no recovery reboot. All four baseline files
and the actual running Main are verified. Original Tetris has an advancing
heartbeat/playback counter, zero underruns in a fresh two-second observation,
and four correct 74.25-MHz transmitter CTS measurements. The human did not watch
the original failed selection, so no physical failed-state display claim is made.

`build/native-sdk-freeze-diagnostic-qualification.json` preserves this separate
diagnostic evidence and the earlier qualification receipt unchanged. Candidate
acceptance remains false. The extra read-only `ps` column filters in the first two
diagnostic observations returned no rows and are not process-absence evidence;
the final restoration check uses explicit Main PIDs and executable hashes.

## DDR reset follow-up

The actual core and pinned platform terminator reproduce two independent
protocol failures under DDR backpressure. With the old core and a corrected
terminator, reset withdraws an outstanding command before the platform captures
it. With the corrected core and the original terminator, a stalled single/final
write beat is dropped. Counterexamples are retained separately in
`build/ddr-reset-core-counterexample.log` and
`build/ddr-reset-platform-counterexample.log`.

`tic80_video_top` now samples the core reset through four synchronous stages,
allowing the platform's two reset stages and takeover edge to preserve the
outstanding command. The local platform terminator override completes both
normal and cancelled writes only after the final beat is accepted. The combined
simulation passes 100 read/write/cart/frame/audio reset cases, including a
one-clock relative reset-sampling delay. Separate 64-bit and 128-bit burst tests
each pass 60 cancellation/backpressure cases with a subsequent fresh write.
Existing DDR, audio and combined integration regressions also pass.

The first revised fit fails HDMI setup timing and is retained as a rejected fit;
it is not loaded on the board. Seed 13 produces RBF
`c4284f26597941c05659477820f02d823d787ed71c904fb473a4cc5e0b2cda3e`
with all 140 timing checks passing and minimum slack 0.107 ns. The fitted
netlist preserves all four core reset stages and both platform RAM1 reset
stages on the same 105-MHz DDR clock. Audio/control CDC and shared video/playback
clock audits also pass. No clock requirement is relaxed to obtain this fit.

The revised FPGA is staged separately with production Main `f83eb786...` and
unchanged SDK service `acbd070c...`. Initial Tetris playback advances with zero
underruns, eight transmitter CTS samples infer exactly 74.25 MHz, and the human
confirms "Picture is correct and stable" at 720p/60. Native, compressed-PNG and
legacy-PNG sweeps now also require exact source path/size/ticket agreement; PNG
selection uses Main's real extension index 64. All fourteen languages pass each
of the three formats (42 cases). Every full 256x144 eight-bit RGB capture matches
a retained desktop pose, every source ticket/path/size agrees, and every
two-second audio observation has zero underruns with over 90,000 advancing
PCM frames. This uses production Main, rather than the log-only diagnostic Main.

The reset-fixed music soak completes its original 600-second monitor once:
3,047 samples, zero startup or sustained underruns, 36,003 advancing game/carrier
frames, a fitted queue change of -0.456 ms, and an audio/video phase span of
800 samples. CRC saves have one BOOT and a maximum game tick gap of 40 ms.
SSH resets during journal polling and its immediate reconnection times out before
executing any remote command. Later read-only recovery verifies that the original
monitor kept advancing and completed with status zero. Its full recovered sample
record matches the retained 1,503-sample local prefix. A clean MENU exit flushes
the final CRC save. The memory/save observer has a gap from 270 seconds to the
final observation; collected samples pass the unchanged memory/descriptor/save
checks, but memory was not observed continuously during that gap.

Four cold-entry/MENU cycles with sixteen reloads each also pass. All 68 BOOTs
are accounted for, CRC saves remain monotonic, same-core reloads preserve the
supervisor and replace the worker, and steady descriptor/RSS checks pass. MENU
exits measure 0.226, 0.422, 0.519 and 0.268 seconds. SSH closes during a read-only
parent query after the second cold entry; inspection verifies that entry already
completed with BOOT 18. The test resumes from that point with its original
counter/save, without repeating the dispatched load or relaxing acceptance
assertions. The first cycle's MENU record is reconstructed explicitly from the
retained original stdout and exit log; subsequent progress is saved after MENU.

An unchanged SDK Studio frontend now also completes a native loading-path test.
Synthetic Linux keyboard events travel through Main/HPS/DDR to enter an edit,
cancel the actual unsaved-changes dialog, and save the retained edit to its
original native source. Subsequent native and compressed-PNG MGL selections
carry exact acknowledged source paths and run once. A raw FPGA reload resumes
the PNG with BOOT two; editing and default Save then update that original PNG
while preserving its format. Studio handles four FPGA reconnects and leaves
MENU cleanly with no recovery error. This is synthetic keyboard transport,
not a physical keyboard pass, and does not cover accepting an unsaved dialog,
queued selections, worker failure during a dialog, or storage failure.

The first harness incorrectly requires its working directory to be empty;
pinned upstream Studio always creates `.local/` during initialization. The
native source/code and PNG-format assertions already pass before that folder
assertion fails. The original harness/result are retained. Read-only inspection
finds exactly `.local`, rechecks both edited original cartridges, and confirms
the completed frontend's clean exit. The corrected gate permits only `.local`
and the completed original run is qualified without repeating its core loads.

Broader Studio qualification of this build continues. The reproduced protocol
violations are plausible contributors to the earlier freeze, but the original
freeze's exact cause has not been conclusively established.

Bounded Studio playback and deliberately hung-worker recovery also pass on this
FPGA: each run completes 900 frontend ticks, and each ten-second playback
observation has zero underruns. The normal run completes all 899 asynchronous
ticks without waiting; the fault run records one recovery and 36 waiting frames.
Service tests reject a hung candidate, resume the cached game, preserve the last
completed tick (two) after a later hang, reap that worker, and load a fresh game.
The first service fault observer loses SSH during a read-only parent query;
inspection confirms continuing playback, and a fresh UUID diagnostic passes.
Read-only reconnection does not retry cartridge dispatches or uploads.

PNG recovery rejects corrupt images and an oversized extracted cartridge,
resumes/saves the cached game, shares persistent data with a native cartridge,
and loads the exact 4-MiB extracted boundary. The shared-save BOOT count reveals
an additional switching limitation: MGL `delay="3"` means three seconds, and
Main releases core initialization before its delayed file dispatch. The runtime
first restarts its cached cartridge and later initializes the selected file;
a same-save switch records BOOT five after the earlier rejection recoveries,
instead of four if that action booted once. Existing recovery assertions check
monotonic saves, not once-only BOOT during delayed MGL selection. These bounded
recovery passes do not qualify that behavior as correct. A pending-load/reset
handoff between Main and the runtime remains required; increasing waits or
loosening BOOT assertions would not fix it.

Earlier immutable qualification receipts and candidate bundles are preserved;
the new build is not accepted for permanent installation from these initial
results. Physical microphone testing remains unavailable.

## Outstanding completion gates

The next MGL candidate adds a temporary Main status-bit overlay for the first
native/PNG file action. It holds the cached VM during MGL's delay and preserves
the saved user reset setting. Completion, including a missing file, drops the
overlay. The FPGA holds the real SPI download handshake until cartridge/source
DDR writes and ticket publication finish, and pairs status with the captured
cartridge metadata during DDR stalls. This is a Main/FPGA change; SDK service
and Studio runtime code/binaries remain unchanged.

The real Main status sender, completion fragment, SPI implementation and pinned
`hps_io` pass a transport regression with a 500-cycle final-write stall. The
old loader returns early and fails it. A second regression stalls an old DDR
metadata write while reset releases: the old DDR controller pairs that old
ticket with released reset and fails; the revised controller passes. Current
cart, DDR, combined transport, audio and 100 reset-handoff regressions pass.
The new fit passes 140 timing checks (minimum slack 0.119 ns), fitted CDC,
shared video/playback clock and DDR reset audits. Main's complete build and
14 actual-board library providers pass ABI checks.

The first hardware MGL check rejects that initial Main candidate: it records a
cached Tetris restart before the PNG file arrives. Main's real MGL parser stores
file type as uppercase `F`; the initial helper and synthetic test fixture used
lowercase `f`. The failed candidate, live log/save evidence and verified rollback
are retained in `build/mgl-first-hardware-failure`. The corrected regression
parses a real MGL with the actual pinned Main XML/parser code before calling the
actual status sender. The retained lowercase helper fails this regression; the
uppercase helper passes. The parser-corrected Main was built separately;
the fitted FPGA logic remains unchanged.

The parser-corrected Main (`607120d3`) and fitted FPGA (`e7e5ed90`) pass live
single-BOOT checks with the unchanged SDK service. Corrupt and oversized PNG
rejections retain BOOT one; a later valid native selection advances it exactly
once. The exact 4-MiB extracted boundary, missing initial MGL path, raw FPGA
reload, two-file MGL, and hung-candidate/recovery cases also pass their bounded
checks. Delayed selections visibly hold reset before the cartridge arrives.

That profile is not accepted: unchanged SDK Studio times out during an MGL
reload. Three retained read-only journals reproduce the delay. The final
journal reads only Main's specific UI/MGL scalar fields and shows the initial
file ready at about 3.67 seconds, but `MENU_INFO` blocks automatic selection
until about 11.67 seconds. Studio's ten-second deadline has already expired.
GPIO observations show Main polling throughout; this is not evidence of an
SPI acknowledgement deadlock. The fields are individually sampled, not an
atomic register/process snapshot. Original journals and observer source/binary
are retained in `build/mgl-studio-observed-13027aa2` and the two earlier runs.

A follow-up Main dismisses that information popup only when the initial TIC-80
native/PNG file action has reached dispatch state. Its requested MGL delay,
later files, other cores/actions, and other UI states remain independent. An
extracted actual Main timer/dispatch regression passes with pinned enum values;
the previous Main branch fails the same regression. Fresh Main `13d91917`
passes its complete build, comparison to the unpatched pin ABI and all 14
actual-board provider checks. The FPGA and SDK runtime are unchanged by this
UI fix.

On that follow-up profile, native Studio completes three OSD requests, two
loads, one unsaved-changes cancellation and four FPGA reloads with a clean core
departure. Native and PNG default saves preserve their actual source path and
edits; PNG reset advances BOOT exactly once. Separate 900-tick normal and
deliberately hung-worker tests have zero measured PCM underruns. The normal
run completes 899 asynchronous ticks with no waiting; the fault run records
one recovery and 38 waiting frames. Input is synthetic Linux uinput, not a
physical keyboard. Single-BOOT service PNG/rejection/boundary, missing-file,
raw-reload, two-file MGL and hung-worker recovery checks also pass on this
profile. All 42 language/format cases (14 runtimes in native, compressed PNG
and legacy PNG form) have exact full RGB channel matches to the desktop
references, matching source tickets and zero underruns in their two-second
PCM observations. The 64 raw FPGA reloads and four cold MENU entry/exit cycles
complete with exactly 68 BOOTs, valid saves, the retained 128-KiB memory plateau
limit and descriptor bound, and clean MENU departures. The new music soak
passes its full 600.061 seconds and 3,047 samples with zero startup/steady
underruns, a 47,999.23-Hz audio clock, bounded queue drift and video alignment,
stable worker/memory/descriptors, and a CRC-valid final flushed save. BOOT
remains one; the largest recorded game-tick gap is 40 ms. There are no SSH
observer gaps or reconnections. The completed original remote journal is
retained and its parsed rows exactly equal the local observations. New-profile
TV/stereo confirmation remains separate from earlier human confirmations.

Final Tetris verification checks the actual running Main executable, unchanged
original cartridge source/ACK, advancing playback with zero underruns and a
74.25-MHz HDMI clock at 720p/60. Its initial cold-start observer incorrectly
compares log counts across the handler's log replacement; Tetris is already
loaded. A strictly read-only recovery verifies the live source/playback/clock
without dispatching again. The failed observer and new-service log are retained.
`build/mgl-popup-qualification.json` seals 178 evidence files (all hashes
rechecked), including the original completed music journal. Bounded gates pass;
the receipt explicitly leaves broader completion and physical acceptance open.

The first language sweep's whole-log error check sees deliberate faults left
by the preceding service fault tests and stops before capturing Lua, which
has loaded. The fresh sweep enters MENU and starts a new service, preserving
the same whole-log error assertions. No runtime or Main change is made to
accommodate that observer failure.

The default-console launcher is a separate, incomplete qualification. The
actual handler selects Studio by default, accepts an explicit player setting,
and uses quoted paths derived from its SD layout. The local launcher test
executes that handler with fixture executables, checking arguments, log
rotation, exit status and invalid/missing frontend handling. Hardware tests
use a private SD layout through Frontier with the unchanged SDK runtime,
Main `13d91917` and FPGA `e7e5ed90`; they supply no initial CLI cartridge.
Cold console startup has a captured 256x144 image and zero underruns in the
two-second observation. Native and compressed PNG selections advance the
shared save's BOOT exactly once in the first run; legacy PNG also reaches its
expected BOOT before the audio predicate fails. That first failure's raw PCM
snapshot was not retained, so its magnitude and timing are unknown.

A replay retains a cold native startup observation with 115 cumulative
underrun slots and no additional underruns during the following two seconds.
Another replay journals cold startup and same-core reloads. Its PNG reload
records 197 underrun slots in the new FPGA session while reset remains held
and the cartridge ticket remains zero, before PNG selection. The observer
reads memory without writing control registers; audio coherence is checked,
but status and cartridge fields are individually sampled. This bounds the
failure to the initialization/recovery interval in that run, without proving
which operation causes it. The original journals and observer source/binary
are retained in `build/studio-launcher-trace-2690d2ab`. Failed tests keep the
zero-underrun predicate, restore the prior handler and request Tetris. Earlier
sealed qualification receipts retain their original, narrower scope.

Timing diagnostics separately retain an instrumented frontend and compare
thread CPU time with elapsed time. The audio producer and Studio worker both
run at nice -10 in the original runtime. Studio now drops the inherited boost
in its worker and recovery helper, retaining normal or user-selected lower
priority; the producer stays boosted. A root-run regression checks initial
startup and three replacements. Rebuilding the exact frozen session source
for that regression fails the initial worker-priority assertion; the new
session passes. All 18 selected pacing, supervision, live/OSD, reset and reload
checks pass. The new SDK build changes only the session archive and resolves
all imports against the captured actual-board providers.

The priority-only Studio `b48369d7` passes one complete default-launcher run:
console startup, three cartridge formats, exact shared BOOT counts one through
four, a raw reload and clean MENU departure. All two-second playback snapshots
and retained transition journals have zero underruns. That is not sufficient:
the longer replay fails on its third selection, legacy PNG, with 856 cumulative
underrun slots in the new session. Its journal places the increase before the
cartridge ticket arrives, while reset is held. The original failed run is
retained in `build/studio-launcher-priority-stress-b42ee042`; the restored
handler, actual Main, Tetris source/ACK, advancing zero-underrun playback and
74.25-MHz HDMI clock pass a separate read-only restoration check there.

Studio also now retains two audio ticks rather than one when awaiting a reply.
A recovery ACK can copy a full cartridge checkpoint in addition to its screen;
the reply's processing cost remains after the wait ends. An independent DAC
timing model includes occasional 20-ms checkpoint work: the previous one-tick
reserve underruns, while the two-tick reserve passes. The revised frontend
passes 17 selected pacing, live/OSD, reset and reload regressions. Fresh SDK
Studio `d9d4773e` passes its actual-board import audit. Its hardware replay also
fails on legacy PNG, reporting 204 underrun slots while reset is held before
cartridge delivery. A separate diagnostic splits response handling, audio
submission and video publication: status-message handling takes up to about
20 ms, before PCM submission. Those messages call synchronous stdout writes
on the playback thread. The diagnostic retains source/binary, CPU-time and
elapsed-time observations in `build/studio-launcher-reserve-profile-f7a84586`;
its added profiling output also uses stdout, so it is diagnostic evidence,
not performance qualification. Neither earlier priority nor reserve candidate
is accepted as a complete fix.

Studio now queues its frontend status messages to a bounded background writer
at nice 19. The single producer and consumer use lock-free counters; the
producer never waits for file I/O or a mutex. Overflow and failed writes are
counted. After playback closes, shutdown drains queued records. The blocking
sink regression holds a real stdio callback, confirms prompt producer return,
then checks bounded overflow and ordered draining. All 18 selected logging,
pacing, live/OSD, reset and reload checks pass. SDK Studio `be29f5cb` includes
the new logger archive and passes the captured actual-board import audit.

That build's default-launcher hardware run completes twelve MGL selections
across native, compressed PNG and legacy PNG, plus a raw FPGA reload. Shared
BOOT advances exactly from one through thirteen; the final save has valid CRC.
All fourteen two-second observations (console plus thirteen actions) report
zero cumulative underruns, and all three retained transition journals also
report zero in their coherent snapshots. Worker priority is zero and frontend
priority remains -10. The parent survives same-core reloads; the daemon is
unchanged, no log records are reported lost, and MENU departure is clean.
The frontend records 8,400 paced frames over 142.174 seconds, with 12 OSD loads
and 12 FPGA reloads. The prior handler and Tetris are restored and independently
verified read-only, including actual Main, source/ACK, advancing zero-underrun
playback and a 74.25-MHz HDMI clock. Evidence is retained in
`build/studio-launcher-logging-stress-a3af7678`. This is bounded startup/switch
qualification; a new music soak and physical audio check, full installation,
and broader completion gates remain open.

A fresh 900-frame check of `be29f5cb` exposes a separate timing failure:
779 game ticks complete and 120 frames use fallback silence, despite zero DAC
underruns. Its hang test recovers once with zero underruns, but that does not
qualify uninterrupted normal playback. The original failed journal and log
are retained as `build/logging-studio-live-8b4236ac.*`. Reducing the reply-copy
reserve to 1,200 sample frames (`9c785809`) improves the result to 876 completed
ticks and 23 fallback frames; it still fails the unchanged requirement of 899
completed ticks and zero fallback frames. Evidence is retained as
`build/wait-studio-live-27c2cb36.*`. Earlier sealed startup/switch receipts keep
their bounded scope; neither result establishes acceptance for normal music.

Three-tick queue candidates also retain their failures: `852215e7` primes the
DAC after initialization but completes only 894 ticks, with five fallback
frames (`build/queue-studio-live-f836cf77.*`). `51824786` keeps the same queue
with a 25-ms copy reserve and completes 898 ticks, with one fallback frame
(`build/credit-studio-live-a58b80f9.*`). Both have zero measured DAC underruns
and pass the 18 selected local regressions. A separate gameplay fallback
counter now distinguishes interrupted music from deliberate reset silence.

Four-tick Studio `f0c71b10` still records two gameplay fallback frames in
`build/buffer-studio-live-20c0ff31.*`. Moving the same 20-ms monitor to CPU 1
at nice 19 retains four fallback frames in
`build/isolated-studio-live-7017915c.*`; monitor isolation is not a demonstrated
fix. A separate diagnostic (`672b7b32`, never accepted as the product binary)
records zero fallback frames in one run. Its retained source traces add no
output until shutdown. The worker averages 9.846 ms of thread CPU time per
TIC, with a 12.897-ms CPU maximum and 28.012-ms elapsed maximum. The parent
records no interrupted polls or wait timeouts in that diagnostic run, and
one initial save-identity preparation. Evidence is retained in
`build/studio-worker-profile` and `build/profile-studio-live-f6e72a50.*`.

Initialized workers now use half the boosted producer priority: nice -5
against the producer's -10 on this MiSTer. Initialization and recovery helpers
remain at normal priority; explicit positive priorities are preserved. The
root-run priority test verifies initial startup, three replacement workers,
helper priority and positive-priority retention. The ordinary unprivileged
test correctly skips its privileged boost check. SDK Studio `305036b6` passes
the import audit and selected local regressions but still has one gameplay
fallback frame in `build/scheduler-studio-live-ee55e2c0.*`; it is not accepted.

Studio `edef2e37` keeps four ticks of audio queued, allowing a dynamic wait
of at most 40 ms while retaining 25 ms for checkpoint copying and submission.
The budget now treats two queued ticks as live PCM even before the first DAC
counter update, closing a startup oversleep case. The independent model covers
20-ms checkpoint copies, startup, counter wrap and six-hour runs at nominal
and +/-500-ppm clocks for two-, three- and four-tick queues. The one-tick-copy
negative control still underruns. All 18 selected transport/logging/pacing
checks pass; the unchanged privileged priority contract also passes as root.
Its fresh native 900-frame Tetris run completes 899 game ticks, with zero
waiting/fallback frames and zero measured underruns. The retained run is
`build/playback-studio-live-11c8c611.*`. Its longer 4,200-frame run completes
4,199 game ticks with no fallback frames, recoveries or measured underruns
(`build/playback-studio-long-edee309f.*`). One deliberately hung worker is
replaced while the DAC continues without underruns; its unacknowledged
`pmem(0,99)` value is not persisted (`build/playback-studio-live-0afeb1f8.*`).

The actual default Studio console then passes 12 MGL selections across native,
compressed PNG and legacy PNG formats plus one raw reload. BOOT counts are
exactly 1 through 13, transition and startup journals contain no measured
underruns, and the final log records zero gameplay fallback frames. Deliberate
reset and loading silence remains separately counted. Evidence is retained in
`build/studio-launcher-playback-stress-5113a1a6`.

A fresh default-launcher music soak for the same `edef2e37` binary retains
all 3,047 samples of its original uninterrupted 600.062-second monitor journal.
It records zero underruns and zero gameplay fallback frames, a 47,999.203-Hz
audio clock, 59.999-Hz actual TIC completions, BOOT one, valid save CRCs,
stable worker identity and bounded RSS/descriptor counts. Queue drift is
-0.025 ms over the observation; independent video/audio phase spans 800
samples. The human confirms a stable HDMI picture and good stereo audio at
720p/60 for this exact build. Evidence is retained in
`build/studio-playback-music-2f42a7c9`, including the fresh physical confirmation.
The handler and Tetris are restored after the test. These are bounded candidate
checks; the broader gates below remain separate.

The frozen package is subsequently installed in the canonical SD-card paths:
Studio `edef2e37`, player `acbd070c`, FPGA `e7e5ed90`, companion Main `13d91917`,
the default Studio handler and CA bundle. The running Main already matches
and is retained. The installer checks the actual executable, records each
destination's hash and absence state, stages every payload before promotion,
and backs up existing files. The actual board rollback restores the earlier
player and temporary handler and removes the newly added Studio, CA and RBF.
The package is then installed again. Settings, Frontier daemon, old bitstream,
cartridges and controller maps are preserved. The install guard also rejects
external destination/configuration changes, and an altered qualification
checksum is rejected before payload access or SSH.

The canonical startup checker initially has two predicate errors: it expects
a reload-only log message on a cold console, then counts the forked worker
as a second frontend. Both failed attempts and their drivers are retained;
the installed product binaries remain unchanged. Using the cold-start ready
message and the established process-name filter, the final check passes
cold default-console startup, clean MENU departure and delayed MGL Tetris
selection through the installed paths. Both two-second audio journals contain
zero underruns; the original Tetris source and ACK agree, the actual Main and
Studio executable hashes match, and four transmitter snapshots measure
74.25 MHz at 720p/60. The final board state is Tetris running in Studio.
Evidence is retained in `build/studio-canonical-check-final-20261003`, the two
installation plans, and `build/studio-package-safety-checks.*`. This establishes
the development installation; the broader qualification gates remain open.

The installed Studio is then exercised across all 42 combinations of the 14
runtime demos and native, modern PNG and legacy PNG formats. Each case checks
the actual executable, source path/size/ticket/ACK, a two-second playback journal
and all 256x144 RGB pixels. The first comparison incorrectly uses the player
oracle: Studio intentionally draws its upstream mouse cursor. A new offscreen
reference executes the pinned desktop Studio, including that cursor at the
observed stationary input position. No pixels are masked. All 42 cases match
the full Studio reference and record zero measured underruns. Clean departure
records zero RUN fallback frames, no recoveries and 46 accepted/loaded requests
(including the preceding selections/restorations); a fresh Tetris session and
74.25-MHz HDMI check pass. Evidence: `build/studio-runtime-197b3d80` and
`build/studio-runtime-reference-c6180035`. These common demo poses are bounded
language/format coverage, not broad API or cartridge compatibility proof.

The production persistent-memory writer's existing syscall-wrapper and SIGKILL
contract also passes on the actual exfat SD mount in a fresh private directory.
It covers interrupted/short/zero/partial writes, temporary creation, file and
directory sync/close failures, rename failures, retries, blocked-write
coalescing, asynchronous errors, before/after-rename process crashes and final
flush. Product saves and payloads remain unchanged. This injects faults and
blocks individual wrapper calls; it does not establish physical power-loss
durability or tolerance of arbitrary uninterruptible kernel stalls. Evidence:
`build/pmem-sd-faults-acf54108`, the frozen fixture and host contract log.

Playback following that test exposes accumulated underruns. The retained
read-only follow-up finds 2,567 underrun slots; the initial observer failed
before retaining its raw samples. A controlled repetition records zero before
and after the installed-file hash guard, then 4,488 underrun slots immediately
after the fixture upload. The counter stays unchanged during the save-fault
program itself and the final guard. The fixture upload is 17,916,136 bytes.
There is no observed memory pressure or producer/worker major-page-fault
evidence. This identifies the upload interval as the trigger, without proving
the particular kernel or scheduler mechanism. Evidence:
`build/pmem-sd-faults-30f12367` and `build/pmem-sd-recovery-20261004`.

A reversible live placement diagnostic moves the existing producer to CPU 1
and the worker to CPU 0. The original observation checks the source too early;
a subsequent observation finds that the MGL reload created a worker inheriting
CPU 1. Explicitly moving that worker to CPU 0 establishes the intended split.
The repeated upload/save-fault trace then has zero underruns in every measured
phase (`build/pmem-sd-faults-df37cdc9`). This alone does not prove continuous
cartridge audio. An explicit per-worker affinity API is added so initial,
replacement and cleanup workers need not inherit the frontend CPU. All 22
focused local supervision, affinity, priority, playback, reset, reload and
save regressions pass. A new frozen Studio `95c616ac`, producer CPU 1/worker
CPU 0, survives two large uploads with zero DAC underruns but completes only
3,921 of 4,199 expected ticks and inserts 278 RUN fallback frames. The strict
test fails; that build is not installed or accepted. Evidence:
`build/affinity-studio-live-e44d5d2e.*`. Further placement diagnostics continue.

Three additional fresh, bounded diagnostic sessions use the same `95c616ac`
binary and two uploads each. Moving the worker to CPU 1 yields 106 RUN fallback
frames (`affinity-studio-live-f1dfe4bc`). Keeping the split and assigning only
the worker SCHED_RR priority 1 yields 62 (`affinity-studio-live-ee75eead`). Adding
SCHED_RR priority 2 to the producer yields 11 (`affinity-studio-live-4de3277f`).
Each journal has zero DAC underruns, but each strict zero-fallback assertion
fails. These are live scheduler overrides after initialization, not permanent
policy or recovery qualification. All four sessions stop cleanly at 4,200
frames with no worker recoveries, and restore the canonical handler/Tetris.
No real-time policy change is added to the product. The separate prepared
ten-minute affinity music driver is not run because the prerequisite strict
upload test fails. The latest CPU-placement candidate remains unaccepted.

A separate 900-frame hung-worker run of `95c616ac`, using ordinary scheduling,
passes on the actual board: one recovery, zero measured DAC underruns, clean
exit, and explicit CPU 0 placement for both the initial and replacement worker
while the producer remains on CPU 1. The 33 waiting frames belong to the
deliberate hang/recovery scenario; they are not a normal-playback pass. Evidence:
`build/affinity-studio-live-6ec45281.*`. Read-only restoration checks verify the
canonical `edef2e37` Studio, original Tetris source ACK, CPU 0 placement, ordinary
scheduling, zero measured underruns and the 74.25-MHz HDMI clock. All temporary
affinity/real-time overrides end with their diagnostic processes.

The package validator is also corrected to bind every manifest component to
the qualified Main/FPGA/Studio/handler hashes and pinned player/CA hashes. The
old validator accepts a different Studio binary with an updated manifest and
the old qualified receipt in a local negative case. The corrected validator
rejects that case and all six component-mismatch cases before payload access
or SSH; the genuine package still passes. Historical qualification receipts
are preserved. Evidence: `build/studio-package-mixed-binary-negative-20261004`
and `build/studio-package-component-bindings-20261004`. This is a host installer
change, not a change to the installed payload.

A separately compiled timing diagnostic reproduces 315 RUN fallback frames
during two 17,916,136-byte uploads, despite zero DAC underruns. Its acknowledged
worker wall times average 13.74 ms and reach 125.66 ms. The baseline has 899
completions in 900 frontend frames and no fallback. Trace records are buffered
in RAM and written only after shutdown. The first diagnostic's late worker
observation and unbracketed CPU timestamps are retained as failed checks;
its missing original playback journal is not reconstructed or called a pass.
Evidence: `upload-profile-live-af2062e6`, `upload-profile-live-ac099b6e` and
`upload-profile-live-5e3fad4e` under `build`.

A private checkpoint prototype protects fully interior OS pages of the heap
cartridge, tracks writes, and retains exact comparisons for changed pages and
unaligned boundaries. It reduces baseline worker wall time from 11.52 to
7.15 ms; the ordinary split-CPU upload test still inserts 52 fallback frames.
Live worker RR priority 1 reduces that to 12; adding producer RR priority 2
still yields 13. All three strict upload checks fail. The first guard is
superseded by a variant that forwards repeated permission faults on pages
already made writable, preserving termination for attempted execution of
non-executable cartridge data. Host and ARM boundary/mutation/NX/previous-handler
tests pass. Sanitizer builds disable protection and retain full scans. Twelve
host supervision/editor/history/source/save contracts pass for each variant.
The ARM unit's initial SSH observer times out, but read-only collection finds
the original job's zero status and complete log; the fixture is not restarted.

The corrected guard passes its 900-frame hardware baseline and a deliberate
hung-cartridge run with one recovery and zero DAC underruns. A further
eight-stage diagnostic passes a 4,200-frame baseline, but a split-CPU upload
run still inserts two fallback frames and spends up to 42.87 ms inside the
Studio tick. These wall-time stages do not establish intrinsic TIC CPU cost:
the running kernel `6.18.38-MiSTer` has IRQ time accounting disabled, and its
USB/SD interrupts are routed to CPU 0. Reported thread CPU clocks therefore
do not isolate interrupt work. The kernel observation does not prove a cause
for individual stalls.

Moving both playback and the cartridge worker to CPU 1 passes two uploaded
fixtures with zero fallback and zero DAC underruns under live RR overrides
(`guard-stage-profile-live-0f10fdb7`). The corrected guard without the extra
stage instrumentation also passes two uploads under ordinary scheduling:
4,200 frames, 4,199 completions, zero fallback, zero recoveries and zero DAC
underruns (`guard-v2-profile-live-3f91bec3`). This is one bounded pass after
initialization; permanent startup/replacement placement and broader cartridge
compatibility remain unqualified. No real-time scheduling is added to the
product. Canonical Studio/Tetris restoration, CPU 0 ordinary scheduling,
zero measured underruns and the 74.25-MHz HDMI clock are verified afterward.
The installed `edef2e37` payload and previous qualification receipts remain
unchanged. The new prototype evidence is sealed separately in
`build/studio-checkpoint-upload-progress-20261004.json`.

The guard is then integrated as `src/cart_guard.c`, with a Linux unit test and
sanitizer fallback. Studio starts initial, replacement and cleanup workers
through the existing explicit affinity API, now requesting CPU 1 for hardware
playback. Address- and undefined-behavior-sanitizer unit runs pass. The selected
local regression set reports no failures: 55 tests pass and the privilege-dependent
playback-priority test is skipped on the host. The ARM candidate `113fefa1`
resolves imports against the actual Main providers. Its first 4,200-frame
upload run verifies ordinary scheduling, both CPUs set to 1, producer nice -10
and worker nice -5, but inserts one RUN fallback frame (first at 2,494) despite
zero DAC underruns. This strict failure supersedes any assumption that the
prototype's single ordinary-scheduling pass establishes a release fix.
Evidence: `build/checkpoint-studio-live-a356017e.*`. The prepared music soak
is not run while its upload prerequisite fails. The installed build remains
unchanged; recovery and further scheduling qualification continue.

The integrated candidate also passes a 4,200-frame deliberate hang test with
one recovery, zero DAC underruns and a different replacement worker PID on
CPU 1 at nice -5. The 43 waiting frames are intentional recovery silence.
A separate live nice diagnostic uses the same frozen `113fefa1` binary and
keeps SCHED_OTHER, but raises the producer to nice -20 and worker to nice -10
after initialization. It passes both large uploads, 4,200 frames and 4,199
completions with zero fallback and zero DAC underruns. This establishes a
bounded diagnostic pass, not startup or recovery qualification of the new
priority policy. The source candidate still uses its prior priorities.
Evidence: `checkpoint-studio-live-03bb76e1` and
`checkpoint-nice-diagnostic-f025d570` under `build`. Canonical restoration is
rechecked and the
integration evidence is preserved in a new receipt,
`build/studio-checkpoint-integration-progress-20261004.json`.

## Default ordinary-priority checkpoint candidate qualification

The new frozen Studio `609fcfae` keeps SCHED_OTHER and now defaults hardware
playback to CPU 1/nice -20, with initial and replacement workers on CPU 1/nice
-10. A native privileged priority fixture verifies the initial worker, three
replacement workers, ordinary-priority recovery helper and positive-priority
inheritance. The focused host checks pass the guard and affinity tests; the
privilege-dependent priority test is skipped locally and passes on the board.
The prior integration receipt retains the larger 55-pass regression set.

The new candidate passes 4,200 frames and 4,199 completions while receiving
two 17,916,136-byte uploads, with zero RUN fallback frames and DAC underruns.
A separate deliberate hang produces one recovery, a different worker PID
on CPU 1/nice -10 and zero DAC underruns. All 42 combinations of 14 runtime
demos and native/modern-PNG/legacy-PNG formats match full desktop Studio RGB,
including the cursor, with source acknowledgements and zero sampled underruns.
The final runtime log reports zero RUN waiting frames and recoveries.

An additional guard fidelity test passes 4,480 bulk-copy, overlapping-move and
fill operations on the host and actual ARM board, across unaligned OS-page
boundaries, with exact source and reconstructed checkpoint bytes. Its initial
120-second observer times out while the original low-priority unit continues.
That same unit PID is raised from nice 19 to 0 and completes with status 0;
the original job and journal are collected without restarting execution.

The uninterrupted music journal records 600.111 seconds and 3,043 samples,
including two large uploads. Audio clocks at 47,999.201 Hz and actual TIC
completion at 59.998907 Hz. There are zero startup or playback DAC underruns
and zero RUN fallback frames. Fitted audio queue change is -0.014417 ms.
CRC-valid saves, stable BOOT count, process memory and descriptors pass their
existing bounds; the Frontier daemon is unchanged. The original monitor PID
15793 completes with status 0 and its raw journal matches all collected rows.
The user confirms a stable HDMI picture and good stereo audio at 720p/60
during this candidate's music run. The driver restores the canonical handler
and requests the prior installed Tetris; restoration is independently checked.

This bounded qualification is preserved in
`build/studio-checkpoint-strong-qualification-20261004.json`. It qualifies the
new development package for installation checks; it does not close the broader
release gates below or establish physical microphone/keyboard coverage.

The matched package `studio-checkpoint-strong-package-20261004` is installed
through the unchanged guarded installer. Its first console/Tetris checks pass
the actual executable hashes, CPU 1/nice -20/-10 policy, two zero-underrun
observations, coherent source ACK and four 74.25-MHz HDMI snapshots. Two initial
observer attempts use unsupported BusyBox `ps` syntax; their failed records
and driver snapshots are retained. The corrected observer reads `/proc/stat`
and continues the existing console without repeating its core-load command.

An actual rollback restores all prior payloads and preserved settings. The
prior `edef2e37` Studio then runs Tetris on CPU 0 with zero measured underruns
and the correct HDMI clock. A distinct installation plan stages and installs
`609fcfae` again, followed by successful console/Tetris, priority, source ACK,
audio and HDMI checks. The board is left on Tetris. The new installation
receipt is `build/studio-checkpoint-strong-canonical-qualification-20261004.json`;
`build/studio-installed-latest.json` now names this installation and its verified
rollback stage. The SD-card ZIP contains the six matched payloads, installation
notes, qualification receipt and license notices. Prior receipts remain intact.

## Installed Studio lifecycle and native ARM API contracts

`tools/test_studio_lifecycle_native.py` uses the installed `609fcfae` Studio,
canonical launcher and companion Main/RBF. Its isolated counter fixture shares
a fresh saveid across native, modern-PNG and legacy-PNG containers. Four cold
console entries, 12 MGL cartridge selections and 64 raw FPGA reloads pass with
exact BOOT counts 1 through 76 and CRC-valid monotonic saves across sessions.
Each same-core reload retains its supervisor; cold entries start a new one.
The final logs for all four sessions report zero RUN waiting frames, zero
recoveries and clean departure. The installed payloads, handler and settings
remain unchanged, and the final Tetris source ACK and playback are verified.

The 80 settled playback observations contain 12,400 runtime samples with zero
underruns. Eleven original, separately journaled transition observers retain
5,852 samples across FPGA sessions; all coherent audio observations have zero
underruns. After warmup, producer RSS range is zero in every session and worker
RSS ranges are 40, 32, 24 and 44 KiB. Settled descriptor counts remain seven
for the producer and four for its worker. Each observation verifies CPU 1,
SCHED_OTHER and producer/worker nice -20/-10. This closes the bounded installed
reload stress check, rather than establishing unlimited session lifetime.

Two preceding harness attempts fail before any core-load command: the first
compares Windows CRLF against staged LF XML, and the second uploads CRLF test
XML against an LF fixture hash. Their logs, driver snapshots and partial
private staging are preserved. The corrected writer emits the exact LF bytes
that its manifest hashes; it starts a fresh, separately named fixture run.
No failed core-load action or observer is retried.

Six frozen offline API contracts then run once on the actual ARM processor
with the FPGA in MENU. Their linked libraries match the installed candidate's
archives, and each binary's imports resolve against the recorded board ABI.
The five required shared providers are rehashed on the board before execution.
The native results pass banks 0/1/7 and two-bank palette composition across all
14 runtimes, 224 SFX bounds cases and 238 SFX preset/default/override cases with
exact direct/worker PCM. WebAssembly passes 10 direct and 40 supervised probes
plus 40 seeded linked-import result checks. All 14 demos and the selected
state/isolation/error-cleanup contracts pass, followed by the full Forth stack
contract. These tests compare scripted arguments, state and PCM on actual ARM;
they make no additional physical HDMI, audible-output or capture-device claim.

The initial incremental SDK build retains a 94-ms future-timestamp warning
on Forth's generated dependency file. A later Forth build completes without
warnings before the fixtures are frozen. The original native coordinator
PID 12297 runs each unit once and exits with status 0. Original per-unit logs
and statuses are collected, then installed Tetris is restored with its actual
executable hash, coherent source ACK and zero-underrun playback verified.

Evidence is sealed separately in
`build/studio-native-lifecycle-api-progress-20261004.json`; prior installation,
playback and development receipts remain intact. Broader compatibility and
physical qualification below remain open.

1. Broader live transport fault/restart testing, cartridge switching, clock drift,
   long-session profiling, and abrupt-process-exit handling.
2. Broader physical input/device qualification and audible cartridge checks
   beyond the connected Xbox controller, optical mouse and HDMI music check.
3. Full timing/CDC and external I/O qualification for the release bitstream.
4. Supported analog modes and broader HDMI qualification; image geometry, tearing,
   audio underruns and drift, latency, and long-session stability on the board.
5. SD-card durability/power-loss testing of `pmem`, physical keyboard and broader
   mouse coverage, horizontal wheel support, live four-controller input,
   reset and launcher stress testing beyond the basic menu milestone.
6. Broader cartridge/API compatibility across all 14 scripting runtimes,
   microphone FFT capture, and the remaining console/editor/SURF features.
   Passing demos does not establish a finished console.

These broader gates remain open; the connected Xbox controller and optical
mouse have passed the basic physical diagnostic described above.
Live autosaves now use a background worker; stress its behavior on slow/failing
SD cards. TIC3 preserves complete RGB channel precision; broader display
behavior still needs qualification. DE selects the 256x144 image from the analog carrier;
the physical HDMI scaling and analog geometry still require verification.

## Horizontal mouse-wheel development candidate

The previously missing horizontal input path now has a Main/FPGA/ARM
implementation. The pinned Main staging patch captures `REL_HWHEEL` and
`REL_HWHEEL_HI_RES` from its existing grabbed evdev descriptors. Each device
uses its advertised capability to select coarse or high-resolution input;
coarse companion reports from a high-resolution device are ignored even
when they occur in a separate SYN frame. Partial detents remain per-device,
and lost-event synchronization or inactive input discards pending movement.
Normalization follows the Linux input contract of 120 high-resolution units
per detent ([kernel documentation](https://www.kernel.org/doc/html/latest/input/event-codes.html)).
Positive Linux movement remains positive in platform input, consistent with
the upstream SDL convention
([SDL documentation](https://wiki.libsdl.org/SDL2/SDL_MouseWheelEvent)). The
pinned Linux Studio reverses horizontal input in `studio.c` before editor
processing and cartridge execution. Consequently a rightward event is
negative in the cartridge's `mouse().scrollx`. The library player must apply
that same reversal because it bypasses Studio's platform processing.

Main sends a signed 32-bit delta in the TIC-80-specific UIO 0x45 transaction.
The decoder commits only a complete command/low/high packet at transaction
end; truncated, oversized, unknown, reset-interrupted and OSD-blocked packets
cannot alter the total. It retains wrapping totals rather than narrowing a
burst to a single PS/2 byte. DDR offset 224 publishes the horizontal total
inside the existing keyboard/mouse seqlock. ARM converts counter differences
to TIC's signed six-bit per-frame wheel fields while retaining excess motion.
Horizontal steps use symmetric -31..31 limits: the upstream Studio reversal
cannot represent the negation of -32 in its six-bit field.

TIC3 geometry's previously zero upper word advertises TSW1 support; the
source-path identity capability is unchanged. A new ARM frontend reads zero
horizontal input on an older FPGA, even if unused DDR contains stale data.
Older frontends continue reading the existing geometry and source metadata.
Main's extension is restricted to TIC-80 and does not replace generic hps_io.

Nineteen targeted host checks pass, including real staged Main SPI and the
pinned HPS handshake/hps_io, horizontal packet handling, all existing cart
transport scenarios and their legacy-loader counterparts, coherent DDR
publication under backpressure, Lua `mouse()` results, counter wrap/backlog,
keyboard/controller defaults and build-provenance rejection tests. Seven
build-provenance units also pass separately, including preservation of the
default build receipt when compiling into a separate candidate directory.
The complete Main and both SDK ARM frontends compile. The three ARM input
fixtures pass under QEMU with the SDK sysroot; this is emulated execution.
The final SDK build is warning-free after a prior incremental invocation's
new-target lookup and subsecond dependency timestamp failure. Those initial
logs are retained.

A read-only board preflight verifies the installed payloads/settings and
running Main, observes TIC-80 selected, and confirms `/dev/uinput` exists.
No new payload or bitstream has been deployed. The candidate uses a separate
Quartus output directory so the qualified RBF and build evidence are retained.
Actual evdev-to-cartridge board testing, fresh fit HDMI/audio verification,
physical horizontal-wheel testing and the broader release gates remain open.

The seed-13 fit is rejected for cold HDMI setup slack -0.102 ns (TNS -0.266 ns)
and never installed. A distinct seed-14 build passes all 140 reported timing
checks with minimum slack 0.119 ns, then passes the existing audio/control CDC
path, shared video/playback-clock and fitted DDR reset handoff audits. The
resulting RBF SHA-256 is
`645bf1c9e09ab9eb63ce768d8e3d6bf800e666cade8217d5c2e7bc8bfcf628af`.
The complete Main, Studio, player and three ARM input fixtures resolve against
the recorded actual board provider closure; Main adds no shared dependency.
Frozen candidate evidence is `build/horizontal-wheel-candidate-20261004`, with
the bounded progress receipt `build/horizontal-wheel-local-progress-20261004.json`.

### Horizontal wheel convention correction (2026-10-04)

The first native run, `horizontal-wheel-native-73813a5a`, failed its second
checkpoint because the diagnostic expected a rightward Linux `+3` event to
produce cartridge `+3`; actual Studio RUN correctly produced `-3`. Its raw
pmem file, event log, checkpoint, exit status and rollback result are retained.
Review of the pinned upstream Studio showed its deliberate Linux horizontal
reversal. The player bypassed that reversal, and a separate six-bit boundary
problem could reverse a `-32` platform step in Studio.

The correction preserves Studio's platform input contract, applies its
cartridge convention at both player input producers, and restricts horizontal
per-frame platform steps to -31..31 while retaining the remaining movement.
`runtime_input_frontends` executes a real Lua cartridge in both actual Studio
RUN and the library player against independently specified results. It covers
signs, counter wrap, positive/negative bursts, the 32-detent boundary and editor
scroll direction. Three host checks and two ARM QEMU checks pass. The fresh
candidate is `horizontal-wheel-direction-candidate-20261004`; Main and the
seed-14 RBF are identical to the earlier candidate. The original failed run
and original frozen candidate have not been overwritten.

The corrected native run `horizontal-wheel-native-5b4709f9` passed all 44
checkpoints, 22 in each frontend. Linux coarse/high-resolution events reached
actual cartridge `mouse()` values through Main, SPI, the FPGA and coherent DDR.
The independently checked totals cover companion suppression, retained
fractions, both burst directions, reconnect and OSD suppression/remainder
reset. Both frontends measured exactly 74.25 MHz for 720p/60; bounded audio
sampling before and after each sequence reported zero underruns. The original
Main, launcher, installed payloads and settings were verified restored, and
canonical Studio was running installed Tetris. This is bounded input-path
evidence; fresh human panel/listener confirmation and a long music run for
this candidate, broader lifecycle checks, physical horizontal-wheel hardware
and the remaining release gates are still open.

### Sustained playback and wider clock review (2026-10-04)

`horizontal-wheel-sustain-20e20e70` uses the unchanged production launcher in
a private SD-like layout with the frozen direction-corrected Main, RBF,
Studio and player. Each frontend completes its own original 600-second monitor
run: 3,047 coherent samples, zero startup/runtime underruns, approximately
47,999.22 audio frames per second and 59.999 presented/cartridge frames per
second. Fitted queue change is 0.058 ms for Studio and 0.039 ms for player.
Observed producer/worker RSS ranges after 60 seconds are zero; descriptor
counts remain stable. Save files retain valid CRCs and exactly one BOOT per
frontend, and both supervisors depart cleanly. Studio reports zero recoveries
and zero RUN waiting frames. Before/after HDMI measurements remain exactly
74.25 MHz. The canonical Main, launcher, installed payloads and settings are
verified restored, with installed Studio running Tetris.

The first Python coordinator loses its SSH connection after 270 seconds.
Its original remote monitor PID 30323 and Studio PID 29574 are verified still
live. A fresh observer resumes the same run without replaying activation,
cartridge selection or monitor launch. The complete original journal includes
the interruption interval; the interrupted local journal matches its exact
prefix. The initial coordinator result, failed rollback observation, partial
journal and recovery driver are retained. Player's separately launched
original monitor PID 11720 also completes with status 0. Evidence is sealed in
`horizontal-wheel-sustained-playback-progress-20261004.json`. The subsequent
direct human reply confirms a stable picture and good stereo for this tested
candidate at 720p/60. It is recorded separately in
`horizontal-wheel-panel-confirmation-20261004.json`; the sealed test journals
and original pending-question context are preserved. This confirmation does
not qualify the later shared-reset bitstreams.

A four-corner fitted clock review records all clock transfers, synchronizer
reports, unconstrained paths and one cut path per endpoint touching `emu`.
It identifies 829 incoming and 164 outgoing cut endpoints at each corner.
All clocks are constrained, but three external input ports and 50 output ports
remain unconstrained for setup and hold. Most identified synchronizers have no
calculated MTBF. The overall capped MTBF estimate is not full CDC evidence;
the endpoint inventory is not a proof of every source-to-endpoint path.
The inventory is `horizontal-wheel-cdc-inventory-corrected-20261004`, with
the preceding unsupported-command attempt retained separately.

That review identifies raw platform reset paths into 70 horizontal-decoder
and 18 pixel-generator endpoints. Root sources now expose and share the
existing video top's reset releases: the pixel generator uses its two-stage
video reset and the decoder uses its four-stage system reset. The DDR assertion
delay and terminator handoff remain intact. Local integration checks verify
independent domain release and delayed assertion; pixel cadence, wheel packet
handling and the real DDR terminator regression also pass. The first test
attempt exposes a fixture initialization issue and missing named connections;
both are corrected and the failed log retained. The new fit is isolated in
`fpga-shared-domain-reset-20261004`; it has not been deployed and does not
inherit the preceding candidate's native or human qualification. A new fitted
audit checks shared reset fanout and the absence of direct raw-reset paths.

The new fitted audit rejects the preceding, timing-good wheel candidate:
there are zero paths from the shared system reset to its 71 fitted decoder
consumers. With the shared-reset change, the seed-14 fit passes all eight
corner/block routing checks: 88 physical decoder consumers and 19 physical
pixel-phase consumers, including fitter duplication, receive their domain's
registered reset. Direct platform-reset paths to both groups are zero.
Maximum routed decoder reset delay is 5.122 ns against an 8.571 ns bound;
pixel reset delay is at most 3.017 ns against a 36.614 ns bound. This proves
the targeted reset-routing repair, but that fit is rejected for unrelated
cold HDMI setup slack of -0.087 ns (TNS -0.173 ns). It was never deployed.
Its bitstream and exact compiled sources remain in
`fpga-shared-domain-reset-20261004/rejected-fit.json`. The next two isolated
fits are also rejected and never deployed. Seed 15 has HDMI setup violations
at both slow corners, with minimum slack -0.392 ns and cold TNS -0.723 ns.
Seed 16 has cold HDMI setup slack -0.225 ns and TNS -2.475 ns. Each directory
retains its rejected-fit receipt, original bitstream and compiled sources.
Detailed seed-15 paths identify the HDMI scaler divider as the limiting logic.
The constraints remain unchanged. The separate seed-17 fit completes with
all 140 timing checks passing and minimum slack 0.114 ns. Its original launch
context records session 38754; that original job exits with status 0.
Observation delays do not cause a replacement compile.
The build recorder now requires a fresh eight-check shared-reset audit for
sources using these releases and rejects a retained raw path. Ten recorder
regressions pass, including older-source compatibility and separate artifact
records. The revised build driver includes the new post-fit audit.
The installed-state receipt still names the confirmed `609fcfae` build.

A private candidate lifecycle driver is prepared for four cold console entries,
twelve native/modern-PNG/legacy-PNG cartridge changes and sixty-four raw core
reloads. It reuses the verified temporary Main/launcher activation and rollback
procedure, checks actual cartridge BOOT/tick saves and samples audio during
transitions. Its unique fixture and save directory do not overlap user saves.
Preparation and Python compilation are complete. A read-only board inspection
verifies the canonical payloads/settings and running Main with TIC-80 selected.
No reset candidate has been permanently installed.

The seed-17 RBF is `ccc41285fac38df7d1e76051226bcaaf50dc7a567b92f499c0ee619fddad2884`.
All four required post-fit audits pass, including eight shared-reset checks
across the four corners. The fitted decoder has 87 physical reset consumers
and the pixel phase has 22; every consumer receives its domain's registered
reset and no direct platform-reset path remains. The maximum decoder reset
route is 4.645 ns and the maximum pixel reset route is 2.307 ns. Source-bound
evidence and the unchanged direction-corrected Main/ARM binaries are frozen in
`shared-reset-candidate-seed17-20261004`, manifest
`961008222d061bf6876cb8401dff764bb574a93aaa71f2b10bae2f4fdee6a024`.

The fresh native input run `horizontal-wheel-native-e40bdb92` passes all 44
actual evdev-to-cartridge checkpoints on this RBF. Both frontends measure the
expected 74.25 MHz HDMI clock and zero bounded audio underruns. Original Main,
launcher, installed payloads/settings and installed Tetris are verified
restored. Its sealed receipt is
`shared-reset-seed17-wheel-native-progress-20261004.json`. This proves the
bounded input path on the new fit; it does not inherit the older RBF's human
confirmation or sustained-playback result. The subsequent production-launcher
lifecycle run has begun separately with private fixture
`shared-reset-lifecycle-fixture-52856024`; its result remains pending.

A fresh four-corner inventory for this fit is sealed in
`shared-reset-cdc-inventory-seed17-20261004/summary.json`. It samples 693 incoming
and 164 outgoing cut endpoints at each corner, with no sampled raw platform
reset source. Because it reports one path per endpoint, this does not prove
the absence of every possible raw-reset path. Three input ports and 50 output
ports remain unconstrained for setup/hold; full CDC and external I/O acceptance
remain open. The separate reset fanout audit proves the targeted repair.
`shared-reset-cdc-and-running-lifecycle-progress-20261004.json` preserves a
checkpoint from the original, freshly verified live lifecycle process. At that
checkpoint, one full cold cycle and 33 actions pass; the run and its rollback
are incomplete. No replacement job has been started.

That original lifecycle driver subsequently exits with status 1 at the third
cold console, after two complete cycles and 38 exact-BOOT actions. Its extra
`maximum_error_ppm == 0` assertion rejects an ADV7513 readback that alternates
by one CTS count. The established HDMI analyzer passes those same four raw
samples: 74,250,000--74,251,000 Hz, maximum deviation 13.468 ppm, within its
existing 1,000 ppm limit. VIC 4, PLL lock, automatic CTS and undivided pixel
input all pass. This is an overstrict fixture assertion, not evidence of the
previous 148.5 MHz mode failure. The original result, raw readbacks, driver,
foreground log and verified rollback are sealed in
`shared-reset-candidate-lifecycle-a9964233`. The failure remains a failed full
lifecycle run; its partial passes do not qualify the complete lifecycle.

The corrected fixture uses the established analyzer limit without changing
its implementation, FPGA constraints or clock-generation logic. A regression
replays the actual one-CTS-step capture; all four HDMI analysis checks pass,
including rejection of the earlier 148.5 MHz output and lost-lock cases.
A new lifecycle run has begun with private fixture
`shared-reset-lifecycle-fixture-12b3fc28`, using the same frozen seed-17 candidate.
Its original process/session are 89044/32154; it has not completed. The fresh
sustained-playback fixture `horizontal-wheel-music-fixture-ebd4b6b6` and driver
are prepared separately; neither frontend's new ten-minute run has started.

The corrected original lifecycle process subsequently completes with status 0.
It passes all four cold entries, twelve native/modern/legacy cartridge changes
and sixty-four raw reloads, with actual cartridge BOOT counts exactly 1--76.
There are 12,400 settled and 4,555 coherent transition audio samples, all with
zero underruns. All four supervisors depart cleanly with zero recoveries and
zero RUN waiting frames. Fitted memory/descriptor checks pass. Canonical Main,
launcher, payloads/settings and installed Studio/Tetris are verified restored.
The complete result is sealed in
`shared-reset-seed17-lifecycle-native-progress-20261004.json`; the earlier
overstrict-fixture failure remains immutable and unqualified.

Development packaging and installation now share the exact six-component hash
mapping. Earlier receipts keep the original player/CA pins. New explicit
player/CA hashes must be supplied together, and the player requires its own
bounded playback qualification. Optional mapped sources remain inside the
workspace and must match the qualified component and executable role. Seven
packaging regressions pass, including changed-player and rehashed-CA rejection,
partial qualification, source escape and missing player playback evidence.
The real canonical package still validates. No new real package has been
created or adopted from this tooling change.

Fresh sustained playback is now running in `shared-reset-sustain-40eb20ff`,
using the unchanged production launcher in a private SD-like layout. The
original Studio/monitor PIDs are 8397/9151, with coordinator process/session
17188/26510. The new picture/stereo question is explicitly bound to this RBF
in `human-question-context.json`; a direct human reply remains pending.
`package-and-shared-reset-sustain-live-progress-20261004.json` verifies the
original remote monitor and local coordinator still live, and preserves 2,490
Studio samples over 490.819 seconds with zero underruns. Neither long-run
completion nor rollback is established by that partial checkpoint.

The original Studio monitor 9151 subsequently completes with status 0 after
600.0179 seconds and 3,044 coherent samples. Its music/save/clock/memory checks
pass, with zero underruns, one BOOT and a valid final save. The driver then
starts the player's separate original monitor 25804 without repeating the
Studio run. `shared-reset-studio-soak-and-player-live-progress-20261004.json`
preserves the completed Studio result and directly verifies the original
player monitor and qualified player executable still live. Player completion,
final rollback and fresh direct human picture/stereo confirmation remain
pending at that checkpoint.

The original player monitor 25804 also completes with status 0: 600.0623
seconds, 3,047 coherent samples and zero underruns. Both frontends remain near
47,999.24 audio frames/s and 59.999 cartridge/presented frames/s. Fitted audio
queue change is +0.109 ms for Studio and -0.096 ms for player; observed producer
and worker RSS ranges after 60 seconds are zero. Final saves have valid CRCs
and exactly one BOOT, with maximum observed cartridge tick gaps of 27 ms and
32 ms. Before/after HDMI readback is exactly 74.25 MHz in both runs, and both
frontends depart cleanly. Canonical Main/launcher/payloads/settings and installed
Studio/Tetris are verified restored. The original coordinator exits with status
0; no replacement monitor or observer reconnection occurs.

These complete bounded results are sealed in
`shared-reset-seed17-sustained-playback-progress-20261004.json`, with the
original jobs and direct-human question context archived in
`shared-reset-sustain-40eb20ff`. Fresh direct-human picture/stereo confirmation,
broader transport/fault coverage, full CDC/external I/O and remaining
device/API/release gates remain open. The seed-17 candidate has not been
permanently adopted from these software measurements alone.

The same frozen seed-17 candidate now passes a complete bounded player fault
suite through actual Main/MGL transfers and the unchanged production launcher
in a private SD layout. Corrupt PNG images and PNGs extracting more than 4 MiB
are rejected without an additional BOOT; the previous game resumes. Missing
initial files restart the cached cartridge once, missing later files do not
duplicate BOOT, and two-file MGLs with zero second delay load exactly twice.
A PNG extracting exactly 4,194,304 bytes is accepted. A hanging candidate
preserves the previous game, while a hang on tick 3 saves only completed tick
2, excluding the partial write of 9999. The failed worker is reaped. A new
cartridge runs after the hang, and again after an explicitly killed worker,
with the original supervisor retained and the expected save/BOOT continuity.

All twelve checkpoints pass their CRC/save and applicable BOOT assertions;
672 settled audio samples have zero underruns. Before/after HDMI readback is
74.25 MHz. The original coordinator exits with status 0, and canonical Main,
launcher, payloads/settings and installed Studio/Tetris are verified restored.
Evidence is sealed in `shared-reset-candidate-faults-692ae6bc` and
`shared-reset-seed17-player-fault-progress-20261004.json`. This qualifies the
bounded player scenarios, not Studio fault recovery or every cartridge.

Two earlier incomplete runs remain separately archived. The first observer
raced with an exiting worker's cmdline; the second assumed a task-children
kernel interface unavailable on the board, then encountered that same race
while inspecting the restored Studio. Its actual restoration was completed
and verified read-only before another activation. The fresh driver uses the
board's supported PID/PPID process listing and skips vanished worker argv
observations while retaining exact supervisor checks. Neither incomplete run
is counted as a full pass. No product binary, clock logic, constraint or save
assertion was changed to address these fixture failures.

The latest incoming picture/audio reply matches the earlier candidate prompt.
A separate clarification asks whether the later shared-reset music test was
also observed; its context is retained in
`shared-reset-panel-clarification-context-20261004.json`. That earlier reply
has not been silently used to qualify the new RBF. Studio recovery, fresh
physical confirmation, full CDC/external I/O and broader release gates remain
open; this candidate has not been permanently installed.

Studio now also passes a complete real-MGL fault suite on the same frozen
seed-17 candidate. Sixteen checkpoints cover corrupt and oversized PNGs,
retained-cartridge RUN retries, missing initial/later files, the exact 4 MiB
extracted-payload boundary, first-tick and later-tick hangs, and an explicitly
killed worker. Recovery returns to the console with completed state retained.
The later hang preserves tick 2 rather than the partial tick's value 9999.
Three Ctrl+R retries pass through actual Linux uinput, Main, FPGA keyboard
transport and Studio; saved BOOT counters prove RUN occurred. The worker-kill
retry runs the retained cartridge rather than selecting a replacement file.

The suite independently verifies both Studio queue contracts: files spaced by
three seconds BOOT exactly twice, while a zero-delay pair with a large first
payload runs only the final cartridge. The superseded identity creates no
save. This differs from the player's immediate-pair behavior above and matches
Studio's existing latest-busy-selection contract. The private MGL's second
delay was corrected before that case was dispatched; its original and actual
artifacts, hashes and correction record remain in the archive. No coordinator
or completed case was restarted for that parameter correction.

There are 895 settled and 1,233 coherent transition audio samples, all with zero
underruns, and exactly three intended worker recoveries. The supervisor remains
the same, departure is clean, and before/after HDMI readback is 74.25 MHz.
The original coordinator exits with status 0. Canonical Main, launcher,
payloads/settings and installed Studio/Tetris are verified restored. Evidence
is sealed in `shared-reset-studio-faults-64856e09` and
`shared-reset-seed17-studio-fault-progress-20261004.json`.

The restricted synthetic keyboard probe's RUN action is corrected from F5
(upstream's music-editor shortcut) to Ctrl+R. Its fresh ARM build and runtime
ABI audit are source-bound and native retries verify the change. Three earlier
incomplete runs remain archived: the wrong shortcut, a lost SSH observation
connection followed by independently verified rollback, and the wrong
two-BOOT assertion for Studio's busy queue. The completed run needs no observer
reconnection. These checks do not establish physical keyboard support, broader
storage/API/device compatibility, full CDC/external I/O or release acceptance.
Fresh physical confirmation of this shared-reset RBF remains pending.

The same frozen shared-reset seed-17 candidate now passes a bounded Studio
storage suite using private files on the actual MiSTer SD card. Seventeen
checkpoints cover a failed save rename, background retry after repair, and
four rejected save inputs: bad CRC, truncation, an unreadable directory at the
save path, and trailing data. Rejections preserve the damaged input and the
previous identity's completed save. After repair, an actual Linux uinput/Main
Ctrl+R retry loads the seeded values and advances BOOT from 9 to 10 exactly
once. The unrelated 32-bit persistent slot remains `0x89abcdef`.

The blocked rename leaves the previously completed file intact. Removing the
private obstruction lets background saving resume with later completed values
and BOOT 2, preserving the supervisor and worker. Cartridge MGL reloads normally
establish a new FPGA generation and paused worker; each read rejection and
repair retry preserves that newly initialized worker. The suite records zero
worker recoveries and a clean departure. All 951 settled and 2,127 coherent
transition audio samples report zero underruns. HDMI readback is 74.25 MHz
before and after the tests. The original coordinator exits with status 0,
and canonical Main, launcher, payloads/settings and installed Studio/Tetris
are verified restored.

Evidence is sealed in `shared-reset-storage-2167653d` and
`shared-reset-seed17-storage-progress-20261004.json`. Two incomplete runs are
separately archived: one required an optional write-error diagnostic that
status polling can suppress, and one compared worker PIDs across an intentional
MGL generation reset. Both original runs exited once, retained their failure
records and restored canonical state. Their fixture assertions were corrected
before fresh runs; no product binary or save-preservation assertion changed.

These private SD-file faults do not establish physical power-loss durability,
physical keyboard behavior, or complete storage/API/device compatibility.
Fresh shared-reset picture/audio confirmation, full CDC/external I/O and
broader release gates remain open. The candidate is not permanently installed.

The frozen seed-17 candidate also passes a real-board Studio save-read stall
suite. A private preload fixture delays one exact save-path read in a worker;
the production launcher, Main, RBF and frontends execute normally. A 500 ms
read completes without recovery and advances the seeded BOOT from 9 to 10.
A ten-second read is cancelled by the five-second RUN deadline: Studio returns
to the console, reaps the blocked worker and preserves the seeded save before
an actual Main/Ctrl+R retry. The retry advances BOOT exactly once and resumes
nonzero published PCM. Core departure during a separate ten-second read also
reaps the blocked worker and preserves the unexecuted cartridge's save.

A read-only observer independently brackets published PCM and the displayed
DDR buffer with their publication/session acknowledgements. It records 382
silent PCM snapshots and 262 matching pending-picture hash snapshots across
the three cases. Nonzero PCM is observed before each stall and after successful
RUN/retry. All 224 settled and 6,165 coherent transition audio samples report
zero underruns. There is exactly one timeout recovery, the supervisor remains
the same, departure is clean, and the original coordinator exits with status
0. Canonical Main, launcher, settings/payloads and installed Studio/Tetris are
verified restored. Evidence is sealed in `shared-reset-read-native-5150f5af`
and `shared-reset-seed17-read-native-progress-20261004.json`.

The scoped delay library passes an exact-path, one-shot host check and a fresh
ARM ABI audit against the board-provider snapshot; actual board library hashes
are verified before activation. Native observer rate checks validate bulk RAM
snapshots and normal scheduling priority on CPU 0. Four incomplete runs remain
separately archived: byte-wise framebuffer reads were too slow, an unrelated
spare-buffer publication invalidated displayed-frame observations, a low-priority
observer lost samples during SSH activity, and a finite journal ended before
the explicit retry completed. No failed run is relabelled as a full pass, and
no product binary or save/payload assertion changed. The completed suite uses
finite sixty-second observer jobs and retains the original minimum sample and
tone requirements.

This establishes bounded native five-second RUN-deadline and departure
behavior. The shorter in-RUN deadline, broader storage failures and physical
power-loss durability remain separate. Framebuffer/PCM observations do not
replace physical display/audio capture or the pending shared-reset human
confirmation. Broader API/device compatibility and full CDC/external I/O
release gates remain open.

The same frozen candidate now passes the shorter native running-tick save-read
deadline. A Lua cartridge with no explicit save ID changes bank0's map during
TIC; the pinned runtime independently derives and verifies the initial and
changed bank identities. An actual Linux uinput/Main Ctrl+R while RUN is active
must read the changed identity's seeded save. A private ten-second read stall
is cancelled by the 250 ms tick deadline. A read-only process-liveness observer
records the original worker alive and then reaped 0.232779653 seconds after
the read begins. The supervisor remains the same and recovery returns to the
console with the last completed state retained.

The old identity's save remains at BOOT 1 and completed tick 1,830; the cancelled
new identity remains exactly at seeded tick 100 / BOOT 9. A second actual
Ctrl+R from the console loads that unchanged seed, reaches BOOT 10 and resumes
nonzero published PCM. Seven coherent PCM snapshots in the guarded short
pending interval are silent, and four displayed-frame snapshots have matching
hashes. All 168 settled and 2,608 coherent transition audio samples report zero
underruns. There is exactly one worker recovery and a clean departure. The
original coordinator exits with status 0, with canonical Main, launcher,
settings/payloads and installed Studio/Tetris verified restored.

Evidence is sealed in `shared-reset-run-read-native-ef179640` and
`shared-reset-seed17-running-read-progress-20261004.json`. The running restart
does not reload an MGL or reset the FPGA session, and the injected PID is the
worker already executing the cartridge. This distinguishes the shorter tick
deadline from the earlier console RUN check. Native observer-rate and ABI
checks pass before activation; exact board provider hashes are revalidated.
This remains a bounded Lua/storage/transport check. Physical keyboard/capture,
fresh shared-reset panel confirmation, broader compatibility and the full
CDC/external I/O release gates remain open.

The shared-reset seed-17 candidate also passes the new independent-clock
video ownership simulation (`tools/test_rtl.py --suite video-cdc`, registered
as `rtl_video_cdc`). The production integration runs with independent system
and video clock edges at four phase offsets. Long DDR response stalls make
all 36 complete copies finish during active raster; 1,226,532 patterned RGB
pixels match complete frame generations, and no write reaches the displayed
bank. All 24 observed bank changes occur in vertical blanking. Twelve
one-system-cycle session flushes cover startup, a restart during copying,
and another restart with scanout acknowledgement pending.

Isolated disconnected-acknowledgement and previous-bank counterexamples fail
the progress and ownership assertions respectively. Earlier harness failures
and the less demanding guard-bypass case are retained separately. Candidate
RTL source hashes still match the fitted build; no production RTL or installed
payload changes. Evidence is sealed in `video-cdc-evidence-20261004` and
`shared-reset-seed17-video-handshake-progress-20261004.json`. These digital
checks do not model analog metastability or qualify wider external I/O.

The custom synchronizer review now audits 117 first-stage/second-stage paths
at all four corners. The frozen shared-reset fit has positive stage timing
and exclusive first-stage fanout, but automatic identification leaves MTBF
uncalculated. New Quartus settings explicitly identify the chains. Their
first seed-17 fit is rejected for HDMI setup slacks of −0.090 ns and −0.203 ns.
Only 116 of the expected 117 chains appear in its calculated MTBF reports;
the missing played-counter bit has a confirmed audio-to-system path.
Wildcard vector rates also revert to defaults in subsequent reports, and
14 paths have mixed system/audio sources requiring higher activity bounds.

Current `cdc.tcl` enumerates exact bits and sets 103 heads to 25 million and
14 to 131 million transitions/second. An actual Quartus settings export
confirms 117 unique heads and no wildcard targets. The post-fit audit now
uses a fresh process per corner and requires positive stage timing, expected
topology, calculated MTBF, explicit identification and a rate exceeding the
sum of reported source-clock rates. The parser handles the vendor “Greater
than 1 Billion” literal as a lower bound. Eleven build-record tests pass.
Two interim fits were deliberately stopped after further reporting/rate gaps
were found. The seed-20 fit, original session 2229, is pending; no new fitted
artifact is qualified or deployed. The rejected fit and diagnostics are
sealed in `explicit-sync-seed17-rejection-evidence-20261004` and linked from
`explicit-sync-seed17-rejection-progress-20261004.json`. Installed payloads,
the frozen candidate and the pending human-confirmation context are unchanged.

### October 5: stable WASM linear memory and declaration validation

An owned baseline probe forces a valid allocator relocation during real
`memory.grow`. It reports four to five pages, one allocator move and an
unsynchronized TIC RAM pointer, then exits 1 without dereferencing stale RAM.
The result is retained in `wasm-memory-growth-baseline-v2-20261005`.
The integration now reserves the documented 256 KiB backing once, changes the
logical page count within compatible module/machine limits, zeroes newly
accessible pages and preserves TIC RAM and remap pointers. Initial allocation
failure closes the partial runtime and permits a later valid cartridge.

Host contracts pass sixteen defined/imported memory cases, callback-time
growth, zero-fill, integer-overflow rejection, allocation recovery and five
supervised invalid-memory/replacement pairs. Pointer bounds, mouse bounds,
fourteen-runtime normal mouse/input and WASM result-slot regressions also pass.
The actual CMake WASM target builds and its archive passes memory/pointer
contracts separately. The earlier CTest helper fails after both registered
tests pass because its output-summary assertion expects different wording;
the original successful test output and failed helper remain preserved.

Native coordinator 24319 collects every contract phase at status 0 but exits 1
on an immediate restoration source-ticket check. A later read-only observation
verifies canonical Tetris, its settled source ticket, audio and HDMI. Fresh
coordinator 8885 adds a bounded source handoff observation, passes all native
contracts, exits 0 and verifies canonical restoration. Pure readiness checks
accept a pending-to-ready handoff and reject persistent mismatch or incorrect
cartridge size. The complete review, including the preceding failed attempt,
is sealed in `wasm-stable-memory-native-v3-progress-20261005.json` (`5147f987`).

Frozen candidate `runtime-wasm-stable-memory-candidate-seed22-20261005`
(manifest `cf661c69`) contains player `a254f7ff`, Studio `4ab49c67`, unchanged
Main `81253dc2` and RBF `fd673c65`. Original playback coordinator 21431 passes
ten minutes each of Studio/player music, save and clock checks. Their
3,045/3,047 samples have zero underruns; each has 21 checkpoints with flat
sampled steady parent/worker RSS. Audio clocks are approximately 47,999.22 Hz,
game clocks approximately 60 Hz and fitted queue changes +0.036/-0.043 ms.
Both detached monitor pairs continue through deliberate 35-second observer
disconnects. All four original jobs are collected once at status 0, the
coordinator exits 0 and canonical files/settings, Tetris, audio and HDMI are
verified restored. Independent raw review is sealed in
`runtime-wasm-stable-memory-integration-soak-progress-20261005.json`
(`afd1bae1`). This does not infer fresh human confirmation or final physical
mapping qualification.

A further owned baseline exposes four accepted declarations that should be
rejected: an imported zero maximum incompatible with TIC RAM, a minimum above
its explicit maximum, a maximum above the WASM32 limit, and a truncated
maximum. A valid imported-memory control passes; the expected failing probe
is retained in `wasm-memory-declaration-baseline-20261005`. The pinned parser
loses the explicit maximum flag and the defined-memory section ignores its
parse result. The new source-hash-checked parser adapter retains the flag long
enough to validate the supported memory types and propagates parsing errors.
Vendor files are unchanged. The [WebAssembly memory-type validation rules](https://webassembly.github.io/spec/core/valid/types.html#memory-types)
provide the limit relationships; TIC-80 imposes its separate fixed RAM limit.

The actual CMake host WASM target passes four supported declarations, twelve
rejected limits/encodings and sixteen supervised replacements, alongside all
six preceding regression groups. Its registered declaration CTest also passes
with the independently built exact-source executable. This does not claim a
full CMake build of test targets. The actual ARM CMake WASM target builds;
both preceding frontends relink exactly, and all seven new production/test
products pass the recorded MiSTer provider ABI checks. Candidate
`cf661c69` does not contain it.

Original native declaration coordinator 2851 subsequently completes all five
contract phases with all seven phase/aggregate statuses 0, exits 0 and verifies
canonical restoration. Its independently reviewed archive is
`wasm-memory-declaration-native-progress-20261005.json` (`7958fd8f`). This covers
actual ARM declaration rejection/replacement and prior memory/pointer/input
regressions; it does not qualify the production frontend worker entry points.
The first actual production-worker coordinator, 71529, passes the player cases
but fails Studio startup: the test helper intercepts `posix_spawn`, while Studio
uses `tm_spawn_closed`. This harness failure and verified canonical restoration
are sealed in `wasm-production-worker-v2-failure-progress-20261005.json`.
The corrected helper copies Studio's closed-launch request and redirects only
the executable to the production binary. Fresh coordinator 76571 exits 0 after
all sixteen declarations and sixteen valid replacements pass in each mode.
The helper verifies 32 actual player worker executable paths and one retained
Studio worker. Raw review, exact helper/payload hashes and canonical Tetris,
audio and HDMI restoration are sealed in
`wasm-production-worker-native-v3-progress-20261005.json`. These checks use the
real production worker entry points; they do not open the live backend or
inject physical input.

Frozen candidate `runtime-wasm-declaration-candidate-seed22-20261005`
(manifest `05b7f4ad`) contains parser-fixed player `6dfae0d1` and Studio
`5b4f0f57`, alongside unchanged Main `81253dc2` and RBF `fd673c65`.
Original playback coordinator 41036 exits 0 after separate ten-minute
Studio/player music checks. Its bounded reconnect, owned monitor collection and
source-handoff restoration functions are unchanged from completed coordinator
21431. Independent raw review passes 3,045/3,047 samples with zero audio
underruns, 21 memory/save checkpoints per mode and flat sampled steady
parent/worker RSS. Audio clocks are approximately 47,999.23 Hz and game clocks
approximately 60 Hz; fitted queue changes are +0.129/-0.078 ms. Both monitor
pairs continue across deliberate observer disconnects. All four original jobs
are collected once with status 0. Canonical files/settings, Tetris, audio and
HDMI are verified restored. Evidence is sealed in
`runtime-wasm-declaration-integration-soak-progress-20261005.json`.
Fresh human picture/audio confirmation and final physical mappings are pending.
The prepared final-input driver is now bound to this exact candidate and its
completed playback receipt. The human input window has not been dispatched.

### October 5: standard WASM demo exposes an imported-memory regression

Original matrix coordinator 21720 checks the parser-fixed candidate against
the frozen runtime cartridges and desktop Studio frame references. The first
ten native Studio cases match every RGB channel, acknowledge the correct
cartridge source and record zero measured audio underruns. The eleventh case,
WASM, fails: its log reports `WASM imported memory does not fit TIC-80 RAM`,
and its screenshot differs by 96,966/97,008 RGB channels from the two reference
poses. The cartridge's extracted module is byte-identical to the pinned
upstream 625-byte WASM demo. It imports `env.memory` with minimum and maximum
two pages. The import check wrongly requires a four-page maximum, although
TIC RAM fits two. This is a runtime compatibility regression, not a display
confirmation failure.

Coordinator 21720 exits 1 and verifies restoration of canonical files/settings,
Tetris, audio and HDMI. The original failed run and extracted cartridge/module
are sealed in `runtime-declaration-matrix-v3-failure-progress-20261005.json`.
Candidate `05b7f4ad` is unsuitable for packaging, and its prepared human input
driver is superseded. The earlier bounded music and synthetic worker passes
remain valid for their stated scope; they did not cover this standard demo.

The working source now checks imported limits against TIC RAM's minimum page
count and applies compatible logical sizes/maximums when loading imported
memory. It retains the stable 256 KiB physical backing. Expanded tests cover
20 memory cases, eight supported declarations, thirteen rejected declarations
and twenty-one supervised replacements. A separate full-frame test runs the
actual standard cartridge and compares its 30/60-tick RGBA output with the
independently frozen desktop frames. Original host builder 43448 exits 0:
the old archive reproduces the standard-demo rejection, while the corrected
archive matches both full RGBA references and keeps two-page RAM stable.
The expanded memory/declaration contracts and all preceding pointer, mouse,
input and WASM result-slot groups pass.

Original ARM builder 2216 exits 0 after building the actual CMake WASM target,
reproducing exact parent frontend hashes, relinking both corrected frontends
and six contract executables, and checking all eight products against the
recorded MiSTer library exports. Player `dfe36c88` and Studio `9123c34a` contain
the correction. Original helper builder 78562 exits 0 and preserves those
production binaries while building both real-worker test launchers. Their
source includes the compatible two/three-page import cases. Local evidence,
original terminal observations, source/generated adapters, baseline failure,
host frame/contracts and ABI checks are sealed in
`wasm-import-limits-build-progress-20261005.json`. New native contract,
production-worker and full-matrix qualification remain pending. Canonical
Tetris remains restored, and no final-input window has been dispatched.

### October 5: corrected ARM contracts checked offline

While the user is at work and MiSTer is unavailable, the unchanged corrected
ARM executables run under local QEMU using SDK glibc. All six original commands
exit 0: standard-demo exact 30/60-tick RGBA frames and stable two-page RAM,
20 memory cases, 21 declaration/replacement cases, pointer faults/recovery,
and separate mouse/input contracts for all 14 scripting runtimes. Binary,
reference-frame, SDK library and emulator hashes are recorded. This does not
execute the production frontend workers or qualify the physical board runtime.

Original coordinator 24684 exits 1 after running all six commands successfully:
its output reviewer omitted the two API logs. The original result and terminal
failure remain preserved. A new read-only review combines all six completed
logs, verifies every expected language/case marker and seals the original
outputs in `wasm-import-limits-emulated-progress-20261005.json`. No executable
is rerun to repair the review. The prepared six-phase native shell separately
passes `sh -n` without execution or SSH access. The native driver has not been
dispatched; hardware tests wait for MiSTer availability.

Original clean-build coordinator 2449 exits 0 after a fresh Release CMake build
of `wasm_demo_test`, `wasm_memory_test` and `wasm_memory_declaration_test`.
The two registered memory CTests pass, and the demo matches both independent
full RGBA reference frames. The generated WASM adapters match the prior
hash-bound proof. This checks the actual declared target recipes, beyond the
earlier manually linked contracts; it does not claim every CMake target/test
was built or executed.

Original desktop worker coordinator 52950 subsequently exits 0. It builds the
actual `tic80-live` and `tic80-studio-live` targets in that fresh configuration
and checks their real worker entry points through the supervision protocol.
Each frontend handles eight supported declarations, thirteen rejected cases
and twenty-one valid replacements. Executable identity is verified for 42
distinct player workers and one retained Studio worker. The earlier test
executables, WASM archive and source hashes remain unchanged. Original logs,
terminal observations, target recipes, source/generated adapters and product
hashes are sealed in `wasm-import-offline-production-progress-20261005.json`.
ELFs are referenced by hash rather than duplicated. These are desktop workers;
the corresponding ARM production-worker suite remains undispatched, and no
native playback, full cartridge matrix or physical qualification is claimed.

### October 5: real desktop workers match the complete demo/format matrix

`tests/runtime_production_worker_test.c` routes the existing supervision
protocol to the unchanged desktop production executable, verifying each worker
by PID and `/proc` executable path. It loads the independently frozen upstream
cartridges in native, modern-PNG and legacy-PNG formats through the real worker
decoder. Each of the fourteen languages runs 60 game ticks in each format on
both frontends. The player uses released input, matching its reference. Studio
uses the existing center-pointer reference, and its RUN command supplies the
initial game tick. Each Studio case uses a fresh private configuration/worker.
Every 30/60-tick full RGBA frame and every 1,600-sample PCM tick buffer must
match the frozen reference before a case can pass. This checks runtime/decoder
behavior, rather than FPGA transport, scheduling or physical output.

Original coordinator 57561 runs both matrix executables to exit 0. They verify
84 cases, 84 distinct actual worker executables, 168 complete frames and 5,040
PCM tick buffers. The coordinator itself exits 1 because the unmodified Studio
console leaves an unterminated `>` prompt before 39 case markers. The three
WASM markers are on separate lines after interpreter teardown output. A new
read-only review accepts exactly this prefix pattern, checks all 168 saved
frame hashes against their independent references and seals the original logs
and failure in `wasm-import-runtime-matrix-host-progress-20261005.json`. The
executables are not rerun to correct that review, and frames/ELFs are referenced
by hash rather than duplicated in the archive.

Original ARM helper builder 65184 exits 0 with the exact same test source,
corrected runtime archive and production supervision libraries. Both helper
ELFs pass checks against the recorded MiSTer provider exports; production ARM
binaries remain unchanged. Preparation is sealed in
`wasm-import-runtime-matrix-arm-progress-20261005.json`. The helpers have not
executed on MiSTer. Native contracts, actual ARM production-worker tests,
FPGA/source-acknowledgement demo matrix and final physical checks still wait
for hardware availability.

### October 5: fresh registered runtime/API regressions

Original coordinator 39283 builds the 27 distinct executable targets for 28
selected registered CTests in the fresh corrected CMake configuration. Tests
run sequentially with their existing timeouts, avoiding shared working-folder
fixture collisions. CTest reports 28/28 passing, no skipped cases and 214.21
seconds of test execution; the original coordinator exits 0. The parsed JUnit
report independently names every selected test with no failure/error/skip node.

Coverage includes all-runtime input/mouse and banked assets; sound argument,
bound, default and note contracts; FFT/VQT numeric bindings, synthetic capture
and spectrum arguments; Forth stack recovery; VM/PNG recovery; WASM result
slots and pointer bounds; and basic cartridge, clock and memory behavior.
The FFT/VQT device boundaries are deterministic doubles, while DSP, interpreter
callbacks, worker IPC and lifecycle run normally. This does not verify a
physical microphone. These batch workers dispatch through the test executable;
the real production executable checks are recorded separately above.

Original source hashes, earlier test products, runtime archive and desktop
production binaries remain unchanged. The earlier memory CTests are not
repeated. Original logs, JUnit, terminal observation, source/target recipes and
binary hashes are sealed in `wasm-import-runtime-regressions-host-progress-20261005.json`,
without duplicate ELF archives. This is a selected software regression batch,
not a claim that every project test passes or that native/physical gates close.

### October 5: corrected imported memory passes on MiSTer

After the user renewed hardware availability, original coordinator 10157 runs
all six unchanged ARM executables against the board's verified runtime providers.
All commands and the coordinator exit 0. The standard upstream WASM cartridge
accepts its two-page import, retains stable RAM and exactly matches the independent
30/60-tick RGBA frames. Twenty memory cases, twenty-one declaration/replacement
cases, six supervised pointer faults and all fourteen mouse/input language
contracts pass. Canonical payloads/settings, source-bound Tetris, audio and HDMI
are restored. The raw logs and terminal result are sealed in
`build/wasm-import-limits-native-progress-20261005.json`.

Original production-worker coordinator 20211 loses SSH during the last binary
upload, before dispatching any job. It exits 1 and verifies canonical restoration.
The failure is sealed in `build/wasm-import-workers-upload-failure-progress-20261005.json`.
A fresh coordinator 7143 reuses three completed uploads only after remote hash
checks and transfers the remaining Studio executable compressed. Both actual
ARM production worker checks exit 0, as does the coordinator. Each frontend
passes eight supported declarations, thirteen rejected cases and twenty-one
valid replacements. Executable identity is verified for 42 distinct player
workers and one retained Studio worker. Native provider hashes and canonical
restoration are independently reviewed in
`build/wasm-import-workers-native-v2-progress-20261005.json`. The original failed
coordinator and its partial uploads are not restarted or rewritten.

Candidate manifest `18de1e89` freezes these exact player/Studio binaries with
unchanged Main `81253dc2` and FPGA `fd673c65`. Payload hard links avoid duplicate
ELF storage. Fresh two-frontend playback, source-bound FPGA cartridge matrix and
physical checks are separate gates; these contract passes do not close them.

Original coordinator 27812 subsequently passes the current candidate's complete
ten-minute Studio and ten-minute player music runs and exits 0. Both retain valid
save CRCs, one boot, stable sampled memory, approximately 60 Hz game timing and
48 kHz audio, with zero measured underruns. Each pair of detached audio/memory
monitors continues through a deliberate 35-second observer SSH disconnect and
is collected once with status 0. The coordinator verifies canonical Main,
launcher, payloads/settings, source-bound Tetris, audio and HDMI restoration.
Independent review seals the original logs in
`build/runtime-wasm-import-integration-soak-progress-20261005.json`.
The user explicitly confirms stable picture and good stereo audio for the
current player, bound to candidate `18de1e89`. The Studio output question is
still pending. Source-bound FPGA cartridge checks and final physical controls
remain separate from these bounded music runs.

Original physical-input coordinator 18381 then exits 0 on candidate `18de1e89`.
Saved, CRC-checked diagnostic counters record all eight D-pad/face actions and
all eight separate W/A/S/D/Enter/Esc/Q/E actions, each with a positive press count.
Mouse counters record 89 horizontal and 77 vertical changes, all three buttons,
five up-wheel units and thirteen down-wheel units. All held masks return to zero
and the diagnostic records at least 120 fully released ticks. No physical input
is injected by the harness. The user confirms all matching controls worked.
The initially undetected mouse reports repeated kernel USB descriptor error
`-32`; moving it to another port resolves the connection and the user confirms
it responds. Canonical payloads/settings, Tetris, audio and HDMI are verified
restored. Actual counters, raw saves, terminal observation and restoration are
sealed in `build/final-input-wasm-import-progress-20261005.json`. This checks
controller-generated key actions; it does not claim a physical keyboard or
microphone test.

Original native production-matrix coordinator 33813 then exits 0 after all
84 cases (fourteen languages, native/modern-PNG/legacy-PNG and both frontends).
All original commands exit 0. The helper verifies 84 distinct actual production
worker executable paths, both complete 30/60-tick RGBA frames per case and all
5,040 PCM tick buffers against the independent references. The coordinator
downloads and independently hashes all 168 frames; the only accepted Studio
marker prefix is the previously verified upstream console `>` on 39 cases.
The helper uses deterministic released input for player and the frozen center
pointer for Studio. These are real MiSTer ARM executables and runtime providers,
but this suite does not use the FPGA transport or physical capture.

The original raw logs, downloaded-frame hashes, product/provider identities,
terminal observation and canonical restoration are independently sealed in
`build/wasm-import-runtime-matrix-native-v2-progress-20261005.json`. Frames and
ELFs are referenced by hash rather than duplicated into the evidence archive.
The first local preparation's quoting error is preserved; a fresh preparation
passes offline shell syntax, sealed-log review and all 112 fixture checksums
before the native driver is dispatched. It does not rerun any completed native
suite. Main/FPGA source acknowledgement and real cartridge loading are being
checked separately on the same candidate.

Original Main/FPGA coordinator 49327 exits 1 after 30 exact cases (all 14 native
Studio carts, all 14 modern PNG Studio carts, and legacy Lua/JavaScript). Its
MENU transition guard reads an empty core name and rejects it. The failure is
preserved in `build/runtime-wasm-import-matrix-a97b64ae/result.json`, with the
actual terminal observation. Canonical Main, handler, payloads, settings and
source-bound Tetris restoration pass, with restored audio and HDMI observations.
This does not establish that another named core was selected; it does not qualify
the remaining 54 cases. The original driver is not restarted.

At the user's request, the Realtek Bluetooth controller pairs and trusts an
Anker A7726 keyboard. BlueZ reports Paired/Trusted/Connected yes and the HID
service; Linux enumerates `Anker A7726 Keyboard`, bus 0005, vendor/product
291a:8502, event1. Discovery and pairable mode are turned off afterward, retaining
the bond. Evidence is `build/bluetooth-keyboard-connected-20261005.json` and the
original interactive pairing log. This establishes connection and enumeration;
physical key behavior through TIC-80 is qualified by the subsequent bounded test.

Original physical-keyboard coordinator 50655 exits 0 on candidate `18de1e89`.
A fresh save ID separates this diagnostic from all earlier controller counters.
The cartridge observes all 22 keys: arrows, Z/X/A/S, W/D, Q/E, Enter/Esc,
Space/Tab/Backspace, Shift/Ctrl/Alt, and 1/0. Each key has a positive press and
release count. Shift+Z, Ctrl+Q and Alt+E register together; W is held for 196
ticks and its explicit `keyp(23,30,10)` repeat query registers 25 times. Physical
keyboard conversion also reaches all eight default gamepad actions. The final
held masks are zero and 608 consecutive released ticks are recorded. No
synthetic input is dispatched. The user confirms the matching labels lit up.

`build/physical-keyboard-wasm-import-progress-20261005.json` seals the actual
CRC-valid pmem counters, immutable candidate/fixture/driver identities, source
acknowledgement and canonical Main/handler/payload/settings/Tetris restoration.
Restored audio has zero measured underruns and the HDMI clock is 74.25 MHz.
The user response is recorded in
`build/physical-keyboard-human-confirmation-20261005.json`. This bounded player
diagnostic does not qualify every key, broader Studio editor behavior, Bluetooth
reconnection or power-cycle persistence. Its fresh driver waits at most five
seconds for an empty CORENAME transition to settle; an unexpected named core
still aborts. The original failed cartridge matrix is not rerun.


## Final development package and installation — October 5, 2026

Current matched build `18de1e89` completes the bounded qualification and is
installed with Studio as the default. Original coordinator 21969 exits 0 after
41 remaining player Main/FPGA cases. Independent review combines these with
43 valid raw observations from earlier failed coordinators for all 84 cases,
using frontend-correct complete RGB references, source acknowledgement and
zero measured underruns. No pixels are ignored and no completed test is rerun.
The original MENU-transition and Studio-cursor comparator failures remain
preserved in their native folders. The combined receipt is
`build/runtime-wasm-import-matrix-complete-progress-20261005.json`.

A fresh brief Studio music check under original coordinator 89496 exits 0 and
receives explicit user confirmation of stable picture and clear stereo music.
The earlier complete ten-minute Studio and player soaks remain the sustained
checks; this listening window does not rerun them. It is sealed in
`build/studio-import-listening-v2-progress-20261005.json` and its bound human
confirmation. Both frontends now have fresh physical HDMI/stereo confirmation.

`build/wasm-import-final-qualification-20261005.json` binds the frozen candidate,
all seven qualification archives, both ten-minute phases, all 84 FPGA cases and
physical Xbox/mouse/keyboard observations. The final six-file payload, licenses,
usage and qualification are packaged as
`releases/TIC80-MiSTer-18de1e89-20261005.zip` (16,995,093 bytes). Archive members
are independently CRC- and SHA-verified. ZIP SHA-256 is
`3a824eeb9235d80536128176894db7cf0dbb20a5fae6814f136f109b741d940a`;
`build/wasm-import-package-progress-20261005.json` records the package identity.

Original installer 31861 exits 1 because its verification compares Windows
CRLF MGL bytes with normalized Linux text. Original installer 39065 exits 1
because its final check requires an idle Bluetooth keyboard to be connected,
although paired/trusted state remains intact. Both actual runs verify full
payload and Main rollback. The second run's installed Tetris/source/audio/HDMI
checks pass before rollback. Failures, terminal observations, backups and
restoration proofs remain preserved; neither original is restarted. Verification
repairs normalize the expected MGL text and require persistent Bluetooth
pairing/trust without requiring an idle connection. The packaged production
binaries remain unchanged.

After the user wakes the keyboard, read-only observation confirms connection
and HID enumeration in `build/keyboard-wake-observation-20261005.json`. Fresh
original installer 37417 exits 0 (terminal chunk `e87d74`). Final installed Main,
RBF, Studio, player, handler and CA bundle match the package manifest. Tetris
source ticket/acknowledgement agree for the actual 25,147-byte cart. Its bounded
audio observation has 272 samples, zero underruns and advancing playback. Four
HDMI samples measure exactly 74.25 MHz; the Bluetooth keyboard is paired,
trusted and connected. MiSTer.ini, the Frontier Master Daemon, frontend selection
and the older RBF remain byte-identical. Existing carts and saves are retained.

Independent sealer exits 0 (chunk `80ae94`) and writes
`build/wasm-import-installation-v3-progress-20261005.json`; only then does it
advance `build/studio-installed-latest.json` and `studio-progress-latest.json`.
Native observations and original logs are retained in
`build/wasm-import-installation-v3-review-20261005`. Main backup is under
`/media/fat/games/TIC-80/.install-candidates/wasm-import-18de1e89-v3-20261005-main`;
other payload backups are under the sibling `wasm-import-18de1e89-v3-20261005`
stage. The installation receipt's actual-rollback flag refers to the preserved
prior full rollback, not to rolling back the successful final installation.
TIC-80 Studio remains running Tetris at 720p/60.

This finishes the tested development installation and handoff. It does not
close broader cartridge/API or Studio editor/SURF coverage, Bluetooth
power-cycle/peripheral coverage, unavailable CRT/microphone capture, physical
SD power-loss durability, wider transport/workload stress or complete physical
external-I/O timing/latency qualification. These remain explicit in the frozen
qualification and final installation receipt.

## Standard Main compatibility — October 5, 2026

The user challenges requiring a replacement shared MiSTer executable. Audit of
`tools/stage_main_source.py` identifies three prototype extensions: original
cartridge filename packets, initial delayed-MGL reset/popup handling, and
horizontal mouse-wheel events. Runtime execution, video/audio and requested
controller/key mappings do not require these patches. Standard Main is now the
default release target, with per-core integration tracked in `stock-main.md`.

Original local coordinator 17820 exits 0 (terminal chunk `c514fb`) for
`tools/test_hps_transport.py --scenario stock`. Eight standard transfers exercise
both native and PNG indices and four sizes through actual Main SPI helpers,
the pinned HPS parser and current cartridge loader. Complete bytes, delayed
acknowledgement ownership and empty source metadata pass. The exercised
transfer/status functions are independently compared with unmodified pinned
Main. Source packet, MGL hold and horizontal-wheel extensions are not invoked.
The receipt `build/stock-main-transport-progress-20261005.json` retains original
terminal status and hashes of the raw log, generated helpers and test sources.
The test is registered as `rtl_hps_stock_main` for Linux with Verilator.

This changes neither the installed Main nor the frozen package and does not
prove full Main hardware execution. Startup/MGL/reset/reload behavior, original
Studio source/save behavior and per-core horizontal-wheel input still require
standard-Main implementation/qualification before a new default package can
omit Main. The installed prototype's qualification cannot be reused to claim
that different installation is verified.

## Frontier installer (October 5, 2026)

`Scripts/Install_TIC80.sh` installs only the five per-core payloads and reuses
Frontier's handler discovery, matching PICO-8's launch arrangement. It includes
no shared Main write or download. An absent Frontier daemon is supplied from
the pinned upstream source; an existing daemon is preserved.

The original ten real-Bash fixture tests pass (terminal chunk `993b79`, exit 0):
fresh/repeat installation, existing Frontier/startup preservation, inactive
startup promotion before early exit, read-only check, corrupt bundle rejection,
forbidden Main destination rejection, symlink rejection, actual partial atomic
promotion failure/restoration, core-only rollback and refusal of rollback after
later changes. Three package tests pass (`d07b63`, exit 0), covering omission of
Main from a Main-containing prototype, pending qualification flags, immutable
outputs, corrupt inputs and path traversal rejection. The archive builder
verifies CRCs and SHA-256 for all packaged files.

Original native coordinator 22784 exits 0 (`37f5e0`). The script and checksum
bundle are staged under `/media/fat/Scripts`, and `--check` passes on the board.
Hashes of Main, MiSTer.ini, existing Frontier and all five installed core
payloads match before/after. The selected core does not change. Installation
itself is not run during this check; no runtime compatibility claim follows.
Evidence is `build/frontier-installer-native-check-20261005/result.json` and
`check.log`. The separate development installer archive is
`releases/TIC80-Frontier-installer-dev-20261005.zip`, SHA-256
`205faec50b1f21fcb826701842dc70d820912ad769eb61d09118a451667e3f12`.
