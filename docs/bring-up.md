# Bring-up plan

## Architecture decision

Run TIC-80's upstream runtime on the ARM CPU. Adapt the PICO-8 port's native
output transport and MiSTer shell. This is a software/FPGA hybrid, with the
same division of work as the reference project, rather than a hardware
implementation of the scripting engines.

First bring-up milestone: Lua `.tic` cartridges, four gamepads, video, stereo audio,
reset, cartridge loading, and persistent cartridge memory. Add other script
runtimes after the basic path is working. Editor, SURF network browser, replay,
and arbitrary VM save states are later features; ordinary `pmem` persistence
and VM save states are separate problems.

## Video decisions

Upstream `include/tic80.h` defines 240x136 gameplay, 256x144 complete output,
60 frames per second, and four gamepads with eight buttons each.
The runtime produces final composited pixels, so preserve its complete frame
instead of sending a single 16-color palette: border/scanline callbacks and
video-bank behavior can change colors within a frame.

The TIC3 revision transports `TIC80_PIXEL_COLOR_RGBA8888` directly, with the
FPGA ignoring alpha and preserving all eight bits of each RGB channel.
Earlier TIC2 builds used RGB565 and lost palette precision. Each frame is
147,456 bytes (0x24000); two buffers need 294,912 bytes.
Raw frame writes at 60 Hz are approximately 8.85 MB/s, excluding FPGA reads,
audio, cache effects, and protocol traffic. Bandwidth and CPU headroom still
need measurement on hardware.

Do not reuse the PICO-8 frame layout unchanged: its buffers are only 32,768
bytes each, and expanding them in place collides with later regions.
Audit every byte offset on both sides, including audio/cart staging, then
define one versioned memory map and generate matching C/RTL constants.
The reference's physical DDR base is 0x3A000000; verify reservation with the
actual MiSTer environment before mapping it. Do not map it on a desktop PC.

Start with an HDMI-friendly timing that shows all 256x144 pixels at 60 Hz.
CRT modes need a separate geometry/timing decision; the PICO-8 reference's
NES timing and 128-to-224 line mapping cannot simply be retained for TIC-80.
Preserve the approximately 16:9 square-pixel image geometry by default.

## Implementation sequence

1. **Host runtime harness.** Pin TIC-80 plus recursive dependencies, build
   upstream `tic80core` with Lua and its static runtime dependencies. Load a
   small `.tic` using `tic80_create(48000, TIC80_PIXEL_COLOR_RGBA8888)` and
   `tic80_load`. Call `tic80_tick` once per 60 Hz step, with monotonic counter
   and frequency callbacks; call `tic80_sound` for generated PCM. Forward error,
   trace, and exit callbacks. Export a frame and audio file as a baseline.
   Validate the unit of `samples.count` from source before copying PCM.
2. **ARM build.** Cross-compile for MiSTer's ARMv7 Linux ABI and installed
   libc, without a desktop SDL dependency. Run the same cartridge and measure
   tick + conversion time against the 16.67 ms budget. Handle unsupported
   cartridge script languages with a readable error.
3. **FPGA test pattern.** Derive the reference core under GPL-3.0, retain
   attribution and framework licenses, and leave its `sys/` framework intact.
   Change identity/menu strings and replace video geometry, line buffers,
   counters, address arithmetic, and frame layout together. Update Quartus
   project/source names. Prove a 256x144 color/edge pattern before connecting
   the runtime. Check synthesis resource use and timing closure.
4. **Frame exchange.** Implement inactive-buffer writes, ARM memory barriers,
   frame sequence publication, and FPGA vertical-blank adoption. A buffer may
   only be reused when the FPGA has finished reading it. Audit Linux mapping
   attributes/cache visibility; C `volatile` alone is insufficient. Define
   startup, counter wrap, producer restart, and missed-frame behavior.
5. **Audio and clocking.** Adapt the stereo S16 ring with explicit producer/
   consumer units, capacity, wrap handling, barriers, and underrun silence.
   At 48 kHz/60 Hz the nominal output is 800 stereo sample frames per tick.
   Pace game updates from measured FPGA feedback; measure audio/video drift
   rather than assuming nominal clock rates agree.
6. **MiSTer integration.** Map all eight buttons for each gamepad explicitly;
   do not reuse PICO-8's two-action-button mapping. Bring up OSD `.tic` loading
   with bounded transfer staging and acknowledgment. Reset runtime state on
   cartridge switches. Wire per-cart `pmem` storage using upstream internal
   APIs or a small maintained adapter (the public player API alone does not
   expose a persistence hook). Add the launch handler and SD-card layout.
