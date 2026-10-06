# Native SDK preflight

The SDK candidate in `build/sdk-native-candidate` is locally tested and pending
native qualification. Its exact binaries and companion Main/RBF candidates
are listed in its `manifest.json`. The current installer still selects the
older static ARM profile.

Hardware is unavailable until the human confirms availability and the local
device hold is released. These instructions do not release that hold. The
snapshot tool checks it before opening SSH and before every SFTP operation.

## Capture the board's providers

Once hardware is available, use the existing SSH password environment variable
and a fresh local output directory:

```powershell
python tools/snapshot_native_libraries.py --output build/native-provider-snapshot
```

The tool uses known SSH host keys and reads files through SFTP. It does not
upload files, execute remote commands or candidates, switch cores, install
libraries, or alter the board. It copies the loader, libc, pthread, dl, math,
rt and ALSA provider aliases from `/lib` or `/usr/lib`. The receipt retains
symlink targets, file hashes and kernel/core/audio metadata. Each provider is
read twice and checked for content, size, timestamp or symlink changes. SFTP
operations have a 15-second channel timeout and are not automatically retried. Missing files
and failures retain an incomplete receipt; an existing output is never
overwritten. A renewed device hold stops a transfer at its next operation.

`snapshot.json` being complete means all requested files were captured. It
does not establish an ABI match, the complete dependency closure, usable
audio hardware, or a native test pass. Missing `/proc/asound` metadata is
recorded separately from missing libraries.

## Audit copied libraries offline

Run the ABI checker on Linux/WSL with `readelf`, using only the captured
providers. For example, from the repository directory:

```sh
python3 tools/check_runtime_abi.py \
  --binary build/sdk-native-candidate/bin/tic80-live \
  --library-root build/native-provider-snapshot/providers \
  --dlopen-library libasound.so.2 \
  --miniaudio build/sdk-native-candidate/offline-tools/miniaudio.h \
  --output build/native-service-provider-abi.json
```

Repeat for `bin/tic80-player`, `bin/tic80-studio-live`, `bin/tic80-fft-probe`,
`fixtures/studio_session_test` and `fixtures/studio_net_test`. Preserve failures.
If a provider has an additional dependency, capture a new snapshot using
repeated `--library` arguments for the full requested set, including that
dependency. Explicit arguments replace the seven default names.

Never fill a missing board provider with the SDK's local fixture libraries:
that would qualify a different filesystem. The companion Main's provider
requirements must also pass its separate ABI comparison before deployment.
This snapshot and audit do not replace the native worker/spawn/filesystem
tests or the displayed/audio/reset qualification described in the bundle.

## Local regression

`native_library_snapshot_test.py` uses a fake SFTP filesystem. It checks
relative/absolute symlink provenance, missing providers, cycles, non-ELF
files, preserved output directories, a same-size/same-timestamp replacement,
transfer timeouts, and device holds before connection setup and during transfer. The client
exposes only read operations, so a remote write cannot pass unnoticed.

```sh
python3 tests/native_library_snapshot_test.py
```
