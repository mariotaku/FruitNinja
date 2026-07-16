#include "Layout.h"
#include <cstddef>   // NULL

// DIFFERS: opt-in widescreen (Layout::HalfWidth); faithful 240 under __bada__
// Real (non-bada) build only -- under __bada__ the header macro-shortcuts
// MapX/HalfWidth away entirely, so this whole TU is inert there.
#ifndef __bada__

namespace Layout {

namespace {
bool g_WideLayout = false;
float g_RawWindowAspect = 1.5f;

// Pass 2 fills this in with real per-key X overrides. Pass 1 keeps it
// empty -- every MapX call site is proportional-only for now.
struct KeyOverride {
    const char* key;
    float x;
};
const KeyOverride* FindOverride(const char* key) {
    (void)key;
    return NULL;
}
} // namespace

bool IsWideLayout() {
    return g_WideLayout;
}

void SetWideLayout(bool wide) {
    g_WideLayout = wide;
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
    const KeyOverride* ov = FindOverride(key);
    if (ov) return ov->x;
    float k = HalfWidth() / 240.0f;
    return x * k;
}

} // namespace Layout

#endif // __bada__
