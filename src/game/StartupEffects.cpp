// Analysed: 2026-04-30T00:00

#include "StartupEffects.h"
#include "GameTaskState.h"
#include "FruitCamera.h"
#include "Game.h"
#include "WaveManager.h"
#include "asset/Mesh.h"
#include "render/MatrixManager.h"
#include "math/Matrix44.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include "core/MortarTypes.h"
#include "debug/Logger.h"
#include <cstdint>
#include "game/GameWork.h"
#include "render/Layout.h"
#if defined(FRUIT_PLATFORM_WII)
#include "platform/wii/WiiVideo.h"
#endif

namespace FN {
// @ 0x0016bbf0
void DrawNews() {
    // TODO: implement -- news ticker / MOTD overlay draw
}

// Port specific: web audio-consent splash freeze (see StartupEffects.h).
// Default false everywhere; only ever set true by mainEmscripten.cpp
// (Emscripten-only), so desktop/bada builds never see it flip.
bool g_AudioConsentPending = false;

// Port specific: web save-file-existed-at-boot flag (see StartupEffects.h).
// Default false everywhere; only ever written by GameInitialise.cpp under
// __EMSCRIPTEN__, so desktop/bada builds never see it flip.
bool g_SaveFileExisted = false;
} // namespace FN

// DrawStartFade @ 0x001cd4fc
// 3-phase splash overlay: white fill -> logo on white -> fade out.
// Timer (splashFadeTimer) drains from 1.5 -> 0 at 2*dt per frame.
// Phases:
//   t in (0.5, 1.5]: rgb_factor ramps 1->0, alpha_factor 1->2, bright=1 (white box, then logo reveals)
//   t in (0.0, 0.5]: bright ramps 1->0, rgb_factor=0, alpha_factor=1 (logo+white fade out)
void DrawStartFade() {
    GameTaskState* ts = GetTaskState();
    const float t = ts->splashFadeTimer;
    if (t <= 0.0f) return;

    // ASM-spec v1.6.1 DrawStartFade @0x001cd4fc: after the splashFadeTimer > 0 gate
    // the binary goes straight to `ldr r3,[r4,r3]; ldr r0,[r3,#0x4c]` -- game_work
    // from the GOT, m_FruitCamera loaded and passed to SetupPerspective unguarded.
    // No Game::GetInstance, no null test.
    Game* game = Game::GetInstance();

    // Binary calls FruitCamera::SetupPerspective(camera, 3, 1) to switch to ortho/screen mode.
    // TODO: v1.6.1 DrawStartFade @0x001cd4fc calls SetupPerspective with mode 4 (mov r1,#0x4); port passes PT_GENERIC(3). Mode-4 semantics unresolved -- verify before changing.
    if (game_work.m_FruitCamera) {
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_GENERIC, true);
    }

    float bright, alpha_factor, rgb_factor;
    if (t <= 0.5f) {
        bright       = t * 2.0f;
        rgb_factor   = 0.0f;
        alpha_factor = 1.0f;
    } else {
        rgb_factor   = (t - 0.5f) * 2.0f;
        if (rgb_factor > 1.0f) rgb_factor = 1.0f;
        bright       = 1.0f;
        alpha_factor = (1.0f - rgb_factor) * (1.0f - rgb_factor) + 1.0f;
    }

    if (!game->m_StartupTexture.IsValid()) return;

    MatrixManager& mm = MatrixManager::GetInstance();

    // v1.6.1 DrawStartFade @0x001cd4fc: alpha = bright only; growth goes to scale
    float a_f = bright * 255.0f;
    if (a_f < 0.0f) a_f = 0.0f;
    if (a_f > 255.0f) a_f = 255.0f;
    float r_f = rgb_factor * 255.0f;
    if (r_f < 0.0f) r_f = 0.0f;
    if (r_f > 255.0f) r_f = 255.0f;

    const uint8_t a = (uint8_t)a_f;
    const uint8_t r = (uint8_t)r_f;
    const Colour col(r, r, r, a);

    // DIFFERS: opt-in widescreen -- the 480-wide logo quad below only covers
    // x in [-240, 240], so on a widescreen drawable (Layout::HalfWidth() > 240)
    // two solid black SIDE-STRIP quads fill the remaining gap (x in
    // [240, HalfWidth] and [-HalfWidth, -240]) so the widened bars fade in/out
    // together with the logo instead of showing through. Faithful 3:2 /
    // __bada__: HalfWidth()==240 so stripW is exactly 0 and NO strips are
    // drawn -- this is the original single 480x320 logo quad, unchanged.
    // The strips must NOT overlap the logo quad's own x range (that was the
    // old bug: a single full-width backdrop double-blended the center 480
    // while the side strips only blended once, producing a darker center,
    // brighter sides, and hard seams at x=+-240). The strips track the SAME
    // bright/rgb_factor alpha as the logo (col.a) but never scale down with
    // the logo's alpha_factor growth -- they stay full-coverage throughout
    // the whole fade, unlike the logo quad below which grows from 1x to 2x.
