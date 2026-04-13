//
// BombHit.cpp — DrawBombHit white flash overlay.
// See BombHit.h for binary references.
//
// Analysed: 2026-04-13T22:00
//

#include "BombHit.h"
#include "Game.h"
#include "entities/BombBlast.h"
#include "asset/TextureManager.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "util/SmartPtr.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include <cstdio>

namespace FN {

// Binary DAT constants for DrawBombHit (docs/entities/bomb.md)
static const float FLASH_START     = 1.55f;   // DAT_0016b864
static const float FLASH_DUR_RECIP = -0.45f;  // DAT_0016b868 (divisor)
static const float FLASH_MAX_SCALE = 20000.0f;// DAT_0016b870
static const float FLASH_ALPHA_MUL = 255.0f;  // DAT_0016b874
static const float FLASH_THRESHOLD = 2.0f;    // only draws when timer < 2.0
static const float BLAST_PURGE_THR = 1.55f;   // DAT_0016a1fc

static Vec3 s_BombHitPos(0, 0, 0);
static SmartPtr<Mortar::Texture> s_FlashTex;

void SetBombHitPos(const Vec3& pos) {
    s_BombHitPos = pos;
}

void DrawBombHit() {
    Game* game = Game::GetInstance();
    if (!game) return;
    const float timer = game->bombHitTimer;
    if (timer <= 0.0f || timer >= FLASH_THRESHOLD) return;

    if (!s_FlashTex.IsValid()) {
        // Binary uses a dedicated flash texture (data+0x10c). We fall back
        // to a known-present asset until the exact filename is wired up.
        s_FlashTex = Mortar::TextureManager::LoadLocalisedTexture("bomb_explode.tex");
        if (!s_FlashTex.IsValid()) return;
    }

    // Scale animation: starts at 0 when timer == 1.55, grows to 20000 as
    // timer drops to 1.1, then stays at 20000 until FLASH_THRESHOLD passes.
    const float t = (timer - FLASH_START) / FLASH_DUR_RECIP + 1.0f;
    float scale;
    if (t <= 0.0f)      scale = 0.0f;
    else if (t < 1.0f)  scale = t * FLASH_MAX_SCALE;
    else                scale = FLASH_MAX_SCALE;

    if (scale <= 0.0f) return;

    // Alpha = clamp(255 * timer, 0, 255)
    int a = (int)(FLASH_ALPHA_MUL * timer);
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    const Colour tint(255, 255, 255, (uint8_t)a);

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    Matrix44 mat = Matrix44::MakeScale(scale, scale, 1.0f);
    mat.GlobalTranslate44(s_BombHitPos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    s_FlashTex->Set();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (Renderer* r = Renderer::GetInstance()) {
        r->DrawQuad(tint);
    }
    s_FlashTex->UnSet();
}

void UpdateBombHit(float prevTimer) {
    Game* game = Game::GetInstance();
    if (!game) return;
    const float currentTimer = game->bombHitTimer;

    // Binary: at 1.5s threshold, calls ResetGameEntities(false).
    // TODO: port ResetGameEntities once the Fruit pool/wave machinery lands.
    (void)prevTimer;

    // Below 1.55s, bulk-remove every live BombBlast (type 4) — matches
    // RemoveFlashEntities (0x169ca0).
    if (currentTimer > 0.0f && currentTimer < BLAST_PURGE_THR) {
        BombBlast::RemoveAll();
    }
}

} // namespace FN
