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
// OSD is a dead stub). Rendering reuses the exact DebugFps_Draw text path:
// same lazily-created TTF font cache (FN::DebugFontTTF_Get), the same
// BakedStringTTF primitive, and the same Renderer::SetupGameOrtho projection.

#include "debug/DebugFlags.h"
#include "render/Renderer.h"
#include "render/BakedStringTTF.h"
#include "render/FontCacheObjectTTF.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include <cstring>

// Port specific: fixed message store -- no heap churn, cross-build-safe
// plain loops (though this branch never cross-builds).
static const int   kMaxMsgs    = 6;
static const float kDefaultTtl = 2.5f;
static const float kTextSize   = 12.0f;

// Layout: DebugFps_Draw anchors its baseline at (-235, +138) with glyph tops
// near +148 (top edge +160, ~22 margin). The first toast baseline sits at +120
// -- one clear line below the FPS counter so the two never overlap -- and each
// older message steps 16 units further down (size-12 line height + gap).
static const float kAnchorX  = -235.0f;
static const float kAnchorY0 =  120.0f;
static const float kLineStep =   16.0f;
static const float kZ        =   -0.1f;   // in front of game content, same as FPS overlay

struct OSDMsg {
    char  text[64];
    float ttl;                       // remaining lifetime in seconds
    Mortar::BakedStringTTF* baked;   // lazily built in OSD_Draw; owned by the slot
};

// s_Msgs[0] = newest; active slots are 0..s_MsgCount-1.
static OSDMsg s_Msgs[kMaxMsgs];
static int    s_MsgCount = 0;

static void FreeSlot(OSDMsg& m) {
    delete m.baked;
    m.baked   = 0;
    m.text[0] = '\0';
    m.ttl     = 0.0f;
}

// Port specific: clear the store (host build repurposes the binary's dead
// init symbol as the toast-store reset).
void OSD_Init() {
    for (int i = 0; i < s_MsgCount; ++i) {
        FreeSlot(s_Msgs[i]);
    }
    s_MsgCount = 0;
}

// Port specific: post a toast; evict the oldest when full.
void OSD_AddMessage(const char* s, float ttl) {
    if (!s || !s[0]) return;

    if (s_MsgCount == kMaxMsgs) {
        FreeSlot(s_Msgs[kMaxMsgs - 1]);
        --s_MsgCount;
    }

    // Shift existing messages down one slot (newest lives at [0]). The baked
    // pointer moves with its slot; each pointer value ends up in exactly one
    // active slot, so ownership stays single.
    for (int i = s_MsgCount; i > 0; --i) {
        s_Msgs[i] = s_Msgs[i - 1];
    }

    OSDMsg& m = s_Msgs[0];
    strncpy(m.text, s, sizeof(m.text) - 1);
    m.text[sizeof(m.text) - 1] = '\0';
    m.ttl   = ttl;
    m.baked = 0;
    ++s_MsgCount;
}

// Port specific: binary-stub-compatible form -- default lifetime, returns
// the argument unchanged like the binary's identity stub did.
const char* OSD_AddMessage(const char* s) {
    OSD_AddMessage(s, kDefaultTtl);
    return s;
}

// Port specific: age + compact. Expired slots are freed and the survivors
// slide up so 0..s_MsgCount-1 stays dense (newest first).
void OSD_Update(float dt) {
    int dst = 0;
    for (int src = 0; src < s_MsgCount; ++src) {
        s_Msgs[src].ttl -= dt;
        if (s_Msgs[src].ttl <= 0.0f) {
            FreeSlot(s_Msgs[src]);
            continue;
        }
        if (dst != src) {
            s_Msgs[dst] = s_Msgs[src];
            s_Msgs[src].baked = 0;   // ownership moved to dst
        }
        ++dst;
    }
    s_MsgCount = dst;
}

// Port specific: draw the toast stack through the DebugFps_Draw text path.
void OSD_Draw() {
    if (s_MsgCount == 0) return;

    Mortar::FontCacheObjectTTF* fc = FN::DebugFontTTF_Get();
    if (!fc) return;

    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    // Restore game ortho so the coordinates match game space (same call and
    // convention as DebugFps_Draw: X -240..+240, Y -160..+160).
    r->SetupGameOrtho();

    for (int i = 0; i < s_MsgCount; ++i) {
        OSDMsg& m = s_Msgs[i];
        if (!m.baked) {
            // Flat yellow, size 12, no effects -- same primitive/params as the
            // FPS counter but tinted so toasts read as distinct from "FPS NNN".
            // (No ApplyGradient_TopBottom -- see the gradient-bug note in
            // DebugFps_Draw.)
            m.baked = new Mortar::BakedStringTTF(
                fc, m.text, kTextSize,
                Colour(255, 255, 0, 255),
                0L, 0.0f,
                Mortar::FontCacheObjectTTF::FONT_EFFECT_NONE);
        }
        // align 0x4: hAlign left (text extends rightward), vAlign top-baseline
        // -- identical to the FPS counter's anchor semantics.
        const Vec3 anchor(kAnchorX, kAnchorY0 - kLineStep * (float)i, kZ);
        m.baked->Draw(anchor, Vec2(1.0f, 1.0f), 0.0f, (Mortar::ALIGNMENT_TYPE)0x4);
    }
}

#endif // !__bada__
