//
// Mortar::Touch — poll-based 8-slot touch state, matches binary 0x001958fc
// area. See Touch.h for architecture notes.
//
// Analysed: 2026-04-13T19:30
//

#include "input/Touch.h"
#include <cstring>
#include <cstdio>

// Touch slot logging — flip to 0 to silence after diagnosing crashes.
#define TOUCH_SLOT_LOG 0
#if TOUCH_SLOT_LOG
#  define SLOG(...) do { printf("[TSLOT] " __VA_ARGS__); fflush(stdout); } while (0)
#else
#  define SLOG(...) do {} while (0)
#endif

namespace Mortar {

Touch& Touch::GetInstance() {
    static Touch instance;
    return instance;
}

Touch::Touch() : m_NextTouchId(1) {
    memset(states1, 0, sizeof(states1));
    for (int i = 0; i < MAX_SLOTS; i++) {
        states1[i].phase = 1;       // inactive
        states1[i].touchId = 0;
    }
}

// Matches binary SendIndividualTouchCallbacks edge transitions:
//   phase -1 (just pressed)  → 0 (held) after one frame
// Release transitions happen immediately in OnReleased; phase >= 1 stays.
void Touch::Update() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (states1[i].phase == -1) {
            SLOG("Update: slot %d phase -1 → 0 at (%d,%d)\n",
                 i, states1[i].currX, states1[i].currY);
            states1[i].phase = 0;
        }
    }
}

void Touch::OnPressed(int slot, float x, float y) {
    if (slot < 0 || slot >= MAX_SLOTS) {
        SLOG("OnPressed: REJECTED slot=%d out of range\n", slot);
        return;
    }
    TouchState& s = states1[slot];
    SLOG("OnPressed: slot %d (%.0f,%.0f) prev_phase=%d → -1 id=%d\n",
         slot, x, y, s.phase, m_NextTouchId);
    s.startX = (int)x;
    s.startY = (int)y;
    s.currX  = (int)x;
    s.currY  = (int)y;
    s.phase  = -1;          // just-pressed (transitions to 0 next Update)
    s.touchId = m_NextTouchId++;
    s.field10 = 0;
}

void Touch::OnMoved(int slot, float x, float y) {
    if (slot < 0 || slot >= MAX_SLOTS) {
        SLOG("OnMoved: REJECTED slot=%d out of range\n", slot);
        return;
    }
    TouchState& s = states1[slot];
    // Only accept movement while the slot is active (phase <= 0). Stray
    // moves after release are ignored.
    if (s.phase > 0) {
        SLOG("OnMoved: slot %d ignored (phase=%d, slot inactive)\n", slot, s.phase);
        return;
    }
    SLOG("OnMoved: slot %d (%.0f,%.0f) phase=%d\n", slot, x, y, s.phase);
    s.currX = (int)x;
    s.currY = (int)y;
}

void Touch::OnReleased(int slot) {
    if (slot < 0 || slot >= MAX_SLOTS) {
        SLOG("OnReleased: REJECTED slot=%d out of range\n", slot);
        return;
    }
    TouchState& s = states1[slot];
    SLOG("OnReleased: slot %d at (%d,%d) prev_phase=%d → 1\n",
         slot, s.currX, s.currY, s.phase);
    s.phase = 1;            // released / inactive
}

// Matches binary Mortar::Touch::GetTouchInReigion (0x001954b4).
// Iterates 8 slots, checks phase < 1 (active) AND position inside rect.
// Binary used (left, top, right, bottom) as "left, top, width, height";
// our signature is explicit bounds so callers don't have to add.
int Touch::GetTouchInRegion(float left, float right, float bottom, float top,
                            int preferredSlot) const {
    // Fast-path: preferred slot first if it's valid and still in range.
    // Matches the binary's two-phase search (preferred → full scan).
    if (preferredSlot >= 0 && preferredSlot < MAX_SLOTS) {
        const TouchState& s = states1[preferredSlot];
        if (s.phase < 1) {
            const float x = (float)s.currX;
            const float y = (float)s.currY;
            if (left <= x && x <= right && bottom <= y && y <= top) {
                return preferredSlot;
            }
        }
    }
    for (int i = 0; i < MAX_SLOTS; i++) {
        const TouchState& s = states1[i];
        if (s.phase >= 1) continue;
        const float x = (float)s.currX;
        const float y = (float)s.currY;
        if (left <= x && x <= right && bottom <= y && y <= top) {
            return i;
        }
    }
    return -1;
}

const TouchState* Touch::GetSlot(int slot) const {
    if (slot < 0 || slot >= MAX_SLOTS) return nullptr;
    return &states1[slot];
}

bool Touch::IsSlotDown(int slot) const {
    if (slot < 0 || slot >= MAX_SLOTS) return false;
    return states1[slot].phase <= 0;
}

// ---------------------------------------------------------------------------
// TouchInRegion @ 0x001691cc
// Scans the touch slots for a finger inside [x0..x1] x [y0..y1].
// Hint slot is tried first (fast-path for already-tracked fingers).
// Returns -1 if no active touch is in the region.
// Binary: 16 slots, 12-byte stride, state>0 = active. Port: 8 slots, phase<=0 = active.
int TouchInRegion(float x0, float x1, float y0, float y1, int hint_slot) {
    return Touch::GetInstance().GetTouchInRegion(x0, x1, y0, y1, hint_slot);
}

// IsTouchDown @ 0x00169144
// Returns touch state for the given slot:
//   0 = not pressed (up/released)
//   1 = just pressed this frame (port: phase == -1)
//   2 = held / moving (port: phase == 0)
// Binary: float state field (0.0=up, 1.0=just-down, 2.0=held). Port: int phase.
int IsTouchDown(int slot) {
    const Touch& t = Touch::GetInstance();
    if (slot < 0 || slot >= Touch::MAX_SLOTS) return 0;
    int ph = t.states1[slot].phase;
    if (ph >= 1)  return 0;  // released / inactive
    if (ph == -1) return 1;  // just pressed
    return 2;                // held (phase == 0)
}

} // namespace Mortar
