# Install TIC-80 with Frontier

TIC-80 uses the same launch arrangement as
[PICO-8](https://github.com/MiSTerOrganize/MiSTer_PICO-8#manual-install): a core
in `_Other`, ARM programs and `_handler.sh` in `games/TIC-80`, and the shared
[Frontier Master Daemon](https://github.com/MiSTerOrganize/MiSTer_Frontier).
`Scripts/Install_TIC80.sh` handles setup. It never downloads, installs or
replaces the shared `/media/fat/MiSTer` executable.

## Installation

1. Extract the **installer ZIP** to the root of the SD card (`/media/fat`).
   It places the script and its payload under `Scripts`; extracting alone does
   not replace the installed core or runtime.
2. Load the MiSTer MENU core if TIC-80 is currently running.
3. Run **Install_TIC80** from MiSTer's Scripts menu.
4. Select **TIC80** from the cores menu. Studio is the default frontend.
5. Put `.tic` and `.png` cartridges in `games/TIC-80/Carts`.

The installer verifies SHA-256 checksums for all five TIC-80 payloads before
making changes. It preserves an existing Frontier daemon and registration.
When Frontier is absent it installs a pinned, unmodified upstream daemon,
registers it in `linux/user-startup.sh`, and starts it if the MENU core is loaded.
Otherwise reboot to activate it. It never restarts an existing Frontier daemon.
There is no second TIC-80 daemon alongside Frontier.

Existing cartridges, saves, `frontend.txt`, `MiSTer.ini`, controller mappings
and other cores are preserved. Re-running the script does not duplicate the
startup registration. Symlink destinations are rejected; an installation using
symlinked game folders needs its layout reviewed before using this script.

## Check and rollback

From SSH, `bash /media/fat/Scripts/Install_TIC80.sh --check` verifies the bundle
without writing files. Each installation saves replaced files and a change
journal in `Scripts/TIC80-backups/<timestamp>.<pid>`. A failed promotion restores
the files already changed by that attempt.

To restore TIC-80's previous core/runtime files, load MENU and run:

```sh
bash /media/fat/Scripts/Install_TIC80.sh --rollback /media/fat/Scripts/TIC80-backups/<backup>
```

Rollback first checks that every affected core file still matches the installed
version and that backup checksums are valid. Later edits abort rollback before
any file is restored. Shared Frontier setup stays installed because other cores
may use it. Carts, saves and settings are retained. These checks do not establish
durability through physical SD power loss.

## Development status and packaging

The installer is independently tested with fresh and existing Frontier setups,
repeat installs, read-only checks, corrupt bundles, forbidden Main payloads,
partial update failure and rollback. The development installer bundle uses the
current TIC-80 runtime/core candidate. Its complete behavior with unmodified
Main is **still under qualification**; this package is not a stock-Main release
approval. See [standard-Main compatibility](stock-main.md).

The earlier frozen prototype ZIP contains a companion Main and has a different
installation contract. Do not extract that ZIP as an installer bundle.

Build a fresh installer ZIP from a checksum-verified candidate package with:

```sh
python tools/package_installer.py --package build/TIC80-MiSTer-18de1e89-20261005 --output releases/TIC80-Frontier-installer-dev
```

The builder accepts only the five per-core payload paths, vendors Frontier and
licenses, records its input manifest and pending stock-Main qualification, and
checks all archive CRCs and file hashes. It refuses to overwrite an existing
package. No unpublished GitHub download URL or update_all registration is
assumed; the ZIP installs offline.
