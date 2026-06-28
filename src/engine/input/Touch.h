#ifndef MORTAR_TOUCH_H
#define MORTAR_TOUCH_H

// Mortar::Touch -- binary @ 0x0019591c area.
// sizeof 0x1d4 (468 bytes):
//   +0x000: State states1[8]  (8 * 28 = 224B) -- live polled state
//   +0x0e0: State states2[8]  (8 * 28 = 224B) -- event-applied scratch
//   +0x1c0: RingBufferT<TEvnt,false,false> eventBuffer (16B)
//   +0x1d0: int32_t nextTouchId (init 1)
// Total: 448 + 16 + 4 = 468.
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
// Clarified 2026-05-18: 0x002772d4+0xa0 is a SEPARATE 16-slot table in BSS
// used ONLY by free helpers IsTouchDown @ 0x00169144 and TouchInRegion @
// 0x001691cc. Entries are {float x@+0xa0, y@+0xa4, phase@+0xa8}, stride 12,
// indexed 0..15. Phase: 1.0=held, 2.0=press-edge, <=0=up.
// The BSS table is written by a legacy Mortar input layer that has zero
// observable callers in this binary (Bada caps point ids at 8 per
// GlesForm::OnTouch* @ 0x0018334c). Mortar::Touch::states1 is the only live
// source, and port's IsTouchDown/TouchInRegion correctly read it. The
// 8-slot vs 16-slot cap is binary-faithful, not a port shortcut.

#include <cstdint>

namespace Mortar {
class InputDevice;

// Binary Mortar::Touch::State (28 bytes).
// ASM-spec v1.6.1 Touch::UpdateInternal @0x00242868: float touch coords stored directly (no truncation).
struct TouchState {
    float    prevX;    // +0x00  previous frame x (float: binary stores raw float from event)
    float    prevY;    // +0x04  previous frame y
    float    currX;    // +0x08
    float    currY;    // +0x0c
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

// Binary Mortar::RingBufferT<Mortar::Touch::TEvnt,false,false> (16 bytes).
// Layout per InitRingBuffer_Touch @ 0x001958fc:
//   +0x00: TEvnt* memory  (heap-allocated: operator new(200) = 10*20B backing)
//   +0x04: int    capacity (= 10)
//   +0x08: int    field_8  (binary = 1 after init; semantic TBD)
//   +0x0c: int    field_c  (binary = 0 after init; semantic TBD)
// DIFFERS: v1.6.1 binary @ 0x001958fc sets field_8=1, field_c=0 (internal ring state).
//   Port reuses field_8/field_c as m_eventHead/m_eventTail (0-based head/tail
//   indices) because the binary's RingBufferT ring-management code (Clear/push/pop)
//   is not yet ported. Struct fields are layout-identical to binary; init values
//   differ (port initializes both to 0 instead of 1/0).
//   Binary ground truth: Binary @ 0x001958fc (InitRingBuffer_Touch).
struct RingBufferT_TEvnt {
    TEvnt*  memory;       // +0x00  heap pointer (capacity * sizeof(TEvnt) bytes)
    int     capacity;     // +0x04  element capacity (= 10)
    int     m_eventHead;  // +0x08  DIFFERS: binary=1 after init; port uses 0 (ring read index)
    int     m_eventTail;  // +0x0c  DIFFERS: binary=0 after init; port uses 0 (ring write index)
};

class Touch {
public:
    static const int MAX_SLOTS = 8;
    static const int MAX_EVENTS = 10;  // binary backing buffer = 200B / 20B per TEvnt

    static Touch& GetInstance();

    Touch();
    ~Touch();

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

    // ASM-spec v1.6.1 Touch::GetTouchPos @0x002429d4: (uint, float&, float&).
    // Writes currX/Y of matching slot. Returns 1 if active (phase < 1), 0 if not.
    // Binary leaves *x/*y UNTOUCHED on miss.
    int GetTouchPos(uint32_t touchId, float& x, float& y) const;

    // ASM-spec v1.6.1 Touch::GetTouchDelta @0x00242a20: (uint, float&, float&).
    // Writes currX-prevX/dy if phase >= 0, else 0.0f. Returns 1 if active.
    int GetTouchDelta(uint32_t touchId, float& dx, float& dy) const;

    // Binary @ 0x00242a98 (v1.6.1) -- GetTouchInReigion (note binary typo).
    // Find first active touch inside (x, y, x+w, y+h). Returns touchId or 0.
    // Binary uses inclusive <= on all bounds.
    uint32_t GetTouchInReigion(float x, float y, float w, float h);

