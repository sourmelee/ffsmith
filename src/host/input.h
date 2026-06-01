#pragma once
#include <cstdint>

namespace ffsmith {

// Logical buttons. Mirrors the original engine's input bitmask model
// (FieldClass::GetInputKeys / KeyStateToMoveFlag): one bit per logical button,
// the host maps physical keys/pads onto these.
enum Button : uint32_t {
    BTN_NONE    = 0,
    BTN_UP      = 1u << 0,
    BTN_DOWN    = 1u << 1,
    BTN_LEFT    = 1u << 2,
    BTN_RIGHT   = 1u << 3,
    BTN_CONFIRM = 1u << 4, // "A"
    BTN_CANCEL  = 1u << 5, // "B"
    BTN_MENU    = 1u << 6, // "Start"
    BTN_L       = 1u << 7,
    BTN_R       = 1u << 8,
    BTN_DPAD    = BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT,
};

// One logical tick's input snapshot, with edge detection.
struct InputState {
    uint32_t held     = 0; // currently down
    uint32_t pressed  = 0; // went down this tick
    uint32_t released = 0; // went up this tick

    bool isHeld(uint32_t b)     const { return (held & b) != 0; }
    bool isPressed(uint32_t b)  const { return (pressed & b) != 0; }
    bool isReleased(uint32_t b) const { return (released & b) != 0; }
};

} // namespace ffsmith
