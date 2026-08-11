#ifndef FN_ENGINE_RENDER_LAYOUT_H
#define FN_ENGINE_RENDER_LAYOUT_H

// DIFFERS: opt-in widescreen (Layout::HalfWidth); faithful 240 under __bada__
//
// Pass 1 of the widescreen enhancement. The binary's fruit-field / HUD
// coordinate space is a fixed 480x320 (480 wide -> +-240 horizontal half-
// width) ortho, always. This header adds an OPT-IN wider layout for the
// desktop/web port: when Layout::g_WideLayout is on, the horizontal half-
// width expands proportionally with the real window's aspect ratio (up to
// 16:9), and MapX() lets individual element X-positions be remapped via a
// per-key override table (Layout.cpp's kOverrides) for HUD/screen elements
// that should lean toward an edge rather than just scale proportionally.
//
// Under __bada__ (the asm-verify cross-build, matching the real Bada ABI)
// every symbol here collapses to the exact original constant: HalfWidth()
// is always 240.0f and MapX(x, key) is the identity macro with `key`
// discarded at compile time -- zero asm divergence, zero cross-build cost.
//
// With g_WideLayout left at its default `false`, the real (non-__bada__)
// build takes the exact same numeric path as before this feature landed:
// HalfWidth() returns 240.0f and MapX_impl returns x unchanged. So the
// default game (--widescreen not passed) is byte-for-byte the original
// 3:2 layout; only passing --widescreen changes any output.
//
// Usage:
//   - Read the horizontal half-extent of the game world via
//     Layout::HalfWidth() instead of the literal 240.0f, at any call site
//     that needs to widen along with the opt-in layout (camera ortho,
//     fruit spawn edges, background quad scale).
//   - Wrap an element's X position with MapX(x, "some.unique.key") at its
//     position/draw site. With no matching entry in Layout.cpp's override
//     table, the key is inert and the element just scales proportionally
//     by HalfWidth()/240 (correct for a single centered piece that can't
//     meaningfully "lean", e.g. the main-menu FN logo). Registering the key
//     in the override table (see Layout.cpp's kOverrides / EDGE_FRACTION
//     constants) instead pulls the element toward a fixed fraction of
//     HalfWidth() on its own side -- for a side-anchored element (e.g. the
//     sensei character) that's what makes it lean toward the screen edge
//     as the window widens, rather than just drifting proportionally to
//     its small original offset. The blend is bounded to +-HalfWidth() by
//     construction (can't overshoot off-screen) and collapses to identity
//     at HalfWidth()==240 (non-wide / __bada__ parity).
//   - Call Layout::SetWindowAspect(drawableW, drawableH) once per frame (or
//     on resize) from the render/viewport code so EffectiveAspect() has a
//     fresh raw aspect to clamp.
//
// Never call MapX_impl / read g_WideLayout directly from game logic other
// than through the macro/setter API below -- keeps the __bada__ identity
// path trivially inspectable (a grep for `MapX(` shows every remap site).

#ifdef __bada__

#define MapX(x, key) (x)

namespace Layout {

// Faithful-build no-ops: SettingsSave / SettingsScreen call these unguarded
// (real widescreen state lives only in the non-__bada__ branch below).
inline bool IsWideLayout() { return false; }
inline void SetWideLayout(bool /*wide*/) {}

// Port specific: PREF vs ACTIVE split (see non-__bada__ branch below) -- both
// collapse to the same false/no-op under __bada__.
inline bool IsWideLayoutPref() { return false; }
inline void SetWideLayoutPref(bool /*wide*/) {}
inline bool WideLayoutRestartPending() { return false; }

// Faithful-build no-ops: SettingsSave calls these unguarded too (real
// letterbox state lives only in the non-__bada__ branch below).
inline bool IsLetterbox() { return true; }
inline void SetLetterbox(bool /*letterbox*/) {}

// Collapses to the original constant, as the header comment above promises.
// Draw code (StartupEffects' splash side-strips, BombHit's crit flash) calls
// this unguarded; at 240.0f every widescreen-only term evaluates to zero
// width, so the faithful build emits the original single-screen geometry.
inline float HalfWidth() { return 240.0f; }

} // namespace Layout

#else