7. **Hardware release gate.** Exercise loading, switching carts, reset, four
   controllers, music, persistence after restart, runtime errors, and repeated
   launch/exit. Measure dropped frames, tearing, audio underruns, and latency.
   Compare border/scanline effects with the pinned desktop runtime. Test HDMI
   and each supported analog timing separately before distributing an `.rbf`.

## Source landmarks

PICO-8 reference:

- `src/native_video_writer.{c,h}`: DDR mapping, frame publication, input/audio
  transport and fixed offsets.
- `src/mister_main.cpp`: emulator loop, cartridge lifecycle, persistence and
  input integration; replace the emulator-specific behavior.
- `fpga/rtl/pico8_video_reader.sv`: DDR transactions and framebuffer geometry.
- `fpga/rtl/pico8_video_timing.sv`, `pico8_video_top.sv`: output timing and path.

TIC-80 reference:

- `include/tic80.h`: player interface and frame/input formats.
- `src/tic.c`: load, tick, blit and sound lifecycle.
- `cmake/core.cmake`: core library and runtime linkage.
- `src/system/libretro/tic80_libretro.c`: useful reference for input,
  audio/video, and persistent-memory integration.

## Validation so far

The Linux and statically linked ARM players build from the pinned upstream
runtime. The ARM executable and tests run on a MiSTer over SSH; Tetris, SFX,
and music demo frame/PCM hashes match desktop output. A real Lua cartridge
test covers four-player input, pmem writes, scanline palette changes, border
callbacks, both video banks, and sound. C/RTL co-simulation verifies frame
ownership/session control, DDR burst transfers under stalls, scanout pixels,
and the stereo audio FIFO. The staged Quartus project now builds a native
video development RBF, and the live ARM frontend publishes frames on hardware.
See `validation.md` for measurements and precise remaining gates.

Quartus Prime Lite 17.0 was found at
`C:/Users/kando/intelFPGA_lite/17.0/quartus/bin64`, with Cyclone V support.
The video build succeeds and all constrained timing paths have positive slack.
The MiSTer screenshot path captured both the edge/color pattern and live Tetris;
600 pattern frames ran in 10 seconds and 3,600 cartridge frames in 60 seconds.
The stereo build consumed 2,880,000 sample frames in a 60.012-second music run,
with playback acknowledgments from the FPGA; the later corrected music soak
also received HDMI picture and stereo listener confirmation. Native F0 cartridge loading, hot-swap, invalid-load
recovery, background saves and launch through the existing Frontier daemon now
run on hardware. The loader accepted a 4 MiB padded native cartridge and
rejected a 4 MiB + 1 byte download before successfully loading Tetris.
Keyboard/mouse capture and a coherent DDR snapshot now pass local simulations
and a board uinput probe through MiSTer's normal input path. Keyboard controls
start the upstream Tetris cartridge. Physical keyboard/mouse/controller checks,
horizontal wheel support, storage durability, broader cartridge/API coverage,
and broader physical display qualification remain gates. See `validation.md`
for precise evidence.

All 14 pinned scripting runtimes now build statically for desktop and ARM.
Native demos, two concurrent cartridge instances, and failed-load recovery for
the adapters with shared global state pass on both hosts. MRuby's archives and
pForth dictionaries are generated per build tree; ARM dictionaries run a target
bootstrap under QEMU. Frame/PCM parity covers 600 ticks per language, but does
not establish compatibility with every cartridge or language feature.

The embedded-player clock now keeps per-VM callback data instead of recreating
its epoch each frame. Nonzero-clock, pause/resume and reload tests pass on
desktop and ARM. A native five-second clock cartridge also exits and saves
correctly on the MiSTer; long-session drift remains open.

Live cartridge execution now uses supervised Linux processes for all languages.
Failed candidates preserve the old VM; hung/crashed games return to the service
with the last completed persistent-memory snapshot. Host and ARM fault tests,
native MiSTer recovery, and native loading/capture parity pass. MiniScript
SCN/BDR/MENU callbacks also preserve the yielded main harness. This closes the
basic runaway-execution path; broader long-session and storage stress remain.

