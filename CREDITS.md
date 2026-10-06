# Credits and third-party notices

Project direction, hardware testing and maintenance: **kando**
([kandowontu2](https://github.com/kandowontu2)). The integration was developed
with assistance from **OpenAI Codex**. Original integration contributions are
GPL-3.0-or-later. This is a community port; the upstream projects retain their
own authorship, copyright and licenses.

## Foundation

| Project / contributors | Contribution | License / source |
| --- | --- | --- |
| Vadim Grigoruk (nesbox) and TIC-80 contributors | TIC-80 runtime, Studio, language bindings and upstream demo cartridges | [TIC-80](https://github.com/nesbox/TIC-80), MIT |
| MiSTerOrganize and MiSTer_PICO-8 contributors | Hybrid-core design reference, pinned MiSTer platform, PLL and interface sources | [MiSTer_PICO-8](https://github.com/MiSTerOrganize/MiSTer_PICO-8), GPL notices retained |
| Alexey Melnikov (Sorgelig), MiSTer-devel and platform contributors | MiSTer framework, Menu interface and standard Main transport | [MiSTer](https://github.com/MiSTer-devel), original GPL notices retained |
| TEMLIB | Original scaler implementation underlying the local arithmetic override | Original `ascal.vhd` notices retained in source |
| bellwood420 | Safe F2SDRAM terminator source | Copyright header retained in `fpga/sys/f2sdram_safe_terminator.sv` |
| MiSTerOrganize and Frontier contributors | Shared core discovery and ARM launcher daemon | [MiSTer_Frontier](https://github.com/MiSTerOrganize/MiSTer_Frontier), GPL-3.0 |
| Intel / Altera | Cyclone V PLL and reconfiguration IP used by the platform | Original IP headers and terms retained; Quartus is obtained separately |

Pinned source revisions and integration modifications are recorded in
[NOTICE](NOTICE), [build instructions](docs/building.md) and the release's
source manifest. Authors listed in the original source headers also receive
credit; this table does not replace their notices.

## Runtime and Studio dependencies

The release includes the original available license files under `licenses/`.
For dependencies whose notice lives in a source file, the corresponding file is
also supplied there. The source archive contains the pinned dependency sources.

| Component | Authors / contributors | License |
| --- | --- | --- |
| Lua | Lua.org, PUC-Rio and contributors | MIT |
| MoonScript | Leaf Corcoran and contributors; its bundled LPeg / Lua code retains separate notices | MIT |
| YueScript | Li Jin and contributors | MIT |
| Fennel | Calvin Rose and contributors | MIT |
| s7 Scheme | Bill Schottstaedt and contributors named in s7 source | 0BSD |
| Squirrel | Alberto Demichelis and contributors | MIT |
| QuickJS | Fabrice Bellard, Charlie Gordon and contributors | MIT |
| pocketpy | blueloveTH and contributors | MIT |
| Wren | Robert Nystrom and Wren contributors | MIT |
| Janet | Calvin Rose and contributors | MIT |
| wasm3 | Steven Massey, Volodymyr Shymanskyy and contributors | MIT |
| mruby | mruby developers | MIT |
| MiniScript | Joe Strout and contributors | MIT |
| pForth | Phil Burk; source also credits 3DO, Larry Polansky and David Rosenboom | ISC-style notice supplied |
| blip-buf | Shay Green | LGPL-2.1-or-later; original notices and full source supplied |
| libpng | PNG Reference Library authors, including Cosmin Truta, Glenn Randers-Pehrson, Andreas Dilger and Guy Eric Schalnat / Group 42 | PNG Reference Library license |
| zlib | Jean-loup Gailly and Mark Adler | zlib |
| giflib | Eric S. Raymond and contributors | MIT-style notice supplied |
| zip | kuba--/zip contributors | Original license supplied, including embedded compression notices |
| argparse | Yecheng Fu (cofyc) | MIT |
| jsmn | Serge A. Zaitsev | MIT |
| LPeg | Roberto Ierusalimschy and contributors | MIT |
| miniaudio | David Reid and contributors | Public domain / MIT-0 alternatives retained |
| MD5 implementation | Alexander Peslyak (Solar Designer) | Public-domain dedication with permissive fallback notice |
| dirent and dlfcn portability sources | Authors identified in their supplied notices | Original notices supplied |
| GNU compiler support libraries | Free Software Foundation and GCC contributors | GPL with GCC Runtime Library Exception; toolchain supplied separately |
| curl and Mozilla CA data | curl contributors and Mozilla certificate-store contributors | curl executable supplied by the system; bundled CA data is MPL-2.0 |

No proprietary PICO-8 executable or commercial cartridge collection is included.
User-supplied cartridges remain the work of their respective authors.
The Tetris cartridge pictured in the README credits **Bob** in its metadata;
its original code, artwork and music remain its author's work. It is shown for
demonstration and is not bundled with this installer.

The optional experimental Main patches are documented for development in
[stock-Main compatibility](docs/stock-main.md). The Frontier installer release
does not distribute a replacement Main executable.

The development HID input implementation references the Linux kernel
community's [HIDraw documentation](https://www.kernel.org/doc/html/latest/hid/hidraw.html),
originally documented by Alan Ott of Signal 11 Software, and the kernel HID
maintainers' resolution-multiplier and USB/Bluetooth transport behavior.
The native input diagnostic also references
[UHID documentation](https://www.kernel.org/doc/html/latest/hid/uhid.html),
written by David Herrmann, and the kernel's public UHID API headers.
These are API/behavior references; kernel driver source is not bundled into
the TIC-80 executable.
