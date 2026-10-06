# Using TIC-80 on MiSTer

Install with [Install_TIC80](install.md), then select **TIC80** from the cores
menu. Frontier starts the ARM frontend automatically. The default is TIC-80
Studio, with its console and editors. This development preview is still being
qualified with unmodified MiSTer Main; see [release status](release-status.md).

## Cartridges and storage

Place `.tic` cartridges and TIC-80 cartridge PNGs in
`/media/fat/games/TIC-80/Carts/`. Load a cartridge through the MiSTer OSD's
**Load Cart** entry, or Studio's own browser/console. Ordinary image PNGs are
not TIC-80 cartridges. The current player and Studio pass native, modern-PNG
and legacy-PNG demo loads for all fourteen included languages through stock
MiSTer Main.

Use Studio's console `help` for its commands; `load <filename>` loads a project
and `run` starts it. Studio prompts when an OSD selection would discard unsaved
work. Save your edits before switching cores or powering off. A stock-Main OSD
transfer does not identify the original filename; use Studio's own browser for
an established save path, or Save As / the working-copy fallback. Do not assume
an OSD-loaded cart will overwrite its original file automatically.

| Directory / file | Purpose |
| --- | --- |
| `games/TIC-80/Carts/` | Your cartridges and Studio projects |
| `saves/TIC-80/` | Cartridge persistent memory |
| `logs/TIC-80/tic80.log` | Current frontend log; previous log is `tic80.prev.log` |
| `games/TIC-80/frontend.txt` | Optional frontend selector |
| `Scripts/TIC80-backups/` | Installer backups and journals |

## Controls

| Xbox control | Default action |
| --- | --- |
| D-pad | TIC-80 direction buttons |
| A/B/X/Y | Face actions Z/X/A/S, according to the core mapping |
| Left analogue stick | W/A/S/D |
| Start | Enter |
| Back | Esc |
| LB / RB | Q / E |
| Guide / assigned OSD button | MiSTer OSD |

Use MiSTer's core remapper to change bindings, including Q/E on LT/RT.
The remapper names the individual Z/X/A/S, W/A/S/D, Enter, Esc and Q/E actions.
Keyboard and mouse input are supported by the frontend; pair a Bluetooth
keyboard through your MiSTer Bluetooth setup before using it. Physical Xbox,
keyboard and mouse diagnostics passed on earlier prototype builds; physical
acceptance of the current release remains open. Horizontal-wheel input is
included without replacing Main. Physical horizontal-wheel hardware has not
yet been verified.

## Choose Studio or player

Studio is the default. To use the cartridge-only player, create
`games/TIC-80/frontend.txt` containing only `player` and a newline. To return to
Studio, put `studio` there or remove that optional file. Leave the core and
re-enter after changing it. The installer preserves your existing selection.

## Display and troubleshooting

The development board was tested on HDMI at standard **720p/60**. If a TV
reports an unsupported mode, return to MENU and use standard MiSTer video
settings. The tested prototype's per-core configuration is:

```ini
[TIC-80]
video_mode=0
vsync_adjust=0
```

The installer does not change `MiSTer.ini`. CRT/analog modes and physical
microphone/FFT capture have not been qualified.

If the frontend does not launch, check `logs/MiSTer_Frontier/Master_Daemon.log`
and `logs/TIC-80/tic80.log`. Verify the bundle with
`bash /media/fat/Scripts/Install_TIC80.sh --check`. Confirm Frontier is registered
in `linux/user-startup.sh`; reboot if the installer deferred its activation.
For a failed update, use the [rollback instructions](install.md#check-and-rollback).
Report the release tag, frontend, cartridge/language, display mode, reproduction
steps and relevant log excerpt. Exclude passwords and private account tokens.
