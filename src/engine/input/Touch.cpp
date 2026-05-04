// Analysed: 2026-05-04T00:00

#include "input/Touch.h"
#include "input/InputDevice.h"
#include <cstring>
#include <cstdio>

namespace Mortar {

Touch& Touch::GetInstance() {
    static Touch instance;
    return instance;
}

// Binary @ 0x0019591c — ctor: 16 States (states1+states2) init, RingBufferT init,
// allocate 200B backing (10 TEvnt events), nextTouchId=1.
Touch::Touch()
    : m_eventHead(0)
    , m_eventTail(0)
    , nextTouchId(1)
    , m_slotCursor(0)
{
    memset(states1, 0, sizeof(states1));
    memset(states2, 0, sizeof(states2));
    for (int i = 0; i < MAX_SLOTS; i++) {
        states1[i].phase  = 1;
        states1[i].extId  = 0;
        states1[i].touchId = 0;
        states2[i].phase  = 1;
        states2[i].extId  = 0;
        states2[i].touchId = 0;
    }
    memset(m_events, 0, sizeof(m_events));
}

// Binary @ 0x001952f0 — State::Update.
// phase==1: zero extId+touchId. Else: snapshot prev=curr, promote phase==-1 to 0.
void Touch::StateUpdate(TouchState& s) {
    if (s.phase == 1) {
        s.extId   = 0;
        s.touchId = 0;
    } else {
        s.prevX = s.currX;
        s.prevY = s.currY;
        if (s.phase == -1) {
            s.phase = 0;
        }
    }
}

// Binary @ 0x001953ec — _Update.
// 8x: states1[i] = states2[i]; State::Update on the copy.
void Touch::_Update() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        states1[i] = states2[i];
        StateUpdate(states1[i]);
    }
}

// Binary @ 0x00195630 — Update(float dt).
// Drain events with timestamp <= dt (or all if dt == 0.0); then _Update().
void Touch::Update(float dt) {
    bool drainAll = (dt == 0.0f);
    while (m_eventHead != m_eventTail) {
        TEvnt& ev = m_events[m_eventHead % MAX_EVENTS];
        if (!drainAll && ev.timestamp > dt) break;
        ___UpdateInternal(ev.extId, ev.isMove, ev.x, ev.y);
        m_eventHead++;
    }
    _Update();
}

// Binary @ 0x00195690 — __UpdateInternal.
// Push TEvnt to ring; on overflow: Update(0.0f) then retry.
void Touch::__UpdateInternal(uint32_t extId, bool isMove, float x, float y, float t) {
    int next = m_eventTail + 1;
    if (next - m_eventHead >= MAX_EVENTS) {
        // Ring full: drain all pending events then retry.
        Update(0.0f);
    }
    TEvnt& ev = m_events[m_eventTail % MAX_EVENTS];
    ev.extId     = extId;
    ev.isMove    = isMove;
    ev.x         = x;
    ev.y         = y;
    ev.timestamp = t;
    m_eventTail++;
}

// Binary @ 0x00195314 — ___UpdateInternal.
// Apply event to states2: match by extId or claim free slot at rotating cursor.
// nextTouchId++ skipping 0 on wrap.
// DIFFERS: original skips touchId=0 on wrap; binary @ 0x00195314.
void Touch::___UpdateInternal(uint32_t extId, bool isMove, float x, float y) {
    if (!isMove) {
        // Touch down: find free slot via rotating cursor, claim it.
        int found = -1;
        for (int i = 0; i < MAX_SLOTS; i++) {
            int idx = (m_slotCursor + i) % MAX_SLOTS;
            if (states2[idx].phase >= 1) {
                found = idx;
                break;
            }
        }
        if (found < 0) return;  // all slots occupied
        m_slotCursor = (found + 1) % MAX_SLOTS;

        states2[found].extId  = extId;
        states2[found].currX  = (int32_t)x;
        states2[found].currY  = (int32_t)y;
        states2[found].prevX  = (int32_t)x;
        states2[found].prevY  = (int32_t)y;
        states2[found].phase  = -1;
        // Assign monotonic touchId; skip 0 on wrap.
        // DIFFERS: original skips touchId=0 on wrap; binary @ 0x00195314.
        states2[found].touchId = nextTouchId;
        nextTouchId++;
        if (nextTouchId == 0) nextTouchId = 1;
    } else {
        // Touch move: find slot by extId.
        for (int i = 0; i < MAX_SLOTS; i++) {
            if (states2[i].extId == extId && states2[i].phase < 1) {
                states2[i].currX = (int32_t)x;
                states2[i].currY = (int32_t)y;
                return;
            }
        }
        // Also handle release (isMove == false already handled above, so this
        // branch is for move only — release is handled via extId match with
        // a special sentinel from the port layer).
    }
}

