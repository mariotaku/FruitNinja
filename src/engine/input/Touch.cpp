// Analysed: 2026-05-04T00:00

#include "input/Touch.h"
#include "input/InputDevice.h"
#include <cstring>
#include <new>

namespace Mortar {

// Binary BSS global @ GOT+0x80798 — rotating cursor for ___UpdateInternal.
// Port maps this to a file-static (Touch is a singleton; behaviour is identical).
static int s_slotCursor = 0;

Touch& Touch::GetInstance() {
    static Touch instance;
    return instance;
}

// Binary @ 0x0019591c -- ctor.
// Sequence: State[8] ctor (states1), State[8] ctor (states2),
// RingBufferT ctor (zeroes memory ptr), InitRingBuffer_Touch (alloc + init),
// nextTouchId = 1.
Touch::Touch()
    : nextTouchId(1)
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
    // Binary @ 0x001958fc -- InitRingBuffer_Touch:
    //   eventBuffer.memory = operator new(200);
    //   eventBuffer.capacity = 10;
    //   eventBuffer.field_8 = 1;  (DIFFERS: port uses 0; see RingBufferT_TEvnt DIFFERS)
    //   eventBuffer.field_c = 0;
    eventBuffer.memory      = static_cast<TEvnt*>(::operator new(MAX_EVENTS * sizeof(TEvnt)));
    eventBuffer.capacity    = MAX_EVENTS;
    eventBuffer.m_eventHead = 0;
    eventBuffer.m_eventTail = 0;
    memset(eventBuffer.memory, 0, MAX_EVENTS * sizeof(TEvnt));
}

Touch::~Touch() {
    ::operator delete(eventBuffer.memory);
    eventBuffer.memory = 0;
}

// Binary @ 0x001952f0 -- State::Update.
// Called per-frame on states2 (the LIVE mailbox) by _Update.
// phase==1 (released): zero extId+touchId -> slot becomes free for next press.
// Else: prev <- curr; if phase==-1 (just-pressed) promote to 0 (held).
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
// Per-frame: snapshot states2 -> states1 for readers, then advance states2's
// per-slot state machine via StateUpdate (frees released slots by zeroing
// extId/touchId on phase==1; promotes phase -1 -> 0; rolls prev <- curr).
// IMPORTANT: StateUpdate runs on states2 (the LIVE mailbox), not states1.
// ASM-verified: 2026-05-06T17:00 binary @ 0x001953ec (re-analyst) -- the
// disassembly at 0x00195414 computes r0 = (this + i*0x1c) + 0xe0 = &states2[i],
// not the copy in states1. Running StateUpdate on states1 (the dead snapshot)
// leaves states2's extId stuck after release, so the next press from the
// same finger ID matches the stale slot and never registers as a new press
// (the user's "touch only works once" symptom).
void Touch::_Update() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        states1[i] = states2[i];
        StateUpdate(states2[i]);
    }
}

// Binary @ 0x00195630 -- Update(float dt).
// Drain events with timestamp <= dt (or all if dt == 0.0); then _Update().
void Touch::Update(float dt) {
    bool drainAll = (dt == 0.0f);
    while (eventBuffer.m_eventHead != eventBuffer.m_eventTail) {
        TEvnt& ev = eventBuffer.memory[eventBuffer.m_eventHead % MAX_EVENTS];
        if (!drainAll && ev.timestamp > dt) break;
        ___UpdateInternal(ev.extId, ev.isActive, ev.x, ev.y);
        eventBuffer.m_eventHead++;
    }
    _Update();
}

// Binary @ 0x00195690 -- __UpdateInternal.
// Push TEvnt to ring; on overflow: Update(0.0f) then retry.
// isActive: true=press OR move, false=release (matches binary param 'b').
void Touch::__UpdateInternal(uint32_t extId, bool isActive, float x, float y, float t) {
    int next = eventBuffer.m_eventTail + 1;
    if (next - eventBuffer.m_eventHead >= MAX_EVENTS) {
        Update(0.0f);
    }
    TEvnt& ev = eventBuffer.memory[eventBuffer.m_eventTail % MAX_EVENTS];
    ev.extId     = extId;
    ev.isActive  = isActive;
    ev.x         = x;
    ev.y         = y;
    ev.timestamp = t;
    eventBuffer.m_eventTail++;
}