namespace Layout {

// Real (non-bada) build only. See Layout.cpp.
//
// ACTIVE vs PREF split: the widescreen layout requires an app restart to
// apply, because already-built screens position their elements via MapX()
// at construction time and don't re-flow live. IsWideLayout()/SetWideLayout()
// are the ACTIVE (live, boot-time-latched) value every render/input call site
// above reads -- do NOT call SetWideLayout() from the in-game checkbox.
// IsWideLayoutPref()/SetWideLayoutPref() are the user's SAVED CHOICE, edited
// freely by the checkbox at runtime with no immediate visual effect.
// SetWideLayout() seeds BOTH active and pref (so boot always starts with
// active == pref); LoadSettings() calls it, which is what makes the saved
// choice live from the next boot. The checkbox calls SetWideLayoutPref()
// only. WideLayoutRestartPending() is true once the two have diverged --
// the Settings screen uses it to warn the user a restart (quit) is needed.
bool IsWideLayout();
void SetWideLayout(bool wide);
bool IsWideLayoutPref();
void SetWideLayoutPref(bool wide);
bool WideLayoutRestartPending();

// Port specific: letterbox/pillarbox toggle -- gates ComputeViewport's fit
// behaviour. Default TRUE (= current fit-into-window-preserving-aspect
// behaviour, unchanged from before this flag existed) so host/web stay
// byte-identical unless something explicitly flips it off. Unlike
// IsWideLayout()/SetWideLayout(), there is no ACTIVE/PREF split: this applies
// LIVE, the next ComputeViewport call (no already-built-screen MapX()
// positions depend on it, only the viewport rect), so a UI toggle can call
// SetLetterbox() directly. When false, ComputeViewport returns the full
// window/EFB rect unconditionally (content stretches to fill, no bars) --
// see ComputeViewport's own comment.
bool IsLetterbox();
void SetLetterbox(bool letterbox);

// Raw drawable aspect (w/h) most recently reported by the render/viewport
// code. Feeds EffectiveAspect()'s clamp; a no-op call site is safe (keeps
// the previous value / defaults to 1.5f before the first frame).
void SetWindowAspect(float drawableW, float drawableH);

// Clamped to [1.5, 16/9]. Always 1.5f when !IsWideLayout().
float EffectiveAspect();

// 240.0f when !IsWideLayout(); otherwise 240.0f * (EffectiveAspect()/1.5f).
float HalfWidth();

// Real-build implementation behind the MapX macro. `key` names the call
// site for a future per-element override (Pass 2); the override table is
// intentionally empty in Pass 1, so every call proportionally scales by
// HalfWidth()/240.0f (or returns x unchanged when layout is not wide).
float MapX_impl(float x, const char* key);

// Pass 3: single source of truth for the centred pillarbox/letterbox
// viewport rect, shared by render (Game::renderFrame's glViewport) and
// input (InputTranslatorSDL::TransformTouchNormalized). Computes the
// largest EffectiveAspect()-shaped rect that fits inside winW x winH,
// centred, whenever IsLetterbox() is on (default true) -- independent of
// IsWideLayout(), since a compositor (webOS panel resize, Wii TV aspect)
// can hand the app a window shape that doesn't match the content aspect
// even without widescreen. Returns the full window unchanged
// (0, 0, winW, winH) when !IsLetterbox() (content stretches to fill instead
// of being aspect-fit with bars). A desktop window pre-sized to exactly
// EffectiveAspect() (mainSDL.cpp's non-wide default) is a no-op here by
// construction, so a non-widescreen host/web build that isn't resized stays
// byte-identical to pre-Pass-3 behaviour.
void ComputeViewport(int winW, int winH, int* outX, int* outY, int* outW, int* outH);

// Port specific: alias for ComputeViewport, kept so existing Wii call sites
// don't need to change. ComputeViewport itself now always fits whenever
// IsLetterbox() is on, so this is no longer a distinct behaviour.
void ComputeViewportFitAlways(int winW, int winH, int* outX, int* outY, int* outW, int* outH);

// Stores the most recently applied viewport rect + the window size it was
// computed from. Call once per frame right after ComputeViewport/glViewport
// so TouchToGame can invert the same rect input used to produce it.
void SetActiveViewport(int x, int y, int w, int h, int winW, int winH);

// Maps window-normalized touch coords (nx, ny in [0,1], SDL convention:
// origin top-left, y down) through the last-stored viewport (see
// SetActiveViewport) into centred game-ortho coords (gx in
// [-HalfWidth(), +HalfWidth()], gy in [-160, +160], y up).
// When !IsWideLayout() (viewport == full window), this reduces exactly to
// the original nx*480-240 / 160-ny*320 mapping -- see Layout.cpp.
void TouchToGame(float nx, float ny, float* gx, float* gy);

} // namespace Layout

#define MapX(x, key) (::Layout::MapX_impl((x), (key)))

#endif // __bada__

#endif // FN_ENGINE_RENDER_LAYOUT_H
