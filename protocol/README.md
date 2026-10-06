# Shared DDR protocol, version 2

`memory_map.json` is the source of truth. Run `python tools/generate_protocol.py`
after changing it; `--check` verifies generated C and SystemVerilog constants.
All addresses in the definition are byte offsets. The MiSTer DDR master uses
64-bit word addresses, so convert `(physical_base + offset) >> 3` at its boundary.
Each control field occupies the low 32 bits of a separate aligned 64-bit word,
except CART_META and MOUSE, which use both halves, and KEYBOARD_BITS, a
64-byte physical-key bitmap. Version 2 uses a new identity so a mismatched
player/bitstream pair fails its handshake instead of interpreting old input data.
Pixels and PCM are little-endian. The whole region lies in the board's reserved
upper DDR range; the actual map and core identity must be checked before use.

## Startup and restart

The FPGA writes IDENTITY=`0x32434954` (ASCII TIC2), GEOMETRY=`(144<<16)|256`,
and increments HEARTBEAT at each vblank. ARM checks identity, geometry and a
moving heartbeat before touching any payload. It clears VIDEO_PUBLISH and
AUDIO_WRITE, applies a release barrier, then writes a new nonzero SESSION_REQUEST
different from the previous SESSION_ACK. FPGA stops display/audio, resets its
consumer state, clears VIDEO_PRESENTED and AUDIO_READ, then echoes SESSION_ACK.
ARM waits for that echo before publishing payload. Stale memory from an earlier
core or process cannot stand in for a live handshake.

## Video ownership

VIDEO_PUBLISH and VIDEO_PRESENTED use `[31:2]` = 30-bit sequence, `[1]` = valid,
`[0]` = buffer. ARM writes an acquired buffer completely, applies a release
barrier, then writes VIDEO_PUBLISH. FPGA acquires that publication only at
vblank, completes any old-frame DDR reads before acknowledging, and echoes it
in VIDEO_PRESENTED. The old front buffer becomes available only after that
acknowledgment; ARM may have only one publication outstanding. The portable
`tm_exchange` state machine implements this ownership policy. Equality of the
entire control word permits sequence wrap. Session loss requires a new startup
handshake, not guessing which buffer is free. The DDR reader copies a complete
frame into FPGA BRAM before writing its public acknowledgment. A return toggle
from scanout prevents the next BRAM copy until the previous bank is adopted
during vertical blanking, including DMA completion delayed past blanking.

## Audio

AUDIO_WRITE (ARM) and AUDIO_READ (FPGA) are wrapping unsigned 32-bit counters
of **stereo sample frames**. Occupancy is `write-read`; it must not exceed 4096.
Ring indexing masks the counter with 4095; each entry is four bytes, signed S16
left then right. Producer writes PCM then releases AUDIO_WRITE; consumer reads
PCM then releases AUDIO_READ. Neither side overwrites the other's counter.
Consumer underrun produces silence without advancing AUDIO_READ. The producer
currently publishes an even number of frames; TIC-80 emits 800 per tick. The
reader copies pairs into a 256-frame asynchronous FIFO; AUDIO_READ advances
after playback, and a Gray counter synchronizes the acknowledgment back to
DDR. Startup or
session change flushes counters and audio state. FPGA can fetch two entries per
64-bit DDR beat, but must not consume an unpublished second entry.

## Cartridge transfers and menu state

The byte-wide F0 download stages native `.tic` files in CART_DATA, bounded to
4 MiB. CART_META packs the byte length in `[63:32]`, a 30-bit sequence in
`[31:2]`, and state in `[1:0]`: 0 empty, 1 receiving, 2 ready, 3 invalid transfer.
FPGA publishes ready only after all DDR writes, including the partial final
word, have been accepted. Nonsequential addresses and oversize transfers fail.
The next sequence derives from CART_ACK, so stale DDR from an earlier core
cannot make a new download appear already acknowledged.

ARM copies the whole ready payload into private memory, verifies session and
ticket stability, then echoes the complete low 32-bit ticket to CART_ACK.
It acknowledges failed transfers too, to release staging. FPGA applies
`ioctl_wait` while an accepted write is pending or the previous ticket is
unacknowledged. Staging cannot overwrite a payload while ARM copies it.
Native cartridge envelope validation and VM creation happen after the copy;
failure preserves the existing game and its private cartridge.

STATUS contains the menu status, with bit 0 requesting cartridge reset. The
MiSTer keyboard/MGL reset button also sets bit 0; ARM acts on its rising edge.
JOY0..3 are FPGA-written snapshots.
The ARM backend
uses `/dev/mem` with `O_SYNC`, rejects kernel RAM overlap, and uses ARM `dmb sy`
barriers. Live frame exchanges and MiSTer pipeline captures verify visibility
on the development board. Portable tests alone do not prove cache coherency.

## Keyboard and mouse snapshots

The FPGA captures MiSTer's decoded PS/2 set-2 events. KEYBOARD_BITS contains
512 physical held-key bits: ordinary scancode `n` occupies bit `n`; extended
scancode `n` occupies bit `256+n`. Left/right modifiers stay distinct until
ARM maps them to TIC-80 key codes, preventing a release on one side from
clearing the other. The runtime's four-key buffer prioritizes held modifiers,
then remaining keys in TIC key-code order. The default keyboard gamepad mapping
matches upstream: arrows and Z/X/A/S for player one, combined with controllers.

KEYBOARD is the snapshot sequence. On changed input only, FPGA captures the
entire bitmap and mouse, publishes an odd sequence, writes the snapshot, then
publishes the next even sequence. ARM accepts data only between matching even
reads; during a conflicting update it retains the last coherent snapshot.
Unchanged state is not rewritten, avoiding starvation of the reader.

MOUSE low32 contains gameplay x in `[7:0]`, y in `[15:8]`, and PS/2 left/right/
middle buttons in `[18:16]`. The pointer starts at (120,68), clips to 240x136,
and integrates signed X/Y deltas with the PS/2 Y direction corrected. ARM adds
the border margin expected by the pinned player API. MOUSE high32 is a wrapping
signed raw PS/2 vertical-wheel total (positive downward). ARM reverses its sign
to match TIC-80's positive-upward scroll API, consumes deltas into a queue, and delivers at
most the runtime's signed six-bit range each tick; large events are not dropped.
The common MiSTer PS/2 transport does not provide horizontal-wheel events;
that portion of full mouse support remains pending.

OSD visibility clears held keys/buttons and discards menu-time events while
keeping pointer position, so releases filtered by MiSTer's menu cannot leave
controls stuck. F12 is passed through to cartridges; Win/Command+F12 opens the
MiSTer menu instead. A board uinput probe exercised normal MiSTer device
discovery and translation, HPS events, FPGA capture, DDR snapshots and Lua
key/mouse APIs. It verified modifier aliases, wheel direction, and clearing
controls across OSD transitions. Physical keyboard/mouse qualification and
horizontal wheel support remain pending; see `docs/validation.md`.
