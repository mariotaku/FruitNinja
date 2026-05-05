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

// Binary @ 0x0019591c -- ctor: 16 States (states1+states2) init, RingBufferT init,
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
        states1[i].phase   = 1;
        states1[i].extId   = 0;
        states1[i].touchId = 0;
        states2[i].phase   = 1;
        states2[i].extId   = 0;
        states2[i].touchId = 0;
    }
    memset(m_events, 0, sizeof(m_events));
}

// Binary @ 0x001952f0 -- State::Update.
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

// Binary @ 0x001953ec -- _Update.
// 8x: states1[i] = states2[i]; State::Update on the copy.
void Touch::_Update() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        states1[i] = states2[i];
        StateUpdate(states1[i]);
    }
}

// Binary @ 0x00195630 -- Update(float dt).
// Drain events with timestamp <= dt (or all if dt == 0.0); then _Update().
void Touch::Update(float dt) {
    bool drainAll = (dt == 0.0f);
    while (m_eventHead != m_eventTail) {
        TEvnt& ev = m_events[m_eventHead % MAX_EVENTS];
        if (!drainAll && ev.timestamp > dt) break;
        ___UpdateInternal(ev.extId, ev.isActive, ev.x, ev.y);
        m_eventHead++;
    }
    _Update();
}

// Binary @ 0x00195690 -- __UpdateInternal.
// Push TEvnt to ring; on overflow: Update(0.0f) then retry.
// isActive: true=press OR move, false=release (matches binary param 'b').
void Touch::__UpdateInternal(uint32_t extId, bool isActive, float x, float y, float t) {
    int next = m_eventTail + 1;
    if (next - m_eventHead >= MAX_EVENTS) {
        Update(0.0f);
    }
    TEvnt& ev = m_events[m_eventTail % MAX_EVENTS];
    ev.extId     = extId;
    ev.isActive  = isActive;
    ev.x         = x;
    ev.y         = y;
    ev.timestamp = t;
    m_eventTail++;
}

// Binary @ 0x00195314 -- ___UpdateInternal.
// Apply event to states2: scan for matching extId, then claim free slot if new press.
// isActive: true=press OR move, false=release.
// Binary @ 0x00195314 -- free slot predicate is extId==0, NOT phase>=1.
void Touch::___UpdateInternal(uint32_t extId, bool isActive, float x, float y) {
    int firstFree = -1;
    for (int i = 0; i < MAX_SLOTS; i++) {
        int idx = (m_slotCursor + i) & 7;
        uint32_t slotExtId = states2[idx].extId;
        if (slotExtId == extId) {
            if (isActive) {
                states2[idx].currX = (int32_t)x;
                states2[idx].currY = (int32_t)y;
            } else {
                states2[idx].phase = 1;
            }
            return;
        }
        // Binary @ 0x00195314 -- free slot is extId==0, NOT phase>=1.
        if (slotExtId == 0 && firstFree == -1) firstFree = idx;
    }
    // No matching slot. Claim a free slot only for new press (isActive==true).
    if (isActive && firstFree != -1) {
        m_slotCursor = (m_slotCursor + 1 > 7) ? 0 : m_slotCursor + 1;
        states2[firstFree].touchId = nextTouchId;
        nextTouchId++;
        if (nextTouchId == 0) nextTouchId = 1;
        states2[firstFree].extId  = extId;
        states2[firstFree].prevX  = (int32_t)x;
        states2[firstFree].prevY  = (int32_t)y;
        states2[firstFree].currX  = (int32_t)x;
        states2[firstFree].currY  = (int32_t)y;
        states2[firstFree].phase  = -1;
    }
}

// Binary @ 0x00195424 -- FindTouch(uint touchId).
// Linear scan states1; return slot index or -1.
int Touch::FindTouch(uint32_t touchId) const {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (states1[i].touchId == touchId) return i;
    }
    return -1;
}

// Binary @ 0x001954fc -- GetAnyTouch.
// First slot with phase < 1; returns touchId or 0.
uint32_t Touch::GetAnyTouch() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (states1[i].phase < 1) return states1[i].touchId;
    }
    return 0;
}

