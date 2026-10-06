# Building from source

The integration is GPL-3.0-or-later; upstream components retain their own
licenses. See [credits](../CREDITS.md) and [NOTICE](../NOTICE).

## Pinned dependencies

| Source | Revision |
| --- | --- |
| [TIC-80](https://github.com/nesbox/TIC-80) | `4dba5bc2640d9cde650fb0b427c9be6aab598de9` |
| [MiSTer_PICO-8 platform](https://github.com/MiSTerOrganize/MiSTer_PICO-8) | `72cb0405417d506c33e59ab51c1a374fc4db649e` |
| [Frontier](https://github.com/MiSTerOrganize/MiSTer_Frontier) | `a7c61e0a000d9dfd40235e638229e06a603534d5` (vendored daemon) |
| [Main](https://github.com/MiSTer-devel/Main_MiSTer) (optional development tests) | `5a3a08662c25bd792043f8a8fb48e4be12099beb` |

On Linux, install CMake, a C/C++ compiler, Git, Python 3, Ruby/rake, and Verilator
for RTL tests. Runtime tests need QEMU when using an ARM cross build. Bootstrap
the pinned runtime and its submodules:

```sh
python3 tools/bootstrap.py
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host -j4
ctest --test-dir build/host --output-on-failure
```

The release's corresponding-source ZIP also contains pinned upstream snapshots
and license notices, including initialized TIC-80 dependencies, without Git
metadata or compiler output. For an offline runtime build, extract it and set
`-DTM_TIC80_SOURCE=upstream/tic80` instead of running bootstrap. `source-manifest.json`
records every supplied file and upstream revision. Do not confuse an archive
snapshot with a Git checkout when running Git-based staging tools.

## MiSTer ARM programs

The candidate uses GNU Arm `10.2-2020.11-x86_64-arm-none-linux-gnueabihf`
(GCC 10.2.1, glibc 2.31). Obtain the toolchain separately, set `TM_MISTER_SDK` to
its directory, and build:

```sh
cmake -S . -B build/mister-sdk \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mister-arm-linux.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/mister-sdk -j4
```

The integration enables the fourteen scripting languages and stages the
adapters in `cmake/`, retaining upstream notices. Studio and player are separate
executables. Audit target-library compatibility with `tools/check_runtime_abi.py`
before deploying a new binary. The system loader, libc, ALSA and curl are not
included in the installer ZIP. See the detailed development commands in the
[development record](../DEVELOPMENT.md).

## FPGA

Install Quartus Prime Lite 17.0 with Cyclone V support separately. The normal
Git-based path is:

```sh
python3 tools/prepare_fpga.py
cd build/fpga
quartus_sh --flow compile TIC80.qpf
python3 ../../tools/check_timing.py output_files/TIC80.sta.summary
```

The corresponding-source ZIP includes the staged source project under
`fpga-project/`, with platform sources, Intel IP source, integration overrides,
QPF/QSF/SDC and original notices. For an offline build, compile its `TIC80.qpf`
directly. Compiler reports and generated bitstreams are not source inputs.
Run the additional timing/CDC audits documented in DEVELOPMENT.md before hardware
qualification. Internal timing checks do not establish complete external-I/O
or CRT compatibility.

## Installer and tests

```sh
python3 tests/install_script_test.py
python3 tests/installer_package_test.py
```

`tools/package_installer.py` builds a fresh offline installer ZIP from an
existing checksum-verified candidate package; it never includes Main. Packaging
is not runtime qualification. For reproducible releases retain the source
revision, candidate payload hashes, toolchain profile and qualification status.
