// OSD (on-screen message overlay).
//
// The binary's OSD is dead: both symbols are empty bx-lr stubs with zero
// call sites. The __bada__ branch below keeps those faithful stub bodies for
// the asm-verify cross-build; the host branch is a Port specific dev toast
// system (contract in OSD.h).

#include "OSD.h"

#ifdef __bada__

// ASM-spec v1.6.1 OSD_Init @0x1ca2b4: empty body (single bx lr).
void OSD_Init() {}

// ASM-spec v1.6.1 OSD_AddMessage @0x1ca2b8: identity — returns argument unchanged.
const char* OSD_AddMessage(const char* s) {
    return s;
}

#else // !__bada__

// Port specific: everything below is port-invented dev tooling (the binary
// OSD is a dead stub). Rendering uses the bitmap font "fonts/verdana.fnt"
// (a bundled game asset, present in the web build's preloaded Data package)
// through the same Mortar::Font::DrawString path as DebugHUDBounds_Draw.

#include "debug/DebugFlags.h"
#include "render/Renderer.h"
#include "render/Font.h"
#include "render/MatrixManager.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include <cstring>

// Port specific: fixed message store -- no heap churn, cross-build-safe
// plain loops (though this branch never cross-builds).
static const int   kMaxMsgs    = 24;
static const float kDefaultTtl = 2.5f;
static const float kTextScale  = 8.0f;   // verdana.fnt line height in world units

// Layout: DebugFps_Draw anchors its baseline at (-235, +138) with glyph tops
// near +148 (top edge +160, ~22 margin). The first toast anchor sits at +120
// -- one clear line below the FPS counter so the two never overlap -- and each
// older message steps 10 units further down (size-8 line height + gap).
// 16 slots span +120 .. -30, well inside the -160 bottom edge.
static const float kAnchorX  = -235.0f;
static const float kAnchorY0 =  120.0f;
static const float kLineStep =   10.0f;
static const float kZ        =   -0.1f;   // in front of game content, same as FPS overlay

// Flat yellow, full opacity so toasts read as distinct from the
// FPS counter without fully covering the gameplay behind them.
static const Colour kTextColour(255, 255, 0, 255);

struct OSDMsg {
    char  text[64];
    float ttl;                       // remaining lifetime in seconds
};

// s_Msgs[0] = newest; active slots are 0..s_MsgCount-1.
static OSDMsg s_Msgs[kMaxMsgs];
static int    s_MsgCount = 0;

// Lazy bitmap font (fonts/verdana.fnt) -- bundled asset, loads on desktop
// AND web (the whole FruitNinjaBada/Data dir is emscripten-preloaded).
static Mortar::SmartPtr<Mortar::Font> s_OsdFont;

// Port specific: clear the store (host build repurposes the binary's dead
// init symbol as the toast-store reset).
void OSD_Init() {
    for (int i = 0; i < s_MsgCount; ++i) {
        s_Msgs[i].text[0] = '\0';
        s_Msgs[i].ttl     = 0.0f;
    }
    s_MsgCount = 0;
}

// Port specific: post a toast; evict the oldest when full.
void OSD_AddMessage(const char* s, float ttl) {
    if (!s || !s[0]) return;

    if (s_MsgCount == kMaxMsgs) {
        --s_MsgCount;
    }

    // Shift existing messages down one slot (newest lives at [0]).
    for (int i = s_MsgCount; i > 0; --i) {
        s_Msgs[i] = s_Msgs[i - 1];
    }

    OSDMsg& m = s_Msgs[0];
    strncpy(m.text, s, sizeof(m.text) - 1);
    m.text[sizeof(m.text) - 1] = '\0';
    m.ttl = ttl;
    ++s_MsgCount;
}

// Port specific: append to the newest toast in place (contract in OSD.h).
void OSD_AppendToLast(const char* s) {
    if (!s || !s[0] || s_MsgCount == 0) return;

    OSDMsg& m = s_Msgs[0];
    size_t used = strlen(m.text);
    if (used + 1 >= sizeof(m.text)) return;   // no room left
    strncpy(m.text + used, s, sizeof(m.text) - used - 1);
    m.text[sizeof(m.text) - 1] = '\0';
}

// Port specific: binary-stub-compatible form -- default lifetime, returns
// the argument unchanged like the binary's identity stub did.
const char* OSD_AddMessage(const char* s) {
    OSD_AddMessage(s, kDefaultTtl);
    return s;
}

// Port specific: age + compact. Expired slots drop and the survivors slide
// up so 0..s_MsgCount-1 stays dense (newest first).
void OSD_Update(float dt) {
    int dst = 0;
    for (int src = 0; src < s_MsgCount; ++src) {
        s_Msgs[src].ttl -= dt;
        if (s_Msgs[src].ttl <= 0.0f) continue;
        if (dst != src) {
            s_Msgs[dst] = s_Msgs[src];
        }
        ++dst;
    }
    s_MsgCount = dst;
}

// Port specific: draw the toast stack through the same Font::DrawString
// path as DebugHUDBounds_Draw (scene ortho + identity world matrix).
void OSD_Draw() {
    if (s_MsgCount == 0) return;

    if (!s_OsdFont.IsValid()) {
        s_OsdFont = Mortar::Font::Create("fonts/verdana.fnt");
        if (!s_OsdFont.IsValid()) return;
    }

    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    // Restore game ortho so the coordinates match game space (same call and
    // convention as DebugFps_Draw: X -240..+240, Y -160..+160), then reset
    // the world matrix -- DrawString multiplies the current modelview.
    r->SetupGameOrtho();
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    // Suppress the level-3 font-debug overlay for our own debug text, same
    // as DebugHUDBounds_Draw does around its labels.
    FN::g_SuppressTextOverlay = true;
    for (int i = 0; i < s_MsgCount; ++i) {
        const _Vector3<float> anchor(kAnchorX, kAnchorY0 - kLineStep * (float)i, kZ);
        s_OsdFont->DrawString(kTextScale, 1.0f, 0.0f,
                              s_Msgs[i].text, anchor, kTextColour,
                              Mortar::FONT_ALIGN_LEFT);
    }
    FN::g_SuppressTextOverlay = false;
}

#endif // !__bada__
