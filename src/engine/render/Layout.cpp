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
// Default TRUE = current fit-into-window-preserving-aspect behaviour,
// unconditionally, so host/web are unaffected by this flag's existence
// unless something explicitly calls SetLetterbox(false).
bool g_Letterbox = true;

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
    // In-game Classic-mode 3-strikes miss indicator (the 3 X marks, top-right
    // corner) -- edge-anchor so it hugs the widened corner instead of
    // floating inward; per-icon spacing (kMC[] table) is untouched, only the
    // shared per-icon base X shifts (GameInit.cpp's MissControl setup block).
    { "hud.misscontrol" },
    // Red bomb QUIT/back button (m_pQuitButton, m_RingTex[16]) -- back/quit
    // buttons edge-anchor universally (same rule as modeselect.btn.back /
    // settings.back / pause.quit); positioned via m_HudScale not pos, see
    // MainScreen::CreateQuitButton.
    { "menu.quit",    BACK_BOMB_RIGHT_PAD },
    // PauseScreen's quit button (m_QuitButton, kCloseBtnX=215) -- was MapX'd
    // proportional despite being a back/quit button; edge-anchor per the
    // universal back/quit rule (same corner treatment as settings.back).
    { "pause.quit"    },
    // ScoreControl's top-left SCORE readout (the "SCORE" wordmark +
    // large score number, incl. the NEW BEST banner group it anchors) --
    // was MapX'd proportional despite being a corner-hugging HUD group
    // (same treatment as menu.sound/menu.music/hud.misscontrol). Both the
    // steady-state pos.x (ScoreControl::Update) and the SP wordmark xPos
    // (ScoreControl::PreDraw Section D) share this key, so the label and
    // number move together and keep their relative offset. Shared with
    // in-game HUD (ScoreControl persists from GameInit into game-over) --
    // both contexts should hug the widened corner, so one key is correct.
    // No rightPad: pure edge-anchor preserves the score group's (comfortable) 3:2
    // gap from the left edge in widescreen too -- no extra pad needed. 3:2/__bada__
    // identity (MapX returns x when !wide). Shared by in-game HUD + game-over score.
    { "hud.score" },
    // GameOverScreen's Classic-mode fact panel (sensei body+head + "SENSEI'S
    // FRUIT FACT" board -- one rigid group anchored on FruitFactControl::pos,
    // see GameOverScreen::Update common tail). Right-side panel -> edge-anchor
    // right so it hugs the widened right edge instead of leaving that space
    // empty. Small +5 rightPad keeps it a touch off the widened edge -- tunable.
    { "gameover.factboard", 5.0f },
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

bool IsLetterbox() {
    return g_Letterbox;
}

void SetLetterbox(bool letterbox) {
    g_Letterbox = letterbox;
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

// Port specific: pure aspect-fit geometry, factored out of ComputeViewport so
// Wii's ComputeViewportFitAlways (below) can reuse the exact same math
// without a second hand-copy that could drift. Centres the largest
// targetAspect-shaped rect that fits inside winW x winH. No gating of any
// kind -- callers decide whether/when to invoke this.
static void FitAspect(int winW, int winH, float targetAspect,
                      int* outX, int* outY, int* outW, int* outH) {
    int vpX = 0, vpY = 0, vpW = winW, vpH = winH;
    if (winW > 0 && winH > 0) {
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

// Pass 3: centred pillarbox/letterbox viewport rect, shared by render and
// input. Mirrors the pillarbox/letterbox math that used to live inline in
// Game::renderFrame (GameSDL.cpp) -- moved here so InputTranslatorSDL can
// invert the exact same rect instead of re-deriving it (two independent
// copies of this math would drift on the widescreen edges).
void ComputeViewport(int winW, int winH, int* outX, int* outY, int* outW, int* outH) {
    // g_Letterbox gates the fit-vs-stretch behaviour: false returns the full
    // window/EFB rect unconditionally (content stretches, no bars); default
    // true fits the largest EffectiveAspect()-shaped rect that fits inside
    // winW x winH, centred (pillarbox/letterbox bars on the excess).
    //
    // No g_WideLayout gate: an earlier version of this function only fit
    // when g_WideLayout was also on, reasoning that host/web's non-wide
    // default window is pre-sized to exactly EffectiveAspect() (3:2, see
    // mainSDL.cpp) so fit-vs-stretch would be unobservable there anyway.
    // That assumption doesn't hold for a webOS/TV compositor, which resizes
    // the window to the physical panel (e.g. 1920x1080) regardless of the
    // app's requested size -- exactly the Wii's situation (its "window" is
    // the TV's own physical aspect, never pre-shaped to match content). So
    // this now always fits when g_Letterbox is on, matching what used to be
    // the Wii-only ComputeViewportFitAlways. Desktop's pre-sized 3:2 window
    // still makes this a no-op there (FitAspect degenerates to the full
    // rect when windowAspect == targetAspect), preserving byte-identical
    // output for a non-widescreen host/web build that isn't resized.
    if (g_Letterbox) {
        FitAspect(winW, winH, EffectiveAspect(), outX, outY, outW, outH);
    } else {
        *outX = 0; *outY = 0; *outW = winW; *outH = winH;
    }
}

// Port specific: kept as an alias so existing Wii call sites don't need to
// change -- ComputeViewport itself now always fits (see its comment above),
// so this is no longer a distinct behaviour.
void ComputeViewportFitAlways(int winW, int winH, int* outX, int* outY, int* outW, int* outH) {
    ComputeViewport(winW, winH, outX, outY, outW, outH);
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
