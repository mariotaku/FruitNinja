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

namespace FN {
// @ 0x0016bbf0
void DrawNews() {
    // TODO: implement -- news ticker / MOTD overlay draw
}
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

    Game* game = Game::GetInstance();
    if (!game) return;

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

    if (!game->pSplashTex.IsValid()) return;
    game->pSplashTex->Set();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    // ASM-spec v1.6.1 DrawStartFade @0x001cd4fc: logo scale = Vec3(480,320,0)*scale_mul (grows 1x->2x = the "explode")
    Matrix44 mat = Matrix44::MakeScale((float)FN_SCREEN_W * alpha_factor, (float)FN_SCREEN_H * alpha_factor, 0.0f);
    mat.GlobalTranslate44(_Vector3<float>(0.0f, 0.0f, 0.0f));
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

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

    // UV crop: binary draws logo rect (uMin=0.03125, uMax=0.96875, vMin=0.1875, vMax=0.8125)
    Mortar::Mesh::DrawQuadUnCached(col, 0.03125f, 0.96875f, 0.1875f, 0.8125f, NULL);

    game->pSplashTex->UnSet();
}

// v1.6.1 FN::PrepareForLevelStart @ 0x001cb3e8
void PrepareForLevelStart() {
    LOG_DEBUG("FN", "PrepareForLevelStart: firing -> WaveManager::Reset(false)");
    // ASM-spec v1.6.1 PrepareForLevelStart @0x001cb3e8: snapshot coin balance before Reset
    game_work.m_CoinsAtGameStart = game_work.m_CoinsBalance;
    WaveManager::GetInstance()->Reset(false);
    Game* game = Game::GetInstance();
    if (game) game_work.bM_bPaused = 1;
}
