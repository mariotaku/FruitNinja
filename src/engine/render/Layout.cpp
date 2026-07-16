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
//  - Proportional (no entry): x * (HalfWidth()/240) -- the Pass-1 default.
//    Barely moves a near-center element, which is correct for a single
//    centered piece (e.g. the FN logo) that can't meaningfully "lean".
//  - Lean (table entry below): a side-anchored element (e.g. the sensei
//    character) is pulled toward a fixed FRACTION of HalfWidth() on its own
//    side, growing with the extra width instead of staying proportional to
//    its small original offset. Expressed as an EDGE_FRACTION in [0,1]
//    (0 = screen center, 1 = screen edge == HalfWidth()); the result is
//    always bounded to +-HalfWidth() by construction, so it can never
//    overshoot off-screen regardless of aspect ratio.
//
// Tunable aesthetic constants -- eyeball against HLE/screenshots and adjust.
static const float ABOUT_SENSEI_EDGE_FRACTION = 0.88f; // sensei.tex on AboutScreen
static const float DECO_SMLTITLE_EDGE_FRACTION = 0.90f; // sml_title.tex on Dojo/GameMode/About (BaseScreen::DrawBorders)

struct KeyOverride {
    const char* key;
    float edgeFraction;
};
const KeyOverride kOverrides[] = {
    { "about.sensei",   ABOUT_SENSEI_EDGE_FRACTION },
    { "deco.smltitle",  DECO_SMLTITLE_EDGE_FRACTION },
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
    float k = halfWidth / 240.0f;   // 1.0 .. 16/9/1.5 as the window widens
    const KeyOverride* ov = FindOverride(key);
    if (ov) {
        // Lean: blend from the proportional position (t=0, at k==1 this is
        // just x) toward a fixed EDGE_FRACTION of HalfWidth() on the
        // element's own side (t=1, as k reaches its max). t tracks how far
        // into the extra width budget we are, so the mapping is continuous
        // and always identity at k==1 (non-wide / __bada__ parity).
        static const float kMaxK = (16.0f / 9.0f) / 1.5f;
        float t = (kMaxK > 1.0f) ? (k - 1.0f) / (kMaxK - 1.0f) : 0.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float sign = (x < 0.0f) ? -1.0f : 1.0f;
        float edgeTarget = sign * ov->edgeFraction * halfWidth;
        float proportional = x * k;
        return proportional + (edgeTarget - proportional) * t;
    }
    return x * k;
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
