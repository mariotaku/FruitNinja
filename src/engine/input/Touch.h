#ifndef MORTAR_TOUCH_H
#define MORTAR_TOUCH_H

//
// Mortar::Touch — binary-accurate poll-based touch state.
// Matches Mortar::Touch ctor at 0x0019591c / GetTouchInReigion at 0x001954b4.
//
// Layout: 8 × 28-byte TouchState slots (states1 at +0x00), plus a second
// states2 buffer (+0xe0) and a ring buffer of pending TEvnt events (+0x1c0).
// The port drops the states2 swap buffer for simplicity — all writes target
// states1 directly. The ring buffer is collapsed into Push* methods that
// update slots immediately.
//
// Phase semantics (matching binary states1[i].field12_0x18):
//   -1 : just-pressed (one frame)
//    0 : held
//   >=1: released / inactive
//
// SDL event flow: SDLInputTranslator::ProcessSDLEvent → OnPressed / OnMoved /
// OnReleased → slot writes. Once per frame GameUpdate calls Touch::Update()
// which advances phase transitions (-1 → 0).
//
// Polling consumers: MenuButton::Update, SlashEntity::Update read state via
// GetTouchInRegion / GetSlot.
//

#include <cstdint>

namespace Mortar {

// Matches binary Mortar::Touch::State (28 bytes, states1/states2 elements).
// Field names follow the port's conventions; binary offsets in comments.
struct TouchState {
    int   startX;    // +0x00  x at press (or prev frame x)
    int   startY;    // +0x04
    int   currX;     // +0x08  field8_0x8 — current x
    int   currY;     // +0x0c  field9_0xc — current y
    int   field10;   // +0x10  unused flag
    int   touchId;   // +0x14  field11_0x14 — monotonic id (from pointerId)
    int   phase;     // +0x18  field12_0x18 — -1, 0, or 1+
    // total 28 bytes matching binary State::State ctor
};

class Touch {
public:
    static const int MAX_SLOTS = 8;

    static Touch& GetInstance();

    TouchState states1[MAX_SLOTS];
    int m_NextTouchId;

    Touch();

    // Per-frame: advances phase transitions (-1 → 0). Call once from
    // GameUpdate before any polling consumers read the slot state.
    void Update();

    // SDL-side entry points. `slot` is a stable 0..7 channel index mapped
    // from SDL_FingerID by SDLInputTranslator (mouse emulates slot 0).
    // Coordinates are in the binary-centred ortho space [-240..240, -160..160].
    void OnPressed (int slot, float x, float y);
    void OnMoved   (int slot, float x, float y);
    void OnReleased(int slot);

    // Matches Mortar::Touch::GetTouchInReigion (0x001954b4, note binary typo).
    // Iterates the 8 slots, returns the index of the first active touch
    // inside the rect, or -1 if none found.
    // `preferredSlot` is ignored in the port (binary uses it as a fast-path
    // hint when a button is already tracking a specific slot).
    int GetTouchInRegion(float left, float right, float bottom, float top,
                         int preferredSlot = -1) const;

    // Direct slot accessor. Returns nullptr if slot is out of range.
    const TouchState* GetSlot(int slot) const;

    // Convenience: true if the slot is pressed or held (phase <= 0).
    bool IsSlotDown(int slot) const;
};

// ---------------------------------------------------------------------------
// Free functions matching binary helpers used by ScrollingMenu::Update.
// These wrap Mortar::Touch::GetInstance() to provide the binary-accurate API.
//
// TouchInRegion @ 0x001691cc
//   Scans all slots for a touch inside [x0..x1] x [y0..y1].
//   If hint_slot is a valid held slot already in the rect, returns it first.
//   Returns -1 if no match found.
//   Binary uses float x/y from the 16-slot touch table; port uses Mortar::Touch
//   with 8 slots (same semantics, fewer slots).
int TouchInRegion(float x0, float x1, float y0, float y1, int hint_slot);

// IsTouchDown @ 0x00169144
//   Returns the touch state for the given slot:
//     0  = slot not pressed (up / released)
//     1  = just pressed this frame (phase == -1 in port)
//     2  = held / moving (phase == 0 in port)
//   Returns 0 for invalid slot.
//   Binary semantics: acquire fires when IsTouchDown == 2 (held).
int IsTouchDown(int slot);

} // namespace Mortar

#endif // MORTAR_TOUCH_H