// Binary @ 0x00195424 — FindTouch(uint touchId).
// Linear scan states1; return slot index or -1.
int Touch::FindTouch(uint32_t touchId) const {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (states1[i].touchId == touchId) return i;
    }
    return -1;
}

// Binary @ 0x001954fc — GetAnyTouch.
// First slot with phase < 1; returns touchId or 0.
uint32_t Touch::GetAnyTouch() const {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (states1[i].phase < 1) return states1[i].touchId;
    }
    return 0;
}

// Binary @ 0x0019551c — GetMostRecentTouch.
// FindTouch(nextTouchId - 1); returns touchId or 0.
uint32_t Touch::GetMostRecentTouch() const {
    int slot = FindTouch(nextTouchId - 1);
    if (slot < 0) return 0;
    return states1[slot].touchId;
}

// Binary @ 0x0019543c — GetTouchPos.
// Writes currX/Y of matching slot. Returns 1 if active (phase < 1), 0 if not.
int Touch::GetTouchPos(uint32_t touchId, int& x, int& y) const {
    int slot = FindTouch(touchId);
    if (slot < 0) { x = 0; y = 0; return 0; }
    x = states1[slot].currX;
    y = states1[slot].currY;
    return (states1[slot].phase < 1) ? 1 : 0;
}

// Binary @ 0x0019546c — GetTouchDelta.
// Writes currX-prevX/dy if phase >= 0, else 0. Returns 1 if active.
int Touch::GetTouchDelta(uint32_t touchId, int& dx, int& dy) const {
    int slot = FindTouch(touchId);
    if (slot < 0 || states1[slot].phase >= 1) { dx = 0; dy = 0; return 0; }
    if (states1[slot].phase < 0) {
        dx = 0; dy = 0;
    } else {
        dx = states1[slot].currX - states1[slot].prevX;
        dy = states1[slot].currY - states1[slot].prevY;
    }
    return 1;
}

// Binary @ 0x001954b4 — GetTouchInReigion (note binary typo).
// Find first active touch inside (x, y, x+w, y+h). Returns touchId or 0.
uint32_t Touch::GetTouchInReigion(int x, int y, int w, int h) const {
    int x1 = x + w;
    int y1 = y + h;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (states1[i].phase >= 1) continue;
        int cx = states1[i].currX;
        int cy = states1[i].currY;
        if (cx >= x && cx < x1 && cy >= y && cy < y1) {
            return states1[i].touchId;
        }
    }
    return 0;
}

// Binary @ 0x00195764 — SendIndividualTouchCallbacks.
// TODO: 0x00195764 — InputDevice::AxisEvent / ButtonPressed not yet ported.
void Touch::SendIndividualTouchCallbacks(InputDevice* /*dev*/) {
    // TODO: 0x00195764 — emit AxisEvent(X), AxisEvent(Y), ButtonPressed for
    // each active slot (action codes 0x89..0x90). Requires InputDevice::AxisEvent
    // and ButtonPressed to be ported first.
}

