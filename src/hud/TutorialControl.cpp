//
// TutorialControl — "swipe here" arrow for first-play tutorial.
// See TutorialControl.h for binary refs.
//
// Analysed: 2026-04-17T08:00
//

#include "TutorialControl.h"
#include "MenuButton.h"
#include "Game.h"
#include "HUD.h"
#include "asset/TextureManager.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include <cmath>

// Animation phase boundaries (seconds)
static const float ANIM_INACTIVE   = -10.0f;  // sentinel: timer reset value
static const float PHASE_FADEIN    =  0.35f;   // 0..0.35: fade in + bounce up
static const float PHASE_BOUNCE    =  0.60f;   // 0.35..0.6: bounce back down
static const float PHASE_HOLD_END  =  2.25f;   // 0.6..2.25: hold visible + trail
static const float PHASE_FADEOUT   =  2.75f;   // 2.25..2.75: fade out
static const float BOUNCE_OFFSET   =  20.0f;   // Y bounce magnitude

// Arrow draw scale
static const float ARROW_SCALE     =  96.0f;   // DAT_001635c8

// Half-width cap
static const float HALFWIDTH_CAP   = 256.0f;   // DAT_00162f80

// ===================================================================
// Matches TutorialControl::TutorialControl @ 0x001636f8
// ===================================================================
TutorialControl::TutorialControl()
    : m_AnimTimer(ANIM_INACTIVE)
    , m_DrawPos(0, 0, 0)
    , m_Colour(255, 255, 255, 255)
    , m_bHidden(1)
    , m_HalfWidth(0.0f)
    , m_bFlipX(false)
{
    // Load textures
    SmartPtr<Mortar::Texture> tex;
    tex = Mortar::TextureManager::LoadLocalisedTexture("swipe_fruit_begin.tex");
    if (tex.IsValid()) m_SecondaryTex = tex->m_TexId;

    m_PrimaryTex = Mortar::TextureManager::LoadLocalisedTexture("press_indicate.tex");

    m_LayerFlags = 8;
}

TutorialControl::~TutorialControl() {
}

// ===================================================================
// Matches TutorialControl::Init @ 0x00162e38
// ===================================================================
void TutorialControl::Init() {
    m_LayerFlags = 8;
    Reset();
}

// ===================================================================
// Matches TutorialControl::Release @ 0x00162e48 (no-op)
// ===================================================================
void TutorialControl::Release() {
}

// ===================================================================
// Matches TutorialControl::Reset @ 0x00162e4c
// ===================================================================
void TutorialControl::Reset() {
    m_AnimTimer = ANIM_INACTIVE;
}

// ===================================================================
// Matches TutorialControl::CanShowTute @ 0x00162fb8
// Returns true during slow-motion or screen transitions.
// ===================================================================
bool TutorialControl::CanShowTute() const {
    Game* game = Game::GetInstance();
    if (!game) return false;

    // If a transition is running, show the tutorial
    if (fabsf(game->m_TransitionTimer) > 0.0f)
        return true;

    // No game-over screen → don't show
    // (binary checks pGameOverScreen at game+0x164)
    // Port: GameOverScreen not fully ported, skip this check

    // Only show during slow-motion (HUD timeScale < 1.0)
    if (!game->hud) return false;
    // Port: HUD doesn't have m_globalTimeScale yet.
    // For now, return false (tutorial never shows until timeScale is ported).
    return false;
}

// ===================================================================
// Matches TutorialControl::ResetTutePos(MenuButton*) @ 0x00162f04
// ===================================================================
void TutorialControl::ResetTutePos(MenuButton* btn) {
    if (btn) {
        // Copy button position
        pos = btn->pos;

        // Compute arrow half-width from button bounds
        // Binary: halfWidth = (btn->halfWidth - btn->innerPadding*2) - 10
        // Port: approximate from button size
        float halfWidth = btn->size.x * 0.5f - 10.0f;
        if (halfWidth > HALFWIDTH_CAP) {
            halfWidth *= 0.5f;
        }
        m_HalfWidth = halfWidth;

        // Flip direction: pos.x > 0 → button is right of center → arrow flips
        m_bFlipX = (pos.x > 0.0f);

        // Binary: XOR with btn->bFlipped (+0x120)
        // Port: MenuButton doesn't have bFlipped yet, skip
    }
    m_AnimTimer = ANIM_INACTIVE;
}

// ===================================================================
// Matches TutorialControl::ResetTutePos(Vec3) @ 0x00162f84
// ===================================================================
void TutorialControl::ResetTutePos(const Vec3& targetPos) {
    pos = targetPos;
    m_bFlipX = (pos.x > 0.0f);
    m_AnimTimer = ANIM_INACTIVE;
}