// Binary @ 0x0019551c -- GetMostRecentTouch.
// FindTouch(nextTouchId - 1); returns touchId or 0.
uint32_t Touch::GetMostRecentTouch() {
    int slot = FindTouch(nextTouchId - 1);
    if (slot < 0) return 0;
    return states1[slot].touchId;
}

// Binary @ 0x0019543c -- GetTouchPos.
// Writes currX/Y of matching slot. Returns 1 if active (phase < 1), 0 if not.
// Binary leaves *x/*y UNTOUCHED on miss -- do not zero them.
int Touch::GetTouchPos(uint32_t touchId, int& x, int& y) const {
    int slot = FindTouch(touchId);
    if (slot < 0) return 0;
    x = states1[slot].currX;
    y = states1[slot].currY;
    return (states1[slot].phase < 1) ? 1 : 0;
}

// Binary @ 0x0019546c -- GetTouchDelta.
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

// Binary @ 0x001954b4 -- GetTouchInReigion (note binary typo).
// Find first active touch inside (x, y, x+w, y+h). Returns touchId or 0.
// Binary uses inclusive <= on all bounds.
uint32_t Touch::GetTouchInReigion(int x, int y, int w, int h) {
    int x1 = x + w;
    int y1 = y + h;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (states1[i].phase >= 1) continue;
        int cx = states1[i].currX;
        int cy = states1[i].currY;
        if (cx >= x && cx <= x1 && cy >= y && cy <= y1) {
            return states1[i].touchId;
        }
    }
    return 0;
}

// Binary @ 0x00195764 -- SendIndividualTouchCallbacks.
// Per-slot: AxisEvent(X), AxisEvent(Y), ButtonPressed for press/held/release/up.
// Action codes: 0x89+i (button), 0x99+i (X axis), 0xa9+i (Y axis), i in 0..7.
// States: 1=press, 2=held, 4=release, 8=up.
// Port specific: InputDevice::AxisEvent / ButtonPressed not yet declared;
//   body is a no-op stub until those virtual methods are ported into InputDevice.
void Touch::SendIndividualTouchCallbacks(InputDevice* /*dev*/) {
    // Stub: requires InputDevice::AxisEvent and InputDevice::ButtonPressed.
    // Full body per RE pseudocode (tmp/re-touch.md):
    //   for i in 0..7:
    //     code = 0x89 + i
    //     if phase < 1 (active):
    //       dev->AxisEvent(code+0x10, 0x20, currX, currX-prevX, 0, 0)  // X
    //       dev->AxisEvent(code+0x20, 0x20, currY, currY-prevY, 0, 0)  // Y
    //       if phase == -1: dev->ButtonPressed(code, 1, 1.0, 0, 0, dev) // press
    //       dev->ButtonPressed(code, 2, 1.0, 0, 0, dev)                 // held
    //     else:
    //       if extId != 0 && touchId != 0: ButtonPressed(code, 4, ...)  // release
    //       dev->ButtonPressed(code, 8, 1.0, 0, 0, dev)                 // up
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
// Port-specific helpers -- wrap __UpdateInternal for SDL translator.
// OnPressed: isActive=true (press). OnMoved: isActive=true (move).
// OnReleased: isActive=false (release). All route through ring buffer.

void Touch::OnPressed(uint32_t extId, float x, float y) {
    __UpdateInternal(extId, true, x, y, 0.0f);
}

void Touch::OnMoved(uint32_t extId, float x, float y) {
    __UpdateInternal(extId, true, x, y, 0.0f);
}

void Touch::OnReleased(uint32_t extId) {
    __UpdateInternal(extId, false, 0.0f, 0.0f, 0.0f);
}

// Port-specific: direct slot read.
// DIFFERS: binary uses GetTouchPos(touchId, x, y) not slot-indexed GetSlot.
const TouchState* Touch::GetSlot(int slot) const {
    if (slot < 0 || slot >= MAX_SLOTS) return 0;
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

// ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
namespace Mortar {
// STUB: Touch::Clear -- auto stub
void Touch::Clear() {}
}  // namespace Mortar
// ---- end AUTO-STUB MERGE ----
