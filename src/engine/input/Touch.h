#ifndef MORTAR_TOUCH_H
#define MORTAR_TOUCH_H

// Mortar::Touch -- v1.6.1 Mortar::Touch::Touch @0x00242e24 (dtor @0x00243698).
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
// The 16-slot table the free helpers IsTouchDown @0x001ca69c and TouchInRegion
// @0x001ca754 read is game_work.m_FingerSpawnPos (GameWork +0xa4, stride 12:
// x@+0xa4, y@+0xa8, z@+0xac). There is no separate BSS table -- IsTouchDown does
// `vldr.32 s15,[r0,#0xac]` and TouchInRegion gates on [r2,#0xac] then reads
// [r2,#0xa4]/[r2,#0xa8]. z: 2.0=press-edge, 1.0=held, <=0=up.
// Its writers are the per-finger callbacks in GameTaskInput.cpp
// (TouchDownCallback @0x001cbf18 stamps z, PointerMoveCallback @0x001cbfcc
// stores x/y).
// The port's IsTouchDown/TouchInRegion instead read Mortar::Touch::states1,
// which carries the same phase information at 8 slots rather than 16. Bada caps
// point ids at 8 anyway (GlesForm::OnTouch* @ 0x0018334c), so the 8-slot cap is
// binary-faithful, not a port shortcut.

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
    // total 28 bytes (__bada__)
#if !defined(__bada__)
    float liveX, liveY;   // Port specific: task #13 -- per-present live finger pos (0 bytes under __bada__)
#endif
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
// Layout per v1.6.1 Mortar::RingBufferT<Touch::TEvnt,false,false>::Init(int) @0x002436b0:
//   +0x00: TEvnt* memory  (heap-allocated: operator new[](n * 0x14))
//   +0x04: int    capacity (= n, the Init CALLER's argument -- not a literal;
//                  Touch's ctor passes 10, hence the 200-byte allocation)
//   +0x08: int    write index (binary = 1 after Init)
//   +0x0c: int    read  index (binary = 0 after Init)
//
// The rest of the ring IS present in the binary:
//   Push @0x0024356c, Pop @0x00243610, Peek @0x002435d8, Clear @0x00243674.
// Its full-condition is `write == read`, which is why Init seeds write=1/read=0.
//
// DIFFERS: original = write index 1 / read index 0 after Init
//   (v1.6.1 Mortar::RingBufferT<Touch::TEvnt,false,false>::Init @0x002436b0), using
//   head=tail=0 because the port implements its own drain in Touch::Update /
//   Touch::__UpdateInternal rather than calling the binary's Push/Pop.
//   CONSEQUENCE, do not "simplify" this away: under the binary's own Push,
//   write==read means FULL, so a 0/0 seed makes the ring read as permanently
//   full and Push would drop EVERY event. It is harmless only because the port
//   never routes through that push path. Anyone porting Push/Pop/Peek/Clear
//   MUST restore write=1/read=0 here first.
struct RingBufferT_TEvnt {
    TEvnt*  memory;       // +0x00  heap pointer (capacity * sizeof(TEvnt) bytes)
    int     capacity;     // +0x04  element capacity (Init arg; 10 for Touch)
    int     m_eventHead;  // +0x08  binary = WRITE index (init 1); port uses it as read head
    int     m_eventTail;  // +0x0c  binary = READ index (init 0); port uses it as write tail
};

class Touch {
public:
    static const int MAX_SLOTS = 8;
    static const int MAX_EVENTS = 10;  // binary backing buffer = 200B / 20B per TEvnt

    static Touch& GetInstance();

    Touch();
    ~Touch();

    // v1.6.1 Mortar::Touch::Update @0x00242d14 -- Update(float dt).
    // Drain events with timestamp <= dt (or all if dt == 0.0); then _Update().
    void Update(float dt);

    // v1.6.1 Mortar::Touch::_Update @0x00242958 -- _Update().
    // 8x: states1[i] = states2[i]; State::Update on the copy.
    void _Update();

    // v1.6.1 Mortar::Touch::State::Update @0x00242830.
    // phase==1: zero extId+touchId. Else: snapshot prev=curr, promote phase==-1 to 0.
    static void StateUpdate(TouchState& s);

    // v1.6.1 Mortar::Touch::__UpdateInternal @0x00242d98.
    // Push TEvnt to ring; on overflow: Update(0.0f) then retry.
    // SDL entry point: InputTranslatorSDL calls this for each touch event.
    // isActive: true=press OR move, false=release (matches binary param 'b').
    void __UpdateInternal(unsigned long extId, bool isActive, float x, float y, float t);

    // v1.6.1 Mortar::Touch::___UpdateInternal @0x00242868.
    // Apply event to states2: match by extId or claim free slot (rotating cursor).
    // nextTouchId++ skipping 0 on wrap.
    // isActive: true=press OR move, false=release (matches binary param 'b').
    // Free slot predicate is extId==0, NOT phase>=1.
    void ___UpdateInternal(unsigned long extId, bool isActive, float x, float y);

    // v1.6.1 Mortar::Touch::FindTouch @0x002429a8 -- FindTouch(uint touchId).
    // Linear scan states1; return slot index or -1.
    int FindTouch(unsigned long touchId);

    // v1.6.1 Mortar::Touch::GetAnyTouch @0x00242b24.
    // First slot with phase < 1; returns touchId or 0.
    uint32_t GetAnyTouch();

    // v1.6.1 Mortar::Touch::GetMostRecentTouch @0x00242b60.
    // FindTouch(nextTouchId - 1); returns touchId or 0.
    uint32_t GetMostRecentTouch();