// ===================================================================
// Matches TutorialControl::Update @ 0x00163014
// Animation lifecycle: -10=inactive, 0..2.75=animating
// ===================================================================
void TutorialControl::Update(float dt) {
    m_LayerFlags = 8;

    // Default: hide (far off-screen)
    m_DrawPos = Vec3(-1000.0f, -1000.0f, -1000.0f);
    m_bHidden = 1;

    if (!CanShowTute()) {
        m_AnimTimer = ANIM_INACTIVE;
        m_DrawPos = m_DrawPos + pos;
        return;
    }

    if (m_AnimTimer >= PHASE_FADEOUT) {
        // Animation complete — stay at final position
        m_DrawPos = m_DrawPos + pos;
        return;
    }

    // Advance timer
    m_AnimTimer += dt;

    // Compute animated scale
    // Lerp from (-0.5, -0.075, 0) to (1.0, 0.15, 0) over t
    float t = (m_AnimTimer - 1.0f) * 2.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    Vec3 scaleStart(-0.5f, -0.075f, 0.0f);
    Vec3 scaleEnd(1.0f, 0.15f, 0.0f);
    Vec3 scale = scaleStart + scaleEnd * t;

    m_DrawPos = scale * m_HalfWidth;
    if (m_bFlipX) m_DrawPos.x = -m_DrawPos.x;

    if (m_AnimTimer <= 0.0f) {
        m_DrawPos = m_DrawPos + pos;
        return;
    }

    // --- Animation phases ---
    if (m_AnimTimer < PHASE_FADEIN) {
        // Phase 1: fade in (0..0.35)
        float alpha = (m_AnimTimer / PHASE_FADEIN) * 255.0f;
        m_DrawPos.y += BOUNCE_OFFSET;
        m_bHidden = 0;
        m_Colour.a = (uint8_t)(alpha > 0.0f ? (alpha < 255.0f ? alpha : 255.0f) : 0.0f);
    } else if (m_AnimTimer < PHASE_BOUNCE) {
        // Phase 2: bounce back (0.35..0.6)
        float f = m_AnimTimer - PHASE_FADEIN;
        m_DrawPos.y = m_DrawPos.y + f * 4.0f * (-BOUNCE_OFFSET) + BOUNCE_OFFSET;
        m_bHidden = 0;
        m_Colour.a = 255;
    } else if (m_AnimTimer < PHASE_HOLD_END) {
        // Phase 3: hold visible (0.6..2.25)
        m_bHidden = 0;
        m_Colour.a = 255;
    } else if (m_AnimTimer < PHASE_FADEOUT) {
        // Phase 4: fade out (2.25..2.75)
        float f = ((m_AnimTimer - PHASE_HOLD_END) * 2.0f) * (-254.0f) + 255.0f;
        m_DrawPos.y += BOUNCE_OFFSET;
        m_bHidden = 0;
        m_Colour.a = (uint8_t)(f > 0.0f ? f : 0.0f);
    } else {
        // Past 2.75: done, reset
        m_DrawPos.y += BOUNCE_OFFSET;
        m_bHidden = 0;
        m_AnimTimer = ANIM_INACTIVE;
    }

    m_DrawPos = m_DrawPos + pos;
}

// ===================================================================
// Matches TutorialControl::Draw @ 0x00163360
// Draws trail quads (swipe_fruit_begin.tex) + arrow (press_indicate.tex)
// ===================================================================
void TutorialControl::Draw(const Vec3& hudScale, int layerMask) {
    if ((layerMask & m_LayerFlags) == 0) return;
    if (m_AnimTimer <= 0.0f) return;
    if (m_bHidden) return;

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    float flipSign = m_bFlipX ? -1.0f : 1.0f;

    // --- Trail quads (swipe_fruit_begin.tex) during hold phase ---
    if (m_AnimTimer > PHASE_BOUNCE && m_AnimTimer < PHASE_HOLD_END &&
        m_SecondaryTex != 0) {
        // Binary draws 4 trail quads with varying alpha and offset.
        // Simplified port: draw one trail quad at the draw position.
        // TODO: full 4-quad trail with time-based offset and quartic falloff
    }

    // --- Arrow (press_indicate.tex) ---
    if (m_PrimaryTex.IsValid()) {
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(
            flipSign * ARROW_SCALE,
            ARROW_SCALE,
            1.0f);
        // Offset: (-0.125, -0.40625, 0) * m_HalfWidth, then add m_DrawPos
        Vec3 offset(flipSign * -0.125f * m_HalfWidth,
                    -0.40625f * m_HalfWidth,
                    0.0f);
        Vec3 drawAt = offset + m_DrawPos;
        mat.GlobalTranslate44(drawAt);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        m_PrimaryTex->Set();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        r->DrawQuad(m_Colour);
        m_PrimaryTex->UnSet();
    }
}