// Binary @ 0x00195314 -- ___UpdateInternal.
// Apply event to states2: scan for matching extId, then claim free slot if new press.
// isActive: true=press OR move, false=release.
// Binary @ 0x00195314 -- free slot predicate is extId==0, NOT phase>=1.
void Touch::___UpdateInternal(uint32_t extId, bool isActive, float x, float y) {
    int firstFree = -1;
    for (int i = 0; i < MAX_SLOTS; i++) {
        int idx = (s_slotCursor + i) & 7;
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
        s_slotCursor = (s_slotCursor + 1 > 7) ? 0 : s_slotCursor + 1;
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

// Binary @ 0x00195764 (v1.5.1) / 0x00242bc4 (v1.6.1) -- SendIndividualTouchCallbacks.
// Per slot i in 0..7 (action code = 0x89+i, X-axis = 0x99+i, Y-axis = 0xa9+i):
//   if phase < 1 (active):
//     AxisEvent(0x99+i, 0x20, currX, currX-prevX, 0, 0)
//     AxisEvent(0xa9+i, 0x20, currY, currY-prevY, 0, 0)
//     if phase == -1: ButtonPressed(0x89+i, 1, 1.0f, 0, 0)  // press-edge
//     ButtonPressed(0x89+i, 2, 1.0f, 0, 0)                  // held
//   else (inactive):
//     if extId != 0 && touchId != 0: ButtonPressed(0x89+i, 4, 1.0f, 0, 0)  // release
//     ButtonPressed(0x89+i, 8, 1.0f, 0, 0)                  // up
// Arg order matches binary signatures @ 0x0027582c (AxisEvent) / 0x00275864 (ButtonPressed).
void Touch::SendIndividualTouchCallbacks(InputDevice* dev) {
    for (int i = 0; i < MAX_SLOTS; i++) {
        const TouchState& s = states1[i];
        unsigned long code = (unsigned long)(0x89 + i);
        unsigned long mask;
        if (s.phase < 1) {
            dev->AxisEvent((long)(code + 0x10), 0x20,
                           (float)s.currX, (float)(s.currX - s.prevX), 0, 0);
            dev->AxisEvent((long)(code + 0x20), 0x20,
                           (float)s.currY, (float)(s.currY - s.prevY), 0, 0);
            if (s.phase == -1) {
                dev->ButtonPressed(code, 1, 1.0f, 0, 0);
            }
            mask = 2;
        } else {
            if (s.extId != 0 && s.touchId != 0) {
                dev->ButtonPressed(code, 4, 1.0f, 0, 0);
            }
            mask = 8;
        }
        dev->ButtonPressed(code, mask, 1.0f, 0, 0);
    }
}

// Tier A slot-returning region scan. Mirrors binary's free function
// TouchInRegion @ 0x001691cc (the API actually used by UI widgets).
// NOT to be confused with binary's same-class GetTouchInReigion @ 0x001954b4
// which returns touchId -- that Tier B method is dead in the binary (no
// internal callers; only used externally by InputDeviceBada::Update which
// reaches into Touch via GetMostRecentTouch / GetTouchPos directly).
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

// Binary @ 0x0019553c -- Mortar::Touch::Clear().
// Called by InputDeviceBada::Reset @ 0x00195c00 (NOT inlined). Real helper symbol.
// Zeroes ONLY states2 (8 slots, phase=1); leaves states1 and the ring buffer untouched.
// Disassembly confirmed: loops 8 times over states2 (base this+0xe0, stride 0x1c),
// writing prevX=prevY=currX=currY=extId=touchId=0, phase=1 per slot.
void Touch::Clear() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        states2[i].prevX   = 0;
        states2[i].prevY   = 0;
        states2[i].currX   = 0;
        states2[i].currY   = 0;
        states2[i].extId   = 0;
        states2[i].touchId = 0;
        states2[i].phase   = 1;
    }
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

// Tier A slot read helper. Returns TouchState* by slot index for callers
// that already track a slot (latched via Tier A GetTouchInRegion). Binary
// Tier B equivalent GetTouchPos(touchId, &x, &y) is used only by
// InputDeviceBada::Update -- UI widgets use the slot model throughout.
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

// TouchInRegion @ 0x001691cc -- Tier A slot-returning helper. Used by every
// UI widget Update() in the binary (MenuButton, CheckBox, ScrollingMenu,
// SliderControl, VerticalScroller, ComboBox). Port uses (left, right,
// bottom, top) instead of binary's (x, y, w, h) -- numerically equivalent
// since all call sites compute edges from pos +/- halfSize.
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

// IsTouchDown @ 0x00169144 (asm-verified 2026-05-17 re-analyst)
// Binary reads phase float at states[slot]+0xa8 and returns:
//   phase float == 2.0f -> 2  (press-edge, just-pressed, one frame only)
//   phase float == 1.0f -> 1  (held)
//   else                -> 0  (up/inactive)
// Port maps its int phase enum to the same return values:
//   port phase == -1 (just-pressed) -> 2
//   port phase ==  0 (held)         -> 1
//   port phase >= 1 (released/free) -> 0
// MenuButton::Update toggle gate depends on `IsTouchDown == 2` to fire on
// press-edge only and reject slice-drags through the button. Signature
// matches binary verbatim (takes slot index, returns int 0/1/2).
int IsTouchDown(int slot) {
    const Touch& t = Touch::GetInstance();
    if (slot < 0 || slot >= Touch::MAX_SLOTS) return 0;
    int ph = t.states1[slot].phase;
    if (ph >= 1)  return 0;
    if (ph == -1) return 2;
    return 1;
}

} // namespace Mortar
