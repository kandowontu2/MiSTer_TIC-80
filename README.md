# TIC-80 for MiSTer

A community **TIC-80 Studio and cartridge-player core** for MiSTer FPGA.
The DE10-Nano ARM CPU runs TIC-80; the FPGA handles video, stereo audio,
controller transport and MiSTer OSD integration. The architecture and launcher
arrangement follow [MiSTer PICO-8](https://github.com/MiSTerOrganize/MiSTer_PICO-8).

**Development preview:** the Frontier installer preserves your shared MiSTer
executable. The October 6 preview passes bounded native stock-Main loading,
playback, Save and recovery checks; current-binary human output and physical
controls remain pending. See [tested scope and remaining work](docs/stock-main.md).

## Install

1. Download the **TIC80-Frontier installer ZIP** from the
   [releases page](https://github.com/kandowontu2/MiSTer_TIC-80/releases).
2. Extract it to the root of your MiSTer SD card (`/media/fat`).
3. Load MENU, then run **Install_TIC80** from the Scripts menu.
4. Select **TIC80** from the cores menu. Studio starts automatically.
5. Put `.tic` and TIC-80 cartridge `.png` files in `games/TIC-80/Carts/`.

Our installer verifies the bundle, installs the core and ARM programs, and
sets up the same shared Frontier launcher used by PICO-8. It reuses an existing
Frontier installation, preserves cartridges/saves/settings, creates update
backups, and supports rollback. No replacement MiSTer executable is included.
The offline ZIP needs no GitHub token or downloader configuration.

[Full installation and rollback instructions](docs/install.md) ·
[Usage and troubleshooting](docs/usage.md) · [Build from source](docs/building.md)

## Screenshots

The editor views are actual MiSTer video-pipeline captures, displayed larger for
readability. The clean startup console is an actual desktop Studio render with
modeled DDR transport. The Tetris shot comes from an earlier hardware prototype.
These images are not proof of stock-Main qualification or photographs of HDMI.

**Studio startup console — desktop render**

<img src="docs/images/studio-console.png" width="768" alt="TIC-80 Studio startup console rendered on desktop with modeled MiSTer transport">

**Code editor**

<img src="docs/images/studio-code.png" width="768" alt="TIC-80 Studio code editor on MiSTer">

**Sprite editor**

<img src="docs/images/studio-sprites.png" width="768" alt="TIC-80 Studio sprite editor on MiSTer">

**Music editor**

<img src="docs/images/studio-music.png" width="768" alt="TIC-80 Studio music editor on MiSTer">

**Tetris gameplay — earlier hardware prototype**

<img src="docs/images/tetris.png" width="768" alt="Tetris running on an earlier TIC-80 MiSTer prototype">

Capture origins and hashes are in [the screenshot manifest](docs/images/capture-manifest.json).

## Controls and frontends

| Xbox control | Default TIC-80 action |
| --- | --- |
| D-pad | Directions |
| Face buttons | Z/X/A/S |
| Left analogue stick | W/A/S/D |
| Start / Back | Enter / Esc |
| LB / RB | Q / E |

The remapper names Z/X/A/S, W/A/S/D, Enter, Esc and Q/E individually. Q/E can
also be mapped to LT/RT. Keyboard and mouse input are supported. For all control
details, see [usage](docs/usage.md#controls).

Studio is the default. To choose the cartridge-only player, put `player` in
`games/TIC-80/frontend.txt`, then leave and re-enter the core. Put `studio` there,
or remove that optional file, to return to Studio. Saves are in `saves/TIC-80`;
logs are in `logs/TIC-80`. Existing frontend selection is preserved on install.

## Tested scope

The October 6 stock-Main preview passes 84 integrated language/format/frontend
loads with exact cartridge bytes and full scaler RGB comparisons, separate
ten-minute player/Studio music soaks, synthetic mouse/OSD checks, startup/reload
and private Studio Save workflows, and native bad-cartridge/worker recovery.
Its current binaries still need human picture/stereo and physical-control
confirmation. The installer passes local integrity checks and a native read-only
check of all 61 staged script/bundle files. The updated bundle is now installed
on the test MiSTer through that script, with normal Frontier startup, a passing
bounded music check and verified backups. The user confirmed clean installation
of the earlier preview. Installer bundles include a checksum-bound
`docs/TIC-80/qualification.json` identifying the exact payloads, bounded
evidence and outstanding checks. Packaging these records does not transfer
the older prototype's physical qualification to the new binaries.

Broader Studio/cart/API and startup/fault coverage, Bluetooth power-cycle and
multi-controller coverage, CRT, physical microphone/FFT, SD power-loss durability,
and complete external timing/latency qualification remain open. See
[release status](docs/release-status.md), [validation](docs/validation.md), and
the [detailed development record](DEVELOPMENT.md). This preview is not a
finished universal-compatibility release.

## Credits and license

TIC-80 is by **Vadim Grigoruk (nesbox) and contributors**. This port builds on
**MiSTerOrganize's PICO-8 and Frontier work**, **Sorgelig and MiSTer contributors**,
and the many interpreter/library authors listed in [full credits](CREDITS.md).
Project direction and hardware testing are by **kando**; integration development
used **OpenAI Codex** assistance.

Original integration contributions are **GPL-3.0-or-later**. Upstream code keeps
its original license and copyright notices. [NOTICE](NOTICE), [LICENSE](LICENSE)
and [third-party notices](licenses/) accompany the release. A corresponding-source
archive supplies the integration, pinned runtime/dependencies and FPGA source
project. No proprietary PICO-8 binary or commercial cartridge bundle is included.
