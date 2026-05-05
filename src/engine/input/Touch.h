#ifndef MORTAR_TOUCH_H
#define MORTAR_TOUCH_H

// Analysed: 2026-05-04T00:00
//
// Mortar::Touch -- binary @ 0x0019591c area.
// sizeof 0x1d4 (468 bytes):
//   +0x000: State states1[8]  (8 * 28 = 224B) -- live polled state
//   +0x0e0: State states2[8]  (8 * 28 = 224B) -- event-applied scratch
//   +0x1c0: event ring buffer (16B)
//   +0x1d0: uint32_t nextTouchId (init 1)
//
// Phase semantics (State::phase, field +0x18):
//   -1 : just-pressed (one frame)
//    0 : held
//    1 : released / free (also 0 extId + touchId)
//
// Double-buffer model:
//   __UpdateInternal (SDL entry) pushes TEvnt to ring.
//   Update(dt) drains ring via ___UpdateInternal -> states2, then calls _Update().
//   _Update() copies states2 -> states1, then State::Update() on each slot.
//
// SDL event flow: InputTranslatorSDL calls __UpdateInternal per event.
// Once per frame InputManager::Update broadcasts Update(dt) -> Touch::Update(dt).
//
// TODO: 0x002772d4+0xa0 -- global 16-slot touch table feeding IsTouchDown /
//   TouchInRegion free functions. Port currently reads from Mortar::Touch::states1
//   as a substitute. Real backing source unconfirmed; needs follow-up RE on
//   GlesForm::OnTouch* dispatch chain.

#include <cstdint>

namespace Mortar {
class InputDevice;

// Binary Mortar::Touch::State (28 bytes).
struct TouchState {
    int32_t  prevX;    // +0x00  previous frame x
    int32_t  prevY;    // +0x04  previous frame y
    int32_t  currX;    // +0x08
    int32_t  currY;    // +0x0c
    uint32_t extId;    // +0x10  external pointer id (from SDL FingerID / slot index)
    uint32_t touchId;  // +0x14  internal monotonic id (from nextTouchId)
    int32_t  phase;    // +0x18  -1=just-pressed, 0=held, 1=released/free
    // total 28 bytes
};

// Binary Mortar::Touch::TEvnt (20 bytes).
// Binary param 'b': true = press OR move, false = release.
// Port-specific: binary param named 'b'; port uses 'isActive' (was 'isMove', renamed for clarity).
struct TEvnt {
    uint32_t extId;       // a: external pointer id
    bool     isActive;    // b: true=press or move, false=release
    // 3 bytes implicit padding
    float    x;           // c
    float    y;           // d
    float    timestamp;   // e
};

class Touch {
public:
    static const int MAX_SLOTS = 8;
    static const int MAX_EVENTS = 10;  // binary backing buffer = 200B / 20B per TEvnt

    static Touch& GetInstance();

    Touch();

    // Binary @ 0x00195630 -- Update(float dt).
    // Drain events with timestamp <= dt (or all if dt == 0.0); then _Update().
    void Update(float dt);

    // Binary @ 0x001953ec -- _Update().
    // 8x: states1[i] = states2[i]; State::Update on the copy.
    void _Update();

    // Binary @ 0x001952f0 -- State::Update.
    // phase==1: zero extId+touchId. Else: snapshot prev=curr, promote phase==-1 to 0.
    static void StateUpdate(TouchState& s);

    // Binary @ 0x00195690 -- __UpdateInternal.
    // Push TEvnt to ring; on overflow: Update(0.0f) then retry.
    // SDL entry point: InputTranslatorSDL calls this for each touch event.
    // isActive: true=press OR move, false=release (matches binary param 'b').
    void __UpdateInternal(uint32_t extId, bool isActive, float x, float y, float t);

    // Binary @ 0x00195314 -- ___UpdateInternal.
    // Apply event to states2: match by extId or claim free slot (rotating cursor).
    // nextTouchId++ skipping 0 on wrap.
    // isActive: true=press OR move, false=release (matches binary param 'b').
    // Binary @ 0x00195314 -- free slot is extId==0, NOT phase>=1.
    void ___UpdateInternal(uint32_t extId, bool isActive, float x, float y);

    // Binary @ 0x00195424 -- FindTouch(uint touchId).
    // Linear scan states1; return slot index or -1.
    int FindTouch(uint32_t touchId) const;

    // Binary @ 0x001954fc -- GetAnyTouch().
    // First slot with phase < 1; returns touchId or 0.
    uint32_t GetAnyTouch();