#ifdef __bada__
    const float stripW = 0.0f;
#else
    const float stripW = Layout::HalfWidth() - 240.0f;
#endif
    const Colour bgCol(0, 0, 0, col.a);

    // Port specific: the renderer's DrawQuad skips the draw entirely when no
    // texture is bound (see Renderer::DrawQuad's s_LastBoundTexId guard), so
    // the side-strip quads bind the splash texture too (degenerate UV samples
    // a texel that happens to be opaque, so the tint carries full colour) and
    // stay bound for the logo draw that follows -- one Set/UnSet bracket for
    // all quads.
    game->m_StartupTexture->Set();

    if (stripW > 0.0f) {
        const float stripCenter = (240.0f + Layout::HalfWidth()) * 0.5f;

        mm.GetWorldStack().Reset();
        Matrix44 matBgR = Matrix44::MakeScale(stripW, (float)FN_SCREEN_H, 0.0f);
        matBgR.GlobalTranslate44(_Vector3<float>(stripCenter, 0.0f, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(matBgR);
        mm.UploadModelViewOnly();
        // Degenerate UV (opaque texel) -- tint carries full colour.
        Mortar::Mesh::DrawQuadUnCached(bgCol, 0.0f, 0.0f, 0.0f, 0.0f, NULL);

        mm.GetWorldStack().Reset();
        Matrix44 matBgL = Matrix44::MakeScale(stripW, (float)FN_SCREEN_H, 0.0f);
        matBgL.GlobalTranslate44(_Vector3<float>(-stripCenter, 0.0f, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(matBgL);
        mm.UploadModelViewOnly();
        // Degenerate UV (opaque texel) -- tint carries full colour.
        Mortar::Mesh::DrawQuadUnCached(bgCol, 0.0f, 0.0f, 0.0f, 0.0f, NULL);
    }

    mm.GetWorldStack().Reset();
    // ASM-spec v1.6.1 DrawStartFade @0x001cd4fc: logo scale = Vec3(480,320,0)*scale_mul (grows 1x->2x = the "explode")
    Matrix44 mat = Matrix44::MakeScale((float)FN_SCREEN_W * alpha_factor, (float)FN_SCREEN_H * alpha_factor, 0.0f);
    mat.GlobalTranslate44(_Vector3<float>(0.0f, 0.0f, 0.0f));
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    // UV crop: binary draws logo rect (uMin=0.03125, uMax=0.96875, vMin=0.1875, vMax=0.8125)
    Mortar::Mesh::DrawQuadUnCached(col, 0.03125f, 0.96875f, 0.1875f, 0.8125f, NULL);

    game->m_StartupTexture->UnSet();

#if defined(FRUIT_PLATFORM_WII)
    // Port specific: hand off from the embedded boot-splash bridge (see
    // SplashBootScreen.h) to the game's own draw now that DrawStartFade has
    // actually drawn its first real frame -- pixel-identical to the bridge's
    // quad, so DisplayManagerWii::SwapBuffers can stop overlaying it and
    // release the transient buffer with no visible pop.
    fn::wii::NotifyGameSplashDrew();
#endif
}

// v1.6.1 FN::PrepareForLevelStart @ 0x001cb3e8
void PrepareForLevelStart() {
    LOG_DEBUG("FN", "PrepareForLevelStart: firing -> WaveManager::Reset(false)");
    // ASM-spec v1.6.1 PrepareForLevelStart @0x001cb3e8: snapshot coin balance before Reset
    game_work.m_CoinsAtGameStart = game_work.m_CoinsBalance;
    WaveManager::GetInstance()->Reset(false);
    // ASM-spec v1.6.1 PrepareForLevelStart @0x001cb3e8: the whole body is
    // `ldr r3,[r4,#0x20]; str r3,[r4,#0x28]` (coin snapshot), WaveManager::GetInstance,
    // Reset(0), then `mov r3,#1; strb r3,[r4,#5]`. No Game::GetInstance, no null test.
    game_work.bM_bPaused = 1;
}
