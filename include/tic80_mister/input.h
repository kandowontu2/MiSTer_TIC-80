#ifndef TIC80_MISTER_INPUT_H
#define TIC80_MISTER_INPUT_H
#include "tic80.h"
#include "tic80_mister/backend.h"
typedef struct {
    uint32_t wheel;
    uint32_t horizontal_wheel;
    int initialized;
    int64_t pending_wheel;
    int64_t pending_horizontal_wheel;
} tm_input_state;
/* Fills keyboard/mouse and ORs the upstream default keyboard controls into
 * player one, preserving separately transported controller buttons. MiSTer
 * buttons 8..15 supply keyboard-only W/A/S/D/Enter/Esc/Q/E from any controller.
 * Horizontal wheel follows SDL platform direction, for tm_studio_tick(). */
void tm_input_convert(tm_input_state *state, const tm_input_snapshot *snapshot, tic80_input *input);
/* Library player input matches the pinned Linux Studio cartridge mouse() API. */
void tm_input_convert_player(tm_input_state *state, const tm_input_snapshot *snapshot, tic80_input *input);
#endif