    // Binary @ 0x0019551c -- GetMostRecentTouch().
    // FindTouch(nextTouchId - 1); returns touchId or 0.
    uint32_t GetMostRecentTouch();

    // Binary @ 0x0019543c -- GetTouchPos(uint touchId, int& x, int& y).
    // Writes currX/Y of matching slot. Returns 1 if active (phase < 1), 0 if not.
    // Binary leaves *x/*y UNTOUCHED on miss.
    int GetTouchPos(uint32_t touchId, int& x, int& y) const;

    // Binary @ 0x0019546c -- GetTouchDelta(uint touchId, int& dx, int& dy).
    // Writes currX-prevX/dy if phase >= 0, else 0. Returns 1 if active.
    int GetTouchDelta(uint32_t touchId, int& dx, int& dy) const;

    // Binary @ 0x001954b4 -- GetTouchInReigion (note binary typo).
    // Find first active touch inside (x, y, x+w, y+h). Returns touchId or 0.
    // Binary uses inclusive <= on all bounds.
    // Binary signature: (int x, int y, int w, int h).
    uint32_t GetTouchInReigion(int x, int y, int w, int h);

    // Binary @ 0x00195764 -- SendIndividualTouchCallbacks(InputDevice* dev).
    // 8x: emit AxisEvent for X/Y, ButtonPressed for press/held/release/up.
    // Action codes: 0x89+i (button), 0x99+i (X axis), 0xa9+i (Y axis), i in 0..7.
    // Port specific: InputDevice::AxisEvent / ButtonPressed not yet declared;
    //   body is a no-op stub until those virtual methods are ported into InputDevice.
    void SendIndividualTouchCallbacks(InputDevice* dev);

    // Port-specific: slot-indexed region scan (not in binary public API).
    // DIFFERS: binary GetTouchInReigion(x,y,w,h) returns touchId; this shim
    //          takes (left,right,bottom,top) and returns slot index for call
    //          sites in MenuButton/ScrollingMenu pending their touchId migration.
    //          Binary @ 0x001954b4.
    int GetTouchInRegion(float left, float right, float bottom, float top,
                         int preferredSlot = -1) const;

    // Note: binary has no Touch::Clear; symbol-diff false positive.
    // (Removed from port.)

    // Port-specific helpers (not in binary) -- used by SDL translator.
    // Route through __UpdateInternal for ring-buffer ordering.
    void OnPressed (uint32_t extId, float x, float y);
    void OnMoved   (uint32_t extId, float x, float y);
    void OnReleased(uint32_t extId);

    // Port-specific: direct slot read (not in binary public API).
    // Used by MenuButton/ScrollingMenu until those are ported to touchId model.
    // DIFFERS: binary uses GetTouchPos(touchId, x, y) not slot-indexed GetSlot.
    const TouchState* GetSlot(int slot) const;
    bool IsSlotDown(int slot) const;

    // Public fields matching binary layout.
    TouchState states1[MAX_SLOTS];   // +0x000 live polled state
    TouchState states2[MAX_SLOTS];   // +0x0e0 event-applied scratch

    // Ring buffer (Port specific: simpler ring; binary uses RingBufferT (Binary @ 0x001958fc init)).
    TEvnt    m_events[MAX_EVENTS];   // +0x1c0 area
    int      m_eventHead;            // ring head index
    int      m_eventTail;            // ring tail index

    uint32_t nextTouchId;            // +0x1d0 init 1

private:
    // Rotating cursor for ___UpdateInternal slot claim.
    // DIFFERS: binary stores cursor in BSS global (GOT+0x80798); port uses struct member.
    //          Behavior identical for singleton. Cosmetic.
    int m_slotCursor;

};

// ---------------------------------------------------------------------------
// Free functions matching binary helpers.

// TouchInRegion @ 0x001691cc
// Scans all slots for a touch inside [x0..x1] x [y0..y1].
// DIFFERS: binary GetTouchInReigion takes (x,y,w,h); this free function uses
//          port's (left,right,bottom,top) convention for existing call sites.
//          Binary @ 0x001954b4 / 0x001691cc.
int TouchInRegion(float x0, float x1, float y0, float y1, int hint_slot);

// IsTouchDown @ 0x00169144
// Returns 0=up, 1=just-pressed, 2=held for given slot.
// DIFFERS: takes slot index; binary equivalent is GetTouchPos(touchId,...).
int IsTouchDown(int slot);

} // namespace Mortar

#endif // MORTAR_TOUCH_H