    // ASM-spec v1.6.1 Touch::GetTouchPos @0x002429d4: (uint, float&, float&).
    // Writes currX/Y of matching slot. Returns 1 if active (phase < 1), 0 if not.
    // Binary leaves *x/*y UNTOUCHED on miss.
    int GetTouchPos(unsigned long touchId, float& x, float& y);

    // ASM-spec v1.6.1 Touch::GetTouchDelta @0x00242a20: (uint, float&, float&).
    // Writes currX-prevX/dy if phase >= 0, else 0.0f. Returns 1 if active.
    int GetTouchDelta(unsigned long touchId, float& dx, float& dy);

    // v1.6.1 Mortar::Touch::GetTouchInReigion @0x00242a98 (note binary typo).
    // Find first active touch inside (x, y, x+w, y+h). Returns touchId or 0.
    // Binary uses inclusive <= on all bounds.
    uint32_t GetTouchInReigion(float x, float y, float w, float h);

    // v1.6.1 Mortar::Touch::SendIndividualTouchCallbacks @0x00242bc4 (InputDevice* dev).
    // Pointer-walks states1, emits AxisEvent/ButtonPressed per slot.
    // Action codes 0x89..0x90 (button), 0x99..0xa0 (X axis), 0xa9..0xb0 (Y axis).
    // Wired via InputDeviceBada::Update -> Touch::GetInstance().SendIndividualTouchCallbacks(this).
    void SendIndividualTouchCallbacks(InputDevice* dev);

    // Port-side Tier A region-scan helper. Implements binary's free function
    // TouchInRegion @0x001ca754 (the slot-returning ABI used by every UI
    // widget: MenuButton, CheckBox, ScrollingMenu, SliderControl,
    // VerticalScroller, ComboBox). Returns slot index 0..7 (binary: 0..15)
    // or -1. Caller pairs the result with IsTouchDown(slot) for phase.
    //
    // NOT the binary's same-named member `GetTouchInReigion @0x00242a98`
    // (Tier B, touchId-returning) -- that API is DEAD in the binary (zero
    // internal callers, only an unused public-symbol export). Don't conflate.
    //
    // Port specific: skips hover-blade slots, same as the free TouchInRegion
    // below -- see its doc for why.
    int GetTouchInRegion(float left, float right, float bottom, float top,
                         int preferredSlot = -1) const;

    // v1.6.1 Mortar::Touch::Clear @0x00242b88. Real helper symbol, called by
    // InputDeviceBada::Reset (NOT inlined). Zeroes ONLY states2 (8 slots,
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
    // GetTouchPos(touchId, &x, &y) @0x002429d4 -- but Tier B is only used
    // by InputDeviceBada::Update; UI widgets use Tier A throughout.
    const TouchState* GetSlot(int slot) const;
    bool IsSlotDown(int slot) const;

#ifndef __bada__
    // Port specific: task #13 -- per-present (native-refresh-rate) finger
    // tracking for UI SCROLL only. Touch/EDGE dispatch stays on the 60Hz sim
    // tick (Touch::Update / DispatchForSimTick); this refreshes a SEPARATE
    // liveX/liveY per active slot from the ring buffer WITHOUT draining it,
    // so slicing (which reads InputEvent/m_RawTouchPos, never this) and the
    // sim-tick dispatch (which drains the ring via Touch::Update) are both
    // unaffected. Called once per PRESENTED frame from Game::tickRealtimeUi.
    // See Touch.cpp for the ring-scan algorithm.
    void RefreshLivePos();

    // Port specific: task #13. Returns the current live position for `slot`
    // (liveX/liveY, refreshed by RefreshLivePos every present) and whether
    // the slot is active (phase < 1, same predicate as IsSlotDown). Falls
    // back to currX/currY (baked into liveX/liveY by RefreshLivePos) when no
    // newer ring sample exists, so callers get sim-tick-fresh data on
    // presents where no new touch event arrived.
    bool GetLivePos(int slot, float& x, float& y) const;
#endif

    // Public fields matching binary layout.
    TouchState states1[MAX_SLOTS];    // +0x000  live polled state  (8*28=224B)
    TouchState states2[MAX_SLOTS];    // +0x0e0  event-applied scratch (8*28=224B)

    // v1.6.1 Mortar::RingBufferT<Touch::TEvnt,false,false>::Init @0x002436b0 (16B).
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

// v1.6.1 ::TouchInRegion @0x001ca754 -- Tier A slot-returning helper used by every
// UI widget (MenuButton, CheckBox, ScrollingMenu, SliderControl,
// VerticalScroller, ComboBox). Scans all slots for a touch inside the rect.
// Port uses (left, right, bottom, top) instead of binary's (x, y, w, h)
// because every call site already computes edges from pos +/- halfSize;
// numerically equivalent.
//
// Port specific: slots driven by a HOVER BLADE channel
// (FN::HOVER_BLADE_CHANNEL_FIRST..LAST, debug/DebugFlags.h) are SKIPPED. Motion
// mode holds its blade channel pressed for as long as the cursor is on screen,
// which is not a click and must never latch a widget. Blades are unaffected --
// they reach SlashEntity through the per-finger Touch<n> action callbacks, not
// through this helper.
int TouchInRegion(float x0, float x1, float y0, float y1, int hint_slot);

// v1.6.1 ::IsTouchDown @0x001ca69c (asm-verified 2026-05-17)
// Returns 0=up, 1=held, 2=press-edge (just-pressed, one frame) for given slot.
// Matches binary signature verbatim (int slot -> int 0/1/2).
// Port specific: returns 0 for a hover-blade slot -- same rationale as
// TouchInRegion above.
int IsTouchDown(int slot);

#if defined(__bada__)
static_assert(sizeof(Mortar::Touch) == 468, "Touch size mismatch");
#endif

#endif // MORTAR_TOUCH_H