The cartridge adapter also decodes both upstream PNG containers: compressed
`caRt` chunks and legacy pixel-bit payloads. Desktop and real ARM tests verify
native/PNG bytes, save identity, video and PCM parity for all 14 templates.
Input and inflated cartridges are bounded to 4 MiB, images to four million
pixels, with PNG structure/CRC and stream validation. Live image/decompression
work stays inside the supervised process. MiSTer tags the second F0 extension
with index `0x40`; the loader accepts it along with native TIC index `0x00`.

Optional coherent hardware statistics now expose real audio output slots and
underruns. The read-only ARM monitor detects session changes, unsupported
statistics, stale heartbeat and persistently torn samples. A 600-second native
music/clock/save baseline revealed 561 startup underrun slots and 53 later slots;
saves and frame cadence remained healthy. Playback now waits for two published
TIC ticks, with a 50 ms fallback for shorter streams. Delayed-producer and
finite-clip simulations pass, and actual 800/1600/2400-frame CLI clips drain on
the board. The corrected 600-second hardware soak passed with zero startup
or playback underruns, 36,000 adopted game frames and valid saves throughout.
The user confirmed the connected HDMI picture and clear continuous stereo
audio. See `validation.md` for precise measurements and remaining gates.
The Windows build additionally checks physical routing of five audio Gray
buses and six control crossings at four timing corners; full CDC and external
I/O qualification remain open.

TIC3 full-color scanout is now installed. All 14 native and modern PNG demos
match desktop RGB output exactly in live MiSTer captures. An additional raster
preserves every one of the 256 values in each channel, including values lost
by the previous RGB565 transport. Both protocol mismatch directions reject
the incompatible player without taking over its running service. Corrupt and
oversize PNGs recover to the cached game, native and PNG forms retain the same
persistent save, and a PNG with a full 4 MiB extracted cartridge runs and saves.
The revised RBF passes 152 timing checks and 44 routed crossing checks. A fresh
600-second TIC3 music/clock/save soak also passes with zero startup or playback
underruns, approximately 60 game frames per second and valid saves throughout.
Human-operated checks now cover the connected Xbox controller's eight inputs
and the optical mouse's two movement axes, three buttons, both wheel directions
and release behavior. The mouse initially failed USB enumeration and began
responding after connection to another port. Physical keyboard, four-controller
and broader device qualification remain open.

Selected-core ownership now prevents transport writes after a named core
departure, and MENU exits flush saves and reap VM workers without waiting for
an acknowledgement from the replaced FPGA. Runtime cleanup now frees owned
RAM, closes failed VM initialization and releases omitted WASM names. Full
interpreter instrumentation also corrected Python easing alias insertion and
four unused Scheme allocations. All 26 host checks and the all-language
ASan/LeakSanitizer run pass. Four fresh hardware supervisors each passed 16
warm FPGA reloads with flat RSS, exact BOOT/save counters and clean MENU exits.
Native and PNG live RGB parity still covers all 14 languages. The final
600-second music soak has zero underruns and flat supervisor/worker memory.
Measured audio-queue growth exposes a separate clock-alignment and long-run
latency issue, which remains open alongside the broader completion gates.

The current revision paces game ticks from FPGA audio consumption and waits for
reset release before restarting a cartridge. It also reports failed directory
flushes during persistent-memory saves and retries after temporary I/O failures.
All 32 desktop checks pass; sanitizer
and native ARM tests cover slow/failed saves and process death around rename.
The player and Frontier reload compatibility patch were installed. The first
live qualification exposed an intermittent extra BOOT on a same-core reload.
A fresh FPGA session can precede Main's initialization reset; a new readiness
check holds the cached cartridge until reset release or Main's completed
CORENAME rewrite. Local regressions cover delayed reset, a missed pulse, release
without a rewrite, departure while waiting and reset held in the old FPGA.
The first readiness revision passed 64 hardware reloads; a language run then
reported six underrun slots in Squirrel. The current candidate retains a
two-tick audio reserve, adding 16.7 ms over the earlier reserve and surviving
18 ms scheduler-delay simulations without accumulating clock drift. It passes
all 33 host checks across the full run and corrected fixture reruns, plus
focused sanitizer and native ARM checks. The installed revision passes 64
hardware reloads, exact RGB parity and zero recorded underruns for all 28 native
and PNG demos, finite-clip drains and a fresh 600-second music/clock/save soak.
The audio queue changes by -0.062 ms over the soak, both processes retain flat
RSS, and the user confirms HDMI picture and continuous stereo audio. Tetris is
left running for the Xbox controller. Broader display cadence, analog modes,
devices, cartridge/API coverage and release qualification remain open.

