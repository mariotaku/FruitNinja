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
};

#endif
