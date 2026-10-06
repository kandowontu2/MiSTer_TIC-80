# Consolidated hands-on acceptance

Finish independent build, runtime, recovery, package and installation checks
before requesting this session. Keep the same published player, Studio and RBF
through all rounds. If a production fix changes those binaries, retain the
original failure and identify which checks need repeating. A preparation receipt
is not a pass, and an earlier prototype confirmation cannot qualify a new binary.

The current published payloads are player `2a03b849`, Studio `e3613638` and RBF
`5595ba32` from `v0.1.0-dev.20261006`, running with official Main `9f6e5a23`.
The user prefers one consolidated session after independent work finishes.
No new hands-on prompt is needed while the devices are disconnected.

| Round | Human actions | Required evidence |
| --- | --- | --- |
| Picture and stereo | Watch the current player and Studio music display and listen to both channels. | Confirm a stable, correct picture, no unsupported-mode message and clear continuous left/right audio on the exact binaries. Automated frame/clock/underrun measurements are supporting evidence. |
| Controller and mouse | Press D-pad directions and A/B/X/Y; move the left stick in every direction; press Start, Back and LB/RB. Move both mouse axes, click left/middle/right and scroll both ways. Release everything for two seconds. | Fresh single-BOOT save, all eight pad and mapped-key press counters, all three mouse buttons, both axes and wheel directions, and 120 released ticks. Confirm the labels and cursor match the actions. |
| Physical keyboard | Press arrows, Z/X/A/S, W/D/Q/E, Enter, Esc, Space, Tab, Backspace, Shift, Ctrl, Alt, 1 and 0. Press Shift+Z, Ctrl+Q and Alt+E; hold W for at least one second, then release everything for two seconds. | A separate fresh single-BOOT save: all 22 keys pressed and released, all three chords, held-W repeat and 120 released ticks. Inventory must show a connected keyboard other than MiSTer's virtual input. Confirm the labels match. Controller-only results cannot substitute. |
| Studio use | In a new isolated test project, edit code, run it, return to the editor, save and reopen it. Check mouse selection and keyboard navigation. | Record the actual workflow and saved/reopened project. Preserve existing games and projects. Keep the private test project until its contents are reviewed. Earlier automated working-copy Save and recovery passes support this check but do not prove physical editor usability. |

The controller/mouse and keyboard observers send no input. They require a
fresh clean-Studio inspection, reject missing physical devices before switching
cores, use private cartridge/save paths and restore the exact installed Studio.
They preserve installed payloads, official Main, settings and existing games.
Collect each original job once; a timeout or lost observation is not permission
to relaunch it.

Tools are `tools/inspect_studio_native.py`, `tools/run_physical_input_native.py`
and `tools/test_hid_frontends_native.py`. Use `--kind controller-mouse` or
`--kind keyboard` with the corresponding fixture. Preparation is recorded in
`build/final-physical-session-preparation-v4-20261006/result.json`; neither
fixture has been credited as a physical pass. The keyboard check explicitly
covers modifiers, repeat and releases beyond the eight controller mappings.

This session qualifies the available HDMI, controller, mouse, keyboard and
Studio workflow. It does not claim untested CRT/analog modes, physical microphone
capture, horizontal-wheel devices or every possible peripheral/cartridge.
Keep those limitations in release documentation rather than silently treating
them as passing results.
