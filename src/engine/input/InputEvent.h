#ifndef MORTAR_INPUT_EVENT_H
#define MORTAR_INPUT_EVENT_H

#include <cstdint>

// Action type flags — v1.6.1 Mortar::InputActionMapper::ProcessEvent @0x00275728
static const uint32_t INPUT_ACTION_DOWN      = 0x10000;
static const uint32_t INPUT_ACTION_MOVE      = 0x20000;
static const uint32_t INPUT_ACTION_UP        = 0x80000;
// Port specific: genuine press-edge flag, not present in binary.
// Set ONLY on real SDL_FINGERDOWN (first frame of a new touch), never on
// PollHeldFingers re-dispatches. Used by SlashEntity::TouchDown to force
// Reset() even when m_BladeActive is still armed from a prior slice -- mirrors
// the binary's behaviour where the 10ms poll guarantees >=2 DrawSlice frames
// between lift and repress so the latch always decays before the next TouchDown.
static const uint32_t INPUT_ACTION_DOWN_EDGE = 0x100000;

struct InputEvent {
    uint32_t actionHash;   // StringHash of action name
    uint32_t actionFlags;  // INPUT_ACTION_DOWN/MOVE/UP
    int      fingerId;     // touch finger index (0-15)
    float    x, y;         // position in game coords (480x320)
    float    deltaX, deltaY; // movement delta (for move events)
    // Binary InputEvent is 0x14 bytes (5 words; InputActionMapper ctor takes it
    // by value, proving the full size). Word layout, from its two producers
    // v1.6.1 InputDevice::AxisEvent @0x0027582c / InputDevice::ButtonPressed @0x00275864:
    //   +0x00  action word  = mask | 0x20000 (axis) or mask | 0x10000 (button)
    //   +0x04  word 1       = (axisId << 16 | tag) & 0xffffff on axis events; the
    //                         trailing `long` arg on button events. Its HIGH half
    //                         (+0x06) is the ushort keycode ProcessEvent compares.
    //   +0x08  word 2       = the float axis value on axis events; the button/key
    //                         id (e.g. 0x6c MouseButton1) on button events.
    // Port layout differs (see DIFFERS in InputDevice.cpp / InputActionMapper::ProcessEvent);
    // these fields are appended here to keep the call graph compiling.
    uint32_t keycode;      // binary +0x06 (ushort); port uses uint32_t for alignment
    // MISNOMER: binary word 2 is a value word, not an InputActionMapper*. Kept
    // under this name only because ProcessEvent's DOWN arm and the mapper ctor
    // are the sole readers; rename together with the layout-faithful InputEvent.
    void*    m_mapper;     // binary +0x08, compared against m_KeyMask on DOWN events
};

#endif