The subsequent shared-clock FPGA candidate passed internal timing and native
capture checks, but the connected TV reported "mode not supported". Live tests
were stopped and the previous verified RBF (`645633d2...`) restored with Tetris.
The candidate's new Z/X/A/S remapper labels are not present in the restored RBF.
The configured default output was 1366×768 at 60 Hz. A checksum-valid read of
the Samsung TV's cached EDID advertises 720p/60; a TIC-80-only `video_mode=0`
override now selects that mode, preserving the original INI in a backup. Main
reports 1280×720 and the user confirms the picture is back on the restored RBF.
The synchronized-clock candidate also fails the TV check at 720p/60. Its
automatic CTS readback corresponds to a 148.5 MHz HDMI clock, twice the requested
74.25 MHz; the verified RBF measures correctly. The candidate was rolled back.
Source now uses the complete bundled PLL reconfiguration IP through an interface
adapter. Its seed-9 and seed-10 builds fail local timing and were not installed.
Seed 11 passes 140 timing checks (minimum slack 0.087 ns), routed crossing checks
and the shared-clock audit under the unchanged constraints. After MUGEN testing
finished, the user explicitly directed a TIC-80 switch. The installed seed-11
build measures 74.249–74.250 MHz on HDMI and passes a ten-second zero-underrun
smoke check. The user confirms a correct, stable 720p/60 picture.
Screenshot stress then exposed audio gaps despite intact images. The player
now defers pending pictures in private RAM and runs playback/interpreter work
on CPU 0, separate from Main on CPU 1. This installed player passes one-minute
motion/audio stress, 32 reloads with flat memory, exact RGB parity for all 28
native/PNG demos, finite audio drains and a fresh 600-second synchronized-video
soak. The soak has zero underruns, flat process memory, valid saves and only
-0.020 ms fitted audio-queue change. The user confirms stable HDMI picture and
continuous stereo music on this player. Broader analog, keyboard, device,
latency and cartridge/API qualification remain open. See `validation.md` for
the evidence.

Subsequent API checks repaired MiniScript's specific-key `keyp()` binding and
Wren's inherited empty `OVR()` callback, which erased the second video bank.
The asset-bank contract now passes 42 cases across all 14 runtimes on both
the development host and the MiSTer's native ARM CPU. Real Wren callbacks,
including inherited overrides, retain their legacy behavior. Player
`09951069...` passes focused native/PNG Wren image and audio checks and
a fresh ten-minute soak with no underruns, flat memory, valid saves and 0.173 ms
fitted audio-queue change. Earlier broad reload/demo results remain bound to
their recorded binaries.

The subsequent installed player `a49b30b5...` fixes WASM imports that wrote one
byte into 32-bit result slots, leaving stale high bits from previous calls.
Ninety ABI checks and all 42 bank cases pass on native ARM. Native and PNG WASM
loads match reference RGB with no recorded underruns, and a real diagnostic
cartridge produces exact API values in CRC-valid live and final saves through
the normal launcher and DDR player. Tetris is restored at 720p with a clean
ten-second audio check and the correct 74.25 MHz HDMI clock. The previous
ten-minute soak remains bound to `09951069...`.

## Observing bounded hardware jobs

Dispatch core loads, uploads and monitor launches once. If an SSH reply is lost,
preserve the original job identity and inspect its status and journal; an
observation timeout does not prove the job stopped. Do not repeat activation or
start a replacement monitor to obtain a passing trace.

`hardware_ssh.read_with_reconnect` retries explicitly read-only callbacks and
failed reconnections within a 90-second recovery budget. Each callback must also
bound its own I/O timeout: an in-flight callback can finish after that budget.
Remote command failures, missing files and denied access propagate. The ordinary
`command` function still dispatches each command once. `MonitorJournal` accepts
a separate read callback, retains its byte offset, reads terminal status before
the final tail, and defers incomplete JSON rows until their newline arrives.

Use `hardware_process.arguments` when inspecting enumerated frontend PIDs.
An empty argv read means the process departed and must not count as a parent.
Keep memory checkpoint timestamps as well as monitor timestamps; collecting a
continuous remote audio trace after an outage cannot fill missing process-memory
observations. Seal the original failure and any subsequent recovery separately,
and verify restored payloads, settings, cartridge, HDMI clock and audio.
