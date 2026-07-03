#ifndef MORTAR_INPUT_EVENT_H
#define MORTAR_INPUT_EVENT_H

#include <cstdint>

// Action type flags (from InputActionMapper::ProcessEvent at 0x1b3508)
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
    // by value, proving the full size): +0x06 ushort keycode, +0x08 InputActionMapper* m_mapper.
    // Port layout differs (see DIFFERS in InputDevice.cpp / InputActionMapper::ProcessEvent);
    // these fields are appended here to keep the call graph compiling.
    uint32_t keycode;      // binary +0x06 (ushort); port uses uint32_t for alignment
    void*    m_mapper;     // binary +0x08 InputActionMapper* matched on DOWN events
};

#endif
