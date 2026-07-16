#include "Layout.h"
#include <cstddef>   // NULL
#include <cstring>   // strcmp

// DIFFERS: opt-in widescreen (Layout::HalfWidth); faithful 240 under __bada__
// Real (non-bada) build only -- under __bada__ the header macro-shortcuts
// MapX/HalfWidth away entirely, so this whole TU is inert there.
#ifndef __bada__

namespace Layout {

namespace {
bool g_WideLayout = false;      // ACTIVE (live, boot-latched) value
bool g_WideLayoutPref = false;  // PREF (user's saved choice, editable live)
float g_RawWindowAspect = 1.5f;

// Last-applied viewport rect + the window size it was computed from.
// Defaults describe a not-yet-rendered frame's full window; TouchToGame
// is only meaningful after the first ComputeViewport/SetActiveViewport
// call each frame (Game::renderFrame runs before input is ever drained).
int g_ViewportX = 0, g_ViewportY = 0;
int g_ViewportW = 0, g_ViewportH = 0;
int g_ViewportWinW = 0, g_ViewportWinH = 0;

// Pass 2 per-key overrides. Two element classes:
//  - Proportional (no entry): x * (HalfWidth()/240) -- scales the offset from
//    screen center. Right for near-center / spread elements.
//  - Edge-anchored (table entry below): preserve the element's pixel distance
//    from its NEAREST screen edge. Translate by sign(x)*(HalfWidth()-240),
//    i.e. shift by exactly how far that edge moved when the field widened.
//    An element sitting 30px in from the right edge at 3:2 stays 30px in from
//    the (wider) right edge -- edges don't stretch, they just move out.
//    Right for edge-pinned deco/logos. Identity at HalfWidth==240 (non-wide
//    / __bada__), so listing a key here is safe when widescreen is off.
struct KeyOverride {
    const char* key;    // presence in this table == edge-anchored
    float rightPad;     // widescreen-only extra gap from the anchored edge, in
                        // design units: pulls the element toward centre by this
                        // much (0 = pure edge-anchor). Used to give the back/quit
                        // bombs a little breathing room off the widened edge.
};
// Slight padding so the back/quit bombs don't sit flush against the widened
// edge -- tunable.
static const float BACK_BOMB_RIGHT_PAD = 20.0f;
const KeyOverride kOverrides[] = {
    { "about.sensei"      },   // sensei.tex on AboutScreen (right-anchored)
    { "deco.smltitle"     },   // sml_title.tex on Dojo/GameMode/About (BaseScreen::DrawBorders)
    { "dojo.sensei"       },   // dojo_sensei.tex on DojoScreen (bottom-left-anchored)
    { "dojo.border"       },   // DrawBorders title anchor on DojoScreen (same corner as dojo.sensei)
    { "modeselect.sensei" },   // mode_sensei.tex on GameModeScreen (bottom-left-anchored)
    { "modeselect.title"  },   // DrawBorders anchor on GameModeScreen -- feeds m_pDescBox "MODE SELECT" (left-anchored)
    { "modeselect.plate"  },   // zen_sign.tex lerp endpoints on GameModeScreen -- wooden mode-description plate (right-anchored)
    // Dojo/mode-select BACK buttons (the red bomb; pos=(0,0,0) + m_HudScale.x=0.375
    // = true x=180, right edge). Back/quit buttons edge-anchor universally.
    { "dojo.btn.back",       BACK_BOMB_RIGHT_PAD },
    { "about.btn.back",      BACK_BOMB_RIGHT_PAD },   // AboutScreen back (same m_HudScale idiom)
    { "shop.btn.back",       BACK_BOMB_RIGHT_PAD },   // ShopScreen m_pBuyButton is actually the back button
    { "modeselect.btn.back", BACK_BOMB_RIGHT_PAD },
    // NOTE: the dojo/mode-select CONTENT ring buttons (dojo.btn.shop/about,
    // modeselect.btn.classic/zen/arcade) are deliberately NOT here -- they use
    // PROPORTIONAL spread so the rings stay evenly distributed across the wider
    // field, same as the MainScreen play/dojo rings (edge-anchor spread them
    // unevenly).
    // Social share buttons (Facebook/Twitter, defunct-but-drawn on DojoScreen) --
    // right-edge-anchored so they hug the widened edge like the ring buttons above.
    { "social.facebook"      },
    { "social.twitter"       },
    // SettingsScreen's close/back button -- sits at the screen's bottom-right
    // corner (kCloseBtnX=215, outside the centred modal plate), right-edge-
    // anchored so it hugs the widened corner rather than drifting proportionally.
    { "settings.back"        },
    // MainScreen corner toggles (sound/music top-right, settings bottom-left)
    // edge-anchor so they hug the widened corners instead of floating inward.
    // NOTE: the ring buttons (menu.play/dojo/moregames) are deliberately NOT
    // here -- they use PROPORTIONAL spread so the 3 rings stay evenly distributed
    // across the wider field (edge-anchoring spread them unevenly: dojo isolated
    // left, play+bomb crowded right).
    { "menu.sound"    },
    { "menu.music"    },
    { "menu.settings" },
    // Red bomb QUIT/back button (m_pQuitButton, m_RingTex[16]) -- back/quit
    // buttons edge-anchor universally (same rule as modeselect.btn.back /
    // settings.back / pause.quit); positioned via m_HudScale not pos, see
    // MainScreen::CreateQuitButton.
    { "menu.quit",    BACK_BOMB_RIGHT_PAD },
    // PauseScreen's quit button (m_QuitButton, kCloseBtnX=215) -- was MapX'd
    // proportional despite being a back/quit button; edge-anchor per the
    // universal back/quit rule (same corner treatment as settings.back).
    { "pause.quit"    },
    // PowerUp::DrawBar's active-powerup meter row (PowerUpManager::Update /
    // ActivatePower) -- the row's origin (the "0" the per-slot 110/-55
    // offsets are measured from) is right-edge-anchored so the whole row of
    // bars hugs the widened top-right edge instead of drifting toward centre
    // as HalfWidth() grows. MapX is called with x=0.0f at this key, so with
    // rightPad=0 the override degenerates to a pure `(halfWidth-240)` shift.
    { "powerup.bar"   },
};
const int kNumOverrides = sizeof(kOverrides) / sizeof(kOverrides[0]);

const KeyOverride* FindOverride(const char* key) {
    if (!key) return NULL;
    for (int i = 0; i < kNumOverrides; ++i) {
        if (strcmp(kOverrides[i].key, key) == 0) return &kOverrides[i];
    }
    return NULL;
}
} // namespace

bool IsWideLayout() {
    return g_WideLayout;
}

void SetWideLayout(bool wide) {
    g_WideLayout = wide;
    // Seeds the pref too -- called at boot (LoadSettings), so active == pref
    // right after load. Runtime pref-only edits go through SetWideLayoutPref.
    g_WideLayoutPref = wide;
}

bool IsWideLayoutPref() {
    return g_WideLayoutPref;
}

void SetWideLayoutPref(bool wide) {
    g_WideLayoutPref = wide;
}

bool WideLayoutRestartPending() {
    return g_WideLayoutPref != g_WideLayout;
}

void SetWindowAspect(float drawableW, float drawableH) {
    if (drawableH <= 0.0f) return;
    g_RawWindowAspect = drawableW / drawableH;
}

float EffectiveAspect() {
    if (!g_WideLayout) return 1.5f;
    float a = g_RawWindowAspect;
    if (a < 1.5f) a = 1.5f;
    if (a > (16.0f / 9.0f)) a = 16.0f / 9.0f;
    return a;
}

float HalfWidth() {
    if (!g_WideLayout) return 240.0f;
    return 240.0f * (EffectiveAspect() / 1.5f);
}

float MapX_impl(float x, const char* key) {
    if (!g_WideLayout) return x;
    float halfWidth = HalfWidth();
    const KeyOverride* ov = FindOverride(key);
    if (ov) {
        // Edge-anchor: keep the element's pixel gap to its nearest screen edge
        // constant. The edge moved out by (halfWidth - 240) when the field
        // widened, so shift the element by that same amount toward its edge.
        // Identity at halfWidth==240. Example: an element whose right edge was
        // 30px from +240 keeps its right edge 30px from +halfWidth.
        float sign = (x < 0.0f) ? -1.0f : 1.0f;
        // rightPad pulls the element back toward centre by ov->rightPad units
        // (0 for most keys); gives the back/quit bombs a little edge padding.
        return x + sign * (halfWidth - 240.0f - ov->rightPad);
    }
    return x * (halfWidth / 240.0f);   // proportional default (near-center / spread)
}

// Pass 3: centred pillarbox/letterbox viewport rect, shared by render and
// input. Mirrors the pillarbox/letterbox math that used to live inline in
// Game::renderFrame (GameSDL.cpp) -- moved here so InputTranslatorSDL can
// invert the exact same rect instead of re-deriving it (two independent
// copies of this math would drift on the widescreen edges).
void ComputeViewport(int winW, int winH, int* outX, int* outY, int* outW, int* outH) {
    int vpX = 0, vpY = 0, vpW = winW, vpH = winH;
    if (g_WideLayout && winW > 0 && winH > 0) {
        float targetAspect = EffectiveAspect();
        float windowAspect = (float)winW / (float)winH;
        if (windowAspect > targetAspect) {
            // Window wider than target -- pillarbox (side bars).
            vpW = (int)(winH * targetAspect + 0.5f);
            vpX = (winW - vpW) / 2;
        } else if (windowAspect < targetAspect) {
            // Window narrower than target -- letterbox (top/bottom bars).
            vpH = (int)(winW / targetAspect + 0.5f);
            vpY = (winH - vpH) / 2;
        }
    }
    *outX = vpX;
    *outY = vpY;
    *outW = vpW;
    *outH = vpH;
}

void SetActiveViewport(int x, int y, int w, int h, int winW, int winH) {
    g_ViewportX = x;
    g_ViewportY = y;
    g_ViewportW = w;
    g_ViewportH = h;
    g_ViewportWinW = winW;
    g_ViewportWinH = winH;
}

// Inverts the stored viewport rect: window-normalized touch (nx, ny in
// [0,1]) -> pixel position within the window -> normalized position within
// the viewport rect -> centred game-ortho coords.
//
// When !IsWideLayout(), SetActiveViewport was last called with the full
// window (ComputeViewport's early-out), i.e. x=y=0, w=winW, h=winH. Then:
//   u = (nx*winW - 0) / winW = nx
//   gx = u*2*HalfWidth() - HalfWidth() = nx*2*240 - 240 = nx*480 - 240
//   v = (ny*winH - 0) / winH = ny
//   gy = 160 - v*320 = 160 - ny*320
// -- identical to the pre-Pass-3 hardcoded TransformTouchNormalized body.
void TouchToGame(float nx, float ny, float* gx, float* gy) {
    if (g_ViewportW <= 0 || g_ViewportH <= 0) {
        // No viewport recorded yet (first frame before any render) -- fall
        // back to the identity (full-window) mapping.
        *gx = nx * 2.0f * HalfWidth() - HalfWidth();
        *gy = 160.0f - ny * 320.0f;
        return;
    }
    float u = (nx * (float)g_ViewportWinW - (float)g_ViewportX) / (float)g_ViewportW;
    float v = (ny * (float)g_ViewportWinH - (float)g_ViewportY) / (float)g_ViewportH;
    *gx = u * 2.0f * HalfWidth() - HalfWidth();
    *gy = 160.0f - v * 320.0f;
}

} // namespace Layout

#endif // __bada__
