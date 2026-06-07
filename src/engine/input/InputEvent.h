#ifndef MORTAR_INPUT_EVENT_H
#define MORTAR_INPUT_EVENT_H

#include <cstdint>

// Action type flags (from InputActionMapper::ProcessEvent at 0x1b3508)
static const uint32_t INPUT_ACTION_DOWN = 0x10000;
static const uint32_t INPUT_ACTION_MOVE = 0x20000;
static const uint32_t INPUT_ACTION_UP   = 0x80000;

struct InputEvent {
    uint32_t actionHash;   // StringHash of action name
    uint32_t actionFlags;  // INPUT_ACTION_DOWN/MOVE/UP
    int      fingerId;     // touch finger index (0-15)
    float    x, y;         // position in game coords (480x320)
    float    deltaX, deltaY; // movement delta (for move events)
    // Binary InputEvent (12B): +0x06 ushort keycode, +0x08 InputActionMapper* m_mapper.
    // Port layout differs (see DIFFERS in InputDevice.cpp / InputActionMapper::ProcessEvent);
    // these fields are appended here to keep the call graph compiling.
    // TODO: binary addr unknown -- reconcile with binary 12-byte InputEvent layout.
    uint32_t keycode;      // binary +0x06 (ushort); port uses uint32_t for alignment
    void*    m_mapper;     // binary +0x08 InputActionMapper* matched on DOWN events
};

#endif
