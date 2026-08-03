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

// Usage: a screen that wants to swallow raw touch installs itself on
// game_work.m_pActiveTouchSink (+0x1AC). While that pointer is non-null the
// per-finger callbacks in GameTaskInput.cpp offer every touch to the sink
// FIRST; the blade only sees a touch the sink declines.
//
//   TouchDown  -- return non-zero to CONSUME the press (blade gets nothing).
//   TouchMoveX / TouchMoveY -- called instead of the blade, never in addition
//                              to it; the return value is ignored.
//   TouchReleased -- called from the GameUpdate finger-age loop when z hits 0.
//
// `pos` is always &game_work.m_FingerSpawnPos[finger], i.e. the sink may read
// AND write the caller's slot.
//
// No port class installs a sink yet, so all four are dead in practice; they
// exist so the binary's call graph in TouchDownCallback / PointerMoveCallback
// survives verbatim.
class InputSink {
public:
    virtual ~InputSink() {}

    // Binary: InputSink::TouchReleased @ 0x0010ea30.
    // Called from GameUpdate touch-reset loop when finger z==0.
    virtual void TouchReleased(InputEvent* evt, _Vector3<float>* pos) = 0;

    // TODO: v1.6.1 0x001cbf18 (TouchDownCallback) / 0x001cbfcc
    // (PointerMoveCallback) — these three are called through the sink vtable at
    // slots this port has not RE'd. They are appended at the end here so the
    // already-unfaithful slot order is not disturbed further; resolve the real
    // vtable when the InputSink class itself is RE'd.
    virtual int TouchDown (InputEvent* evt, _Vector3<float>* pos) = 0;
    virtual int TouchMoveX(InputEvent* evt, _Vector3<float>* pos) = 0;
    virtual int TouchMoveY(InputEvent* evt, _Vector3<float>* pos) = 0;
};

} // namespace Mortar

#endif
