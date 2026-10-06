#include "tic80_mister/input.h"
#include "tic.h"
#include <string.h>
/* PS/2 set 2. Physical codes remain distinct until this conversion, including
 * the two sides of modifiers and extended versus keypad navigation keys. */
static const uint8_t keymap[512] = {
    [0x1c]=tic_key_a, [0x32]=tic_key_b, [0x21]=tic_key_c, [0x23]=tic_key_d,
    [0x24]=tic_key_e, [0x2b]=tic_key_f, [0x34]=tic_key_g, [0x33]=tic_key_h,
    [0x43]=tic_key_i, [0x3b]=tic_key_j, [0x42]=tic_key_k, [0x4b]=tic_key_l,
    [0x3a]=tic_key_m, [0x31]=tic_key_n, [0x44]=tic_key_o, [0x4d]=tic_key_p,
    [0x15]=tic_key_q, [0x2d]=tic_key_r, [0x1b]=tic_key_s, [0x2c]=tic_key_t,
    [0x3c]=tic_key_u, [0x2a]=tic_key_v, [0x1d]=tic_key_w, [0x22]=tic_key_x,
    [0x35]=tic_key_y, [0x1a]=tic_key_z,
    [0x45]=tic_key_0, [0x16]=tic_key_1, [0x1e]=tic_key_2, [0x26]=tic_key_3,
    [0x25]=tic_key_4, [0x2e]=tic_key_5, [0x36]=tic_key_6, [0x3d]=tic_key_7,
    [0x3e]=tic_key_8, [0x46]=tic_key_9,
    [0x4e]=tic_key_minus, [0x55]=tic_key_equals, [0x54]=tic_key_leftbracket,
    [0x5b]=tic_key_rightbracket, [0x5d]=tic_key_backslash, [0x4c]=tic_key_semicolon,
    [0x61]=tic_key_backslash, /* ISO extra key */
    [0x52]=tic_key_apostrophe, [0x0e]=tic_key_grave, [0x41]=tic_key_comma,
    [0x49]=tic_key_period, [0x4a]=tic_key_slash, [0x29]=tic_key_space,
    [0x0d]=tic_key_tab, [0x5a]=tic_key_return, [0x66]=tic_key_backspace,
    [0x171]=tic_key_delete, [0x170]=tic_key_insert, [0x17d]=tic_key_pageup,
    [0x17a]=tic_key_pagedown, [0x16c]=tic_key_home, [0x169]=tic_key_end,
    [0x175]=tic_key_up, [0x172]=tic_key_down, [0x16b]=tic_key_left,
    [0x174]=tic_key_right, [0x58]=tic_key_capslock,
    [0x14]=tic_key_ctrl, [0x114]=tic_key_ctrl,
    [0x12]=tic_key_shift, [0x59]=tic_key_shift,
    [0x11]=tic_key_alt, [0x111]=tic_key_alt,
    [0x76]=tic_key_escape, [0x05]=tic_key_f1, [0x06]=tic_key_f2,
    [0x04]=tic_key_f3, [0x0c]=tic_key_f4, [0x03]=tic_key_f5,
    [0x0b]=tic_key_f6, [0x83]=tic_key_f7, [0x0a]=tic_key_f8,
    [0x01]=tic_key_f9, [0x09]=tic_key_f10, [0x78]=tic_key_f11, [0x07]=tic_key_f12,
    [0x70]=tic_key_numpad0, [0x69]=tic_key_numpad1, [0x72]=tic_key_numpad2,
    [0x7a]=tic_key_numpad3, [0x6b]=tic_key_numpad4, [0x73]=tic_key_numpad5,
    [0x74]=tic_key_numpad6, [0x6c]=tic_key_numpad7, [0x75]=tic_key_numpad8,
    [0x7d]=tic_key_numpad9, [0x79]=tic_key_numpadplus, [0x7b]=tic_key_numpadminus,
    [0x7c]=tic_key_numpadmultiply, [0x14a]=tic_key_numpaddivide,
    [0x15a]=tic_key_numpadenter, [0x71]=tic_key_numpadperiod,
};
void tm_input_convert(tm_input_state *state, const tm_input_snapshot *s, tic80_input *input)
{
    unsigned char held[tic_keys_count] = {0};
    for (unsigned physical = 0; physical < 512; ++physical)
        if (s->keys[physical/32] & (1u << (physical%32))) held[keymap[physical]] = 1;
    // Matches the pinned studio's default mapping without forcing MiSTer's
    // keyboard-as-joystick mode, which would swallow keyboard API modifiers.
    static const tic_key gamepad_keys[8] = {tic_key_up, tic_key_down, tic_key_left,
        tic_key_right, tic_key_z, tic_key_x, tic_key_a, tic_key_s};
    for (unsigned button = 0; button < 8; ++button)
        if (held[gamepad_keys[button]]) input->gamepads.data |= 1u << button;
    /* These are keyboard-only controller actions. In particular, left-stick
     * A/S must not also press TIC gamepad buttons 6/7. The physical keyboard
     * retains the upstream keyboard-to-gamepad defaults above. */
    static const tic_key controller_keys[8] = {tic_key_w, tic_key_a, tic_key_s,
        tic_key_d, tic_key_return, tic_key_escape, tic_key_q, tic_key_e};
    uint32_t controllers = 0;
    for (unsigned pad = 0; pad < 4; ++pad) controllers |= s->joystick[pad];
    for (unsigned action = 0; action < 8; ++action)
        if (controllers & (1u << (8 + action))) held[controller_keys[action]] = 1;
    input->keyboard.data = 0;
    unsigned slot = 0;
    /* Keep modifiers available when more than the runtime's four-key limit
     * are physically held, then choose remaining keys deterministically. */
    for (unsigned key = tic_key_ctrl; key <= tic_key_alt; ++key)
        if (held[key]) input->keyboard.keys[slot++] = (tic_key)key;
    for (unsigned key = 1; key < tic_keys_count && slot < TIC80_KEY_BUFFER; ++key)
        if (held[key] && !(key >= tic_key_ctrl && key <= tic_key_alt))
            input->keyboard.keys[slot++] = (tic_key)key;
    input->mouse = (tic80_mouse){0};
    // The player API receives border-inclusive coordinates; mouse() subtracts
    // the margin internally. Our FPGA accumulator uses gameplay coordinates.
    input->mouse.x = (uint8_t)s->mouse + TIC80_OFFSET_LEFT;
    input->mouse.y = (uint8_t)(s->mouse >> 8) + TIC80_OFFSET_TOP;
    input->mouse.left = (s->mouse >> 16) & 1;
    input->mouse.right = (s->mouse >> 17) & 1;
    input->mouse.middle = (s->mouse >> 18) & 1;
    if (!state->initialized) {
        state->wheel = s->wheel;
        state->horizontal_wheel = s->horizontal_wheel;
        state->initialized = 1;
    }
    uint32_t delta = s->wheel - state->wheel;
    state->wheel = s->wheel;
    // Linux mousedev/MiSTer's PS/2 wheel is positive downward; TIC-80's
    // scrolly is positive upward. Negate in 64 bits, including counter wrap.
    state->pending_wheel -= delta <= INT32_MAX ? (int64_t)delta : (int64_t)delta - 4294967296LL;
    int step = state->pending_wheel > 31 ? 31 : state->pending_wheel < -32 ? -32 : (int)state->pending_wheel;
    input->mouse.scrolly = step;
    state->pending_wheel -= step;
    delta = s->horizontal_wheel - state->horizontal_wheel;
    state->horizontal_wheel = s->horizontal_wheel;
    // Platform input follows SDL: positive rightward. The pinned Linux Studio
    // reverses this before both editor processing and cartridge execution.
    state->pending_horizontal_wheel += delta <= INT32_MAX ? (int64_t)delta : (int64_t)delta - 4294967296LL;
    // Studio negates this signed six-bit field. -32 cannot be negated in six
    // bits, so use symmetric limits and retain every excess detent for later.
    step = state->pending_horizontal_wheel > 31 ? 31 : state->pending_horizontal_wheel < -31 ? -31 : (int)state->pending_horizontal_wheel;
    input->mouse.scrollx = step;
    state->pending_horizontal_wheel -= step;
}
void tm_input_convert_player(tm_input_state *state, const tm_input_snapshot *s, tic80_input *input)
{
    tm_input_convert(state, s, input);
    // The library player bypasses Studio's Linux mouse processing. Apply its
    // horizontal convention here so the same cart sees identical input.
    input->mouse.scrollx = -input->mouse.scrollx;
}