// Port-specific: slot-indexed region scan.
// DIFFERS: binary GetTouchInReigion(x,y,w,h) returns touchId; this shim
//          takes (left,right,bottom,top) and returns slot index for MenuButton/ScrollingMenu.
//          Binary @ 0x001954b4.
int Touch::GetTouchInRegion(float left, float right, float bottom, float top,
                             int preferredSlot) const {
    if (preferredSlot >= 0 && preferredSlot < MAX_SLOTS) {
        const TouchState& s = states1[preferredSlot];
        if (s.phase < 1) {
            float cx = (float)s.currX, cy = (float)s.currY;
            if (cx >= left && cx <= right && cy >= bottom && cy <= top) return preferredSlot;
        }
    }
    for (int i = 0; i < MAX_SLOTS; i++) {
        const TouchState& s = states1[i];
        if (s.phase >= 1) continue;
        float cx = (float)s.currX, cy = (float)s.currY;
        if (cx >= left && cx <= right && cy >= bottom && cy <= top) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Port-specific helpers — wrap __UpdateInternal for SDL translator.
// The SDL translator currently calls these instead of __UpdateInternal directly.
// TODO: 0x00195690 — update InputTranslatorSDL to call __UpdateInternal directly.

void Touch::OnPressed(uint32_t extId, float x, float y) {
    __UpdateInternal(extId, false, x, y, 0.0f);
}

void Touch::OnMoved(uint32_t extId, float x, float y) {
    // Moves go directly to states2 (no queueing needed for move events in port).
    // TODO: 0x00195690 — route through __UpdateInternal with isMove=true.
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (states2[i].extId == extId && states2[i].phase < 1) {
            states2[i].currX = (int32_t)x;
            states2[i].currY = (int32_t)y;
            return;
        }
    }
}

void Touch::OnReleased(uint32_t extId) {
    // Mark the slot as released in states2.
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (states2[i].extId == extId && states2[i].phase < 1) {
            states2[i].phase = 1;
            return;
        }
    }
}

// Port-specific: direct slot read.
// DIFFERS: binary uses GetTouchPos(touchId, x, y) not slot-indexed GetSlot.
const TouchState* Touch::GetSlot(int slot) const {
    if (slot < 0 || slot >= MAX_SLOTS) return nullptr;
    return &states1[slot];
}

bool Touch::IsSlotDown(int slot) const {
    if (slot < 0 || slot >= MAX_SLOTS) return false;
    return states1[slot].phase <= 0;
}

// ---------------------------------------------------------------------------
// Free functions.

// TouchInRegion @ 0x001691cc
// DIFFERS: binary GetTouchInReigion takes (x,y,w,h); this free function uses
//          port's (left,right,bottom,top) convention for existing call sites.
//          Binary @ 0x001954b4 / 0x001691cc.
int TouchInRegion(float x0, float x1, float y0, float y1, int hint_slot) {
    Touch& t = Touch::GetInstance();
    // Fast-path: try preferred slot first.
    if (hint_slot >= 0 && hint_slot < Touch::MAX_SLOTS) {
        const TouchState& s = t.states1[hint_slot];
        if (s.phase < 1) {
            float cx = (float)s.currX, cy = (float)s.currY;
            if (cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1) return hint_slot;
        }
    }
    for (int i = 0; i < Touch::MAX_SLOTS; i++) {
        const TouchState& s = t.states1[i];
        if (s.phase >= 1) continue;
        float cx = (float)s.currX, cy = (float)s.currY;
        if (cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1) return i;
    }
    return -1;
}

// IsTouchDown @ 0x00169144
// Returns 0=up, 1=just-pressed (phase==-1), 2=held (phase==0).
// DIFFERS: takes slot index; binary equivalent is GetTouchPos(touchId,...).
int IsTouchDown(int slot) {
    const Touch& t = Touch::GetInstance();
    if (slot < 0 || slot >= Touch::MAX_SLOTS) return 0;
    int ph = t.states1[slot].phase;
    if (ph >= 1)  return 0;
    if (ph == -1) return 1;
    return 2;
}

} // namespace Mortar
