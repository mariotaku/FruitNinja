// Port specific: Wiimote IR crosshair overlay. See WiiPointer.h.
//
// Draws crosshair.tex (assets/ui-widgets/crosshair.svg -- style C: thin
// cross + center ring), a symmetric square texture whose geometric center
// is the aim hotspot: reset the world stack, build a scale+translate
// matrix centered on the IR point, upload, bind texture, draw the full
// quad (u/v 0..1, no frame crop, no flip), unbind.
//
// Only compiled when FRUIT_PLATFORM_WII is set.
#ifdef FRUIT_PLATFORM_WII

#include "platform/wii/WiiPointer.h"
#include "platform/wii/InputTranslatorWii.h"
#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "asset/Mesh.h"
#include "render/MatrixManager.h"
#include "render/Renderer.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "util/SmartPtr.h"

namespace FN {
namespace wii {

namespace {

// Port specific tunables (no binary equivalent -- the original has no cursor).
const float kPointerScale = 20.8f;  // quad half-extent-ish, cursor-sized; tunable (was 62.4, user: 1/3)
const float kHideSpeed    = 1.0f;   // smoothed px/sim-tick above which the crosshair hides -- low, so any real motion hides it; shows only when near-still (aiming)
const float kPressScale   = 0.9f;   // optional/tunable: subtle shrink on A-held for press feedback

Mortar::SmartPtr<Mortar::Texture> s_tex;
bool s_initTried = false;

void EnsureLoaded() {
    if (s_initTried) return;
    s_initTried = true;
    s_tex = Mortar::TextureManager::LoadLocalisedTexture("crosshair.tex");
}

} // namespace

void WiiPointer_Init() {
    EnsureLoaded();
}

void WiiPointer_Draw(const InputTranslatorWii& in) {
    EnsureLoaded();
    if (!s_tex.IsValid()) return;

    Renderer::GetInstance()->SetupGameOrtho();
    MatrixManager& mm = MatrixManager::GetInstance();

    for (int remote = 0; remote < InputTranslatorWii::MAX_REMOTES; ++remote) {
        float gx, gy, speed;
        bool aHeld;
        if (!in.GetPointer(remote, &gx, &gy, &aHeld, &speed)) continue;
        if (speed > kHideSpeed) continue;

        // Symmetric crosshair, drawn full-texture (u/v 0..1) centered on
        // the IR aim point -- no frame crop, no flip, no anchor offset.
        float scale = aHeld ? kPointerScale * kPressScale : kPointerScale;

        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(scale, scale, 1.0f);
        mat.GlobalTranslate44(gx, gy, 0.0f);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        s_tex->Set();
        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), 0.0f, 1.0f, 0.0f, 1.0f, NULL);
        s_tex->UnSet();
    }
}

} // namespace wii
} // namespace FN

#endif // FRUIT_PLATFORM_WII
