#ifndef FN_INPUT_INPUT_SINK_H
#define FN_INPUT_INPUT_SINK_H

//
// Mortar::InputSink — base class for touch-input routing.
// Binary: 0x0010ea30 (TouchReleased thunk), sizeof=144 (/Mortar::InputSink).
// Port stub: only the GameUpdate-touch-reset path is needed; vtable slot
// order not yet RE'd.
//
// TODO: v1.6.1 0x001CF534 (GameUpdate) — full InputSink class RE + vtable layout.
//

#include "engine/math/_Vector3.h"

struct InputEvent;

namespace Mortar {

class InputSink {
public:
    virtual ~InputSink() {}

    // Binary: InputSink::TouchReleased @ 0x0010ea30.
    // Called from GameUpdate touch-reset loop when finger z==0.
    virtual void TouchReleased(InputEvent* evt, _Vector3<float>* pos) = 0;
};

} // namespace Mortar

#endif
