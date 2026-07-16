#ifndef FN_ENGINE_RENDER_LAYOUT_H
#define FN_ENGINE_RENDER_LAYOUT_H

// DIFFERS: opt-in widescreen (Layout::HalfWidth); faithful 240 under __bada__
//
// Pass 1 of the widescreen enhancement. The binary's fruit-field / HUD
// coordinate space is a fixed 480x320 (480 wide -> +-240 horizontal half-
// width) ortho, always. This header adds an OPT-IN wider layout for the
// desktop/web port: when Layout::g_WideLayout is on, the horizontal half-
// width expands proportionally with the real window's aspect ratio (up to
// 16:9), and MapX() lets individual element X-positions be remapped (empty
// override table for now -- Pass 2 fills it in for HUD/screen elements).
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
//   - Wrap an element's X position with MapX(x, "some.unique.key") when a
//     Pass-2 per-element override is expected; the key is inert until an
//     override table entry exists for it.
//   - Call Layout::SetWindowAspect(drawableW, drawableH) once per frame (or
//     on resize) from the render/viewport code so EffectiveAspect() has a
//     fresh raw aspect to clamp.
//
// Never call MapX_impl / read g_WideLayout directly from game logic other
// than through the macro/setter API below -- keeps the __bada__ identity
// path trivially inspectable (a grep for `MapX(` shows every remap site).

#ifdef __bada__

#define MapX(x, key) (x)

#else

namespace Layout {

// Real (non-bada) build only. See Layout.cpp.
bool IsWideLayout();
void SetWideLayout(bool wide);

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

} // namespace Layout

#define MapX(x, key) (::Layout::MapX_impl((x), (key)))

#endif // __bada__

#endif // FN_ENGINE_RENDER_LAYOUT_H