    // Binary @ 0x00242bc4 (v1.6.1) -- SendIndividualTouchCallbacks(InputDevice* dev).
    // Pointer-walks states1, emits AxisEvent/ButtonPressed per slot.
    // Action codes 0x89..0x90 (button), 0x99..0xa0 (X axis), 0xa9..0xb0 (Y axis).
    // Wired via InputDeviceBada::Update -> Touch::GetInstance().SendIndividualTouchCallbacks(this).
    void SendIndividualTouchCallbacks(InputDevice* dev);

    // Port-side Tier A region-scan helper. Implements binary's free function
    // TouchInRegion @ 0x001691cc (the slot-returning ABI used by every UI
    // widget: MenuButton, CheckBox, ScrollingMenu, SliderControl,
    // VerticalScroller, ComboBox). Returns slot index 0..7 (binary: 0..15)
    // or -1. Caller pairs the result with IsTouchDown(slot) for phase.
    //
    // NOT the binary's same-named member `GetTouchInReigion @ 0x001954b4`
    // (Tier B, touchId-returning) -- that API is DEAD in the binary (zero
    // internal callers, only an unused public-symbol export). Don't conflate.
    int GetTouchInRegion(float left, float right, float bottom, float top,
                         int preferredSlot = -1) const;

    // Binary @ 0x0019553c -- Mortar::Touch::Clear(). Real helper symbol, called by
    // InputDeviceBada::Reset @ 0x00195c00 (NOT inlined). Zeroes ONLY states2 (8 slots,
    // phase=1); leaves states1 and the ring buffer untouched.
    void Clear();

    // Port-specific helpers (not in binary) -- used by SDL translator.
    // Route through __UpdateInternal for ring-buffer ordering.
    void OnPressed (uint32_t extId, float x, float y);
    void OnMoved   (uint32_t extId, float x, float y);
    void OnReleased(uint32_t extId);

    // Port-side Tier A slot read helper. Returns the TouchState* by slot
    // index for callers that already track a slot (latched via the Tier A
    // GetTouchInRegion above). Binary Tier B equivalent is
    // GetTouchPos(touchId, &x, &y) @ 0x0019543c -- but Tier B is only used
    // by InputDeviceBada::Update; UI widgets use Tier A throughout.
    const TouchState* GetSlot(int slot) const;
    bool IsSlotDown(int slot) const;

    // Public fields matching binary layout.
    TouchState states1[MAX_SLOTS];    // +0x000  live polled state  (8*28=224B)
    TouchState states2[MAX_SLOTS];    // +0x0e0  event-applied scratch (8*28=224B)

    // Binary @ 0x001958fc — RingBufferT<TEvnt,false,false> (16B).
    // memory ptr is heap-allocated (200B = 10 * sizeof(TEvnt)).
    // eventBuffer.m_eventHead/m_eventTail serve as ring indices (see DIFFERS
    // on RingBufferT_TEvnt above); eventBuffer.memory is the heap block.
    RingBufferT_TEvnt eventBuffer;    // +0x1c0  (16B)

    int32_t nextTouchId;              // +0x1d0  init 1

    // Port-specific rotating cursor for ___UpdateInternal slot claim.
    // Binary stores this in a BSS global at GOT+0x80798; port uses a
    // file-static in Touch.cpp for identical behaviour (Touch is a singleton).
};

} // namespace Mortar

// ---------------------------------------------------------------------------
// Free functions matching binary helpers.

// TouchInRegion @ 0x001691cc -- Tier A slot-returning helper used by every
// UI widget (MenuButton, CheckBox, ScrollingMenu, SliderControl,
// VerticalScroller, ComboBox). Scans all slots for a touch inside the rect.
// Port uses (left, right, bottom, top) instead of binary's (x, y, w, h)
// because every call site already computes edges from pos +/- halfSize;
// numerically equivalent.
int TouchInRegion(float x0, float x1, float y0, float y1, int hint_slot);

// IsTouchDown @ 0x00169144 (asm-verified 2026-05-17)
// Returns 0=up, 1=held, 2=press-edge (just-pressed, one frame) for given slot.
// Matches binary signature verbatim (int slot -> int 0/1/2).
int IsTouchDown(int slot);

#if defined(__bada__)
static_assert(sizeof(Mortar::Touch) == 468, "Touch size mismatch");
#endif

#endif // MORTAR_TOUCH_H
