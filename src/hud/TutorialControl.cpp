//
// TutorialControl -- "swipe here" arrow for first-play tutorial.
// See TutorialControl.h for binary refs.
//
// Analysed: 2026-05-02T00:00
//

#include "TutorialControl.h"
#include "MenuButton.h"
#include "Game.h"
#include "HUD.h"
#include "hud/HUDLayer.h"
#include "asset/Mesh.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include <cmath>
#include "game/GameWork.h"

// Animation phase boundaries (seconds)
static const float ANIM_INACTIVE   = -10.0f;  // sentinel: timer reset value
static const float PHASE_FADEIN    =  0.35f;   // 0..0.35: fade in + bounce up
static const float PHASE_BOUNCE    =  0.60f;   // 0.35..0.6: bounce back down (DAT_001635ac)
static const float PHASE_HOLD_END  =  2.25f;   // 0.6..2.25: hold visible + trail
static const float PHASE_FADEOUT   =  2.75f;   // 2.25..2.75: fade out
static const float BOUNCE_OFFSET   =  20.0f;   // Y bounce magnitude

// Arrow draw scale
static const float ARROW_SCALE     =  96.0f;   // DAT_001635c8

// Half-width threshold for ButtonPressedAtPos and ResetTutePos
// Binary halves (* 0.5) when halfWidth > 256, NOT clamps.
static const float HALFWIDTH_THRESH = 256.0f;  // DAT_00162efc / DAT_00162f80
static const float HALFWIDTH_HALVE  = 0.5f;

// Trail loop constants (all from 0x001635ac block)
static const float TRAIL_TIMER_SCALE = 2000.0f; // DAT_001635b0
static const float TRAIL_ALPHA_MAX   =  255.0f; // DAT_001635b4
static const float TRAIL_MOD_DIV     = 1000.0f; // DAT_001635b8
static const float TRAIL_ALPHA_SLOPE = -255.0f; // DAT_001635bc
static const float TRAIL_FADE_IN_END =    0.85f; // DAT_001635c0
static const float TRAIL_FADE_OUT_START = 2.0f;  // from binary phase logic

// ===================================================================
// ASM-verified: 2026-07-04T00:00 v1.6.1 TutorialControl::TutorialControl @ 0x001c2fdc (C1)
// (duplicate C2 body @ 0x001c30cc, unreferenced GCC ctor clone -- cosmetic)
// Texture assignment (verified):
//   swipe_fruit_begin.tex -> super.m_Texture (+0x74)  [arrow graphic]
//   press_indicate.tex    -> m_PressTex (+0x8C)       [trail quads]
// ===================================================================
TutorialControl::TutorialControl()
    : m_AnimTimer(ANIM_INACTIVE)
    , m_DrawPos(0, 0, 0)
    , m_Colour(255, 255, 255, 255)
                  // DIFFERS: original = m_Colour BLACK (0,0,0,255) from Colour::Colour() @0x0011afa8
                  // (v1.6.1 TutorialControl::TutorialControl @0x001c2fdc; Update @0x001c27ac only animates m_Colour.a).
                  // Using WHITE (255,255,255,255) because the binary's per-texture COMBINE texenv
                  // (v1.6.1 Texture2D_Bada::Set @0x00229758: COMBINE_ALPHA=REPLACE, SRC0_ALPHA=PRIMARY_COLOR)
                  // takes RGB from the texture and only alpha from the vertex, so black vertex RGB does NOT tint
                  // the colour swipe_fruit_begin hand. The port's Mesh::DrawQuadUnCached modulates vertex RGB x texture,
                  // which would blacken it; white gives the faithful colour-hand output.
    , m_bHidden(1)
    , m_HalfWidth(0.0f)
    , m_bFlipX(false)
{
    // swipe_fruit_begin.tex -> m_Texture (+0x74, the arrow)
    {
        Mortar::SmartPtr<Mortar::Texture> tex =
            Mortar::TextureManager::LoadLocalisedTexture("swipe_fruit_begin.tex");
        if (tex.IsValid()) m_Texture = tex;
    }

    // press_indicate.tex -> m_PressTex (+0x8C, the trail quads)
    m_PressTex = Mortar::TextureManager::LoadLocalisedTexture("press_indicate.tex");

    m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;
}

TutorialControl::~TutorialControl() {
}

// ===================================================================
// Matches TutorialControl::Init @ 0x00162e38
// ===================================================================
void TutorialControl::Init() {
    m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;
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
// ASM-spec v1.6.1 TutorialControl::CanShowTute @ 0x001c2734:
//   if (Math::Abs(game_work.m_PauseAmount) > 0.99f) return true;
//   if (game_work.pGameOverScreen == 0 || game_work.mHud == 0) return false;
//   return *(float*)(mHud + 0x20) < 1.0f;
// NOTE: the pGameOverScreen (+0x168) and mHud (+0x40) null tests ARE in the
// binary (@0x001c2764 / @0x001c2770) -- do not strip them. There is no
// Game::GetInstance call in the body.
// ===================================================================
bool TutorialControl::CanShowTute() {
    // Binary @0x001c2750: DAT = 0x3F7D70A4 (0.99f).
    if (fabsf(game_work.m_PauseAmount) > 0.99f)
        return true;

    if (!game_work.pGameOverScreen) return false;
    if (!game_work.mHud) return false;
    // Binary @0x001c2780: vldr.32 s14,[r3,#0x20] -- HUD+0x20 is m_DrawAlpha,
    // NOT m_globalTimeScale (+0x24). Settles the prior +0x20-vs-+0x24 TODO.
    return game_work.mHud->m_DrawAlpha < 1.0f;
}

// ===================================================================
// Matches TutorialControl::ResetTutePos(MenuButton*) @ 0x001c2658
// ===================================================================
void TutorialControl::ResetTutePos(MenuButton* btn) {
    if (btn) {
        // Binary reads pos through vtable slot 15 = HUDControl::GetAdjustedPos()
        // (pos + Vec3(480,320,0)*m_HudScale @0x00136c2c), not raw pos -- anchors
        // to the animated (slide-in/out) position, not the rest position.
        pos = btn->GetAdjustedPos();

        // halfWidth = btn->m_RestScale.x - btn->m_HitInsetX * 2.0 - 10.0
        // ASM-spec v1.6.1 TutorialControl::ResetTutePos @ 0x001c2658: reads MenuButton+0x168 (m_HitInsetX)
        float halfWidth = btn->m_RestScale.x - btn->m_HitInsetX * 2.0f - 10.0f;
        // Binary @ 0x00162f44: halve, don't clamp
        if (halfWidth > HALFWIDTH_THRESH) halfWidth *= HALFWIDTH_HALVE;
        m_HalfWidth = halfWidth;

        // m_bFlipX = (pos.x > 0.0f) XOR (btn->m_FlipDirection != 0)
        // Binary v1.6.1 TutorialControl::ResetTutePos @0x001c2658: exact form
        m_bFlipX = (pos.x > 0.0f) != (btn->m_FlipDirection != 0);
    }
    m_AnimTimer = ANIM_INACTIVE;
}

// ===================================================================
// Matches TutorialControl::ResetTutePos(Vec3) @ 0x00162f84
// ===================================================================
void TutorialControl::ResetTutePos(const _Vector3<float>& targetPos) {
    pos = targetPos;
    m_bFlipX = (pos.x > 0.0f);
    m_AnimTimer = ANIM_INACTIVE;
}

// ===================================================================
// Matches TutorialControl::ButtonPressedAtPos @ 0x001c259c
// Advances timer by 9.5: shifts -10.0 sentinel to -0.5, so animation
// starts ~0.5 s after the next Update.  Guard: only fires when inactive.
// ===================================================================
void TutorialControl::ButtonPressedAtPos(MenuButton* btn) {
    if (m_AnimTimer >= 0.0f) return;   // only fires when inactive

    if (btn != nullptr) {
        pos = btn->pos;

        // halfWidth = btn->m_RestScale.x - btn->m_HitInsetX * 2.0 - 10.0
        // ASM-spec v1.6.1 TutorialControl::ButtonPressedAtPos @ 0x001c259c: reads MenuButton+0x168 (m_HitInsetX)
        float halfWidth = btn->m_RestScale.x - btn->m_HitInsetX * 2.0f - 10.0f;
        // Binary @ 0x00162ea6: halve, don't clamp
        if (halfWidth > HALFWIDTH_THRESH) halfWidth *= HALFWIDTH_HALVE;
        m_HalfWidth = halfWidth;

        // m_bFlipX = (pos.x > 0.0f) XOR (btn->m_FlipDirection != 0)
        // Binary v1.6.1 TutorialControl::ButtonPressedAtPos @0x001c259c: exact form
        m_bFlipX = (pos.x > 0.0f) != (btn->m_FlipDirection != 0);
    }

    m_AnimTimer += 9.5f;   // -10.0 -> -0.5; starts animation in ~0.5 s
    if (m_AnimTimer > 0.0f) m_AnimTimer = 0.0f;  // DAT_00162f00 = 0.0
}

// ===================================================================
// Matches v1.6.1 TutorialControl::Update @0x001c27ac
// Animation lifecycle: -10=inactive, 0..2.75=animating
// ===================================================================
void TutorialControl::Update(float dt) {
    m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;

    // Default: hide (far off-screen); m_bHidden=1 selects UV frame 1
    m_DrawPos = _Vector3<float>(-1000.0f, -1000.0f, -1000.0f);
    m_bHidden = 1;

    if (!CanShowTute()) {
        m_AnimTimer = ANIM_INACTIVE;
        m_DrawPos = m_DrawPos + pos;
        return;
    }

    if (m_AnimTimer >= PHASE_FADEOUT) {
        // Animation complete -- stay at final position
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

    _Vector3<float> scaleStart(-0.5f, -0.075f, 0.0f);
    _Vector3<float> scaleEnd(1.0f, 0.15f, 0.0f);
    _Vector3<float> scale = scaleStart + scaleEnd * t;

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
        // Binary never writes m_Colour.a in this sub-phase -- alpha carries
        // over unchanged from whatever the fade-in phase last computed.
    } else if (m_AnimTimer < PHASE_HOLD_END) {
        // Phase 3: hold (0.6..2.25). Binary @ 0x001631d0-0x001631f8 has three
        // early-exits (1.0, 1.5, 2.25) leaving m_bHidden=1 and alpha unchanged.
        // Net behavior: arrow draws UV frame 1 with prior alpha during hold.
    } else if (m_AnimTimer < PHASE_FADEOUT) {
        // Phase 4: fade out (2.25..2.75)
        float f = ((m_AnimTimer - PHASE_HOLD_END) * 2.0f) * (-254.0f) + 255.0f;
        m_DrawPos.y += BOUNCE_OFFSET;
        m_bHidden = 0;
        m_Colour.a = (uint8_t)(f > 0.0f ? f : 0.0f);
    } else {
        // Past 2.75: done, reset (ARM idiom: fires when timer >= 2.75)
        m_DrawPos.y += BOUNCE_OFFSET;
        m_bHidden = 0;
        m_AnimTimer = ANIM_INACTIVE;
    }

    m_DrawPos = m_DrawPos + pos;
}

// ===================================================================
// Matches TutorialControl::Draw @ 0x00163360
// Draws:
//   (1) 4-quad trail with press_indicate.tex (m_PressTex at +0x8C)
//   (2) Arrow with swipe_fruit_begin.tex (m_Texture at +0x74)
//
// m_bHidden is NOT a visibility gate; it selects the UV frame for the
// arrow: 0 -> u=[0.0,0.5], 1 -> u=[0.5,1.0].
// ===================================================================
void TutorialControl::Draw(float* hudScaleRaw) {
    const _Vector3<float>& hudScale = *reinterpret_cast<const _Vector3<float>*>(hudScaleRaw);
    if (m_AnimTimer <= 0.0f) return;
    // NOTE: no early-out on m_bHidden -- it is a UV frame selector, not a guard.

    MatrixManager& mm = MatrixManager::GetInstance();
    float flipSign = m_bFlipX ? -1.0f : 1.0f;

    // --- (1) Trail quads (press_indicate.tex, m_PressTex at +0x8C) ---
    // Active only during hold phase: PHASE_BOUNCE < m_AnimTimer < PHASE_HOLD_END
    // Binary @ 0x00163488: loop 4 quads with quartic alpha falloff.
    if (m_AnimTimer > PHASE_BOUNCE && m_AnimTimer < PHASE_HOLD_END &&
        m_PressTex.IsValid()) {

        float timer = m_AnimTimer;
        int rem = (int)(timer * TRAIL_TIMER_SCALE) % (int)TRAIL_MOD_DIV;

        // Global ones-Vec3 used for the trail quad scale (binary @ 0x001638f4-0x00163902).
        const _Vector3<float> ONES_VEC3(1.0f, 1.0f, 1.0f);

        for (int quad_index = 0; quad_index < 4; ++quad_index) {
            float frac = (float)quad_index + (float)rem / TRAIL_MOD_DIV;

            // Quartic alpha base: 255 + (frac - 3.0) * (-255.0) = 255 * (4 - frac)
            float alpha_base = TRAIL_ALPHA_MAX + (frac - 3.0f) * TRAIL_ALPHA_SLOPE;
            if (alpha_base < 0.0f)   alpha_base = 0.0f;
            if (alpha_base > TRAIL_ALPHA_MAX) alpha_base = TRAIL_ALPHA_MAX;

            float alpha;
            // ARM idiom ordering: check >= 0.85 first (no ramp), then > 2.0 (fade-out),
            // else fade-in ramp.
            if (timer >= TRAIL_FADE_IN_END) {
                // Hold region: no extra ramp
                alpha = alpha_base;
            } else if (timer > TRAIL_FADE_OUT_START) {
                // Fade-out tail: alpha_base * (1 - 4*(timer-2.0))
                alpha = alpha_base * (1.0f - 4.0f * (timer - TRAIL_FADE_OUT_START));
            } else {
                // Fade-in ramp: alpha_base * (timer - 0.60) * 4.0
                alpha = alpha_base * (timer - PHASE_BOUNCE) * 4.0f;
            }
            if (alpha < 0.0f)   alpha = 0.0f;
            if (alpha > 255.0f) alpha = 255.0f;

            Colour trailColour(255, 255, 255, (uint8_t)alpha);

            // Binary @ 0x001634a2-0x001634ba: scale = (2*frac)^2 uniform via
            // global Vec3(1,1,1) multiplied by quad-frac squared, NOT m_HalfWidth.
            float quadScale = (2.0f * frac) * (2.0f * frac);
            _Vector3<float> scaleVec = ONES_VEC3 * quadScale;

            mm.GetWorldStack().Reset();
            Matrix44 mat = Matrix44::MakeScale(scaleVec.x, scaleVec.y, scaleVec.z);
            mat.GlobalTranslate44(m_DrawPos);
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();

            m_PressTex->Set();
            // UV: full (0.0, 0.0, 1.0, 1.0) per DAT_001635c4=0.0, 0x3f800000=1.0
            Mortar::Mesh::DrawQuadUnCached(trailColour, 0.0f, 1.0f, 0.0f, 1.0f, NULL);
            m_PressTex->UnSet();
        }
    }

    // --- (2) Arrow (swipe_fruit_begin.tex, m_Texture at +0x74) ---
    // m_bHidden selects UV frame:
    //   0 -> u0 = 0.0 * 0.5 = 0.0, u1 = 0.0 * 0.5 + 0.5 = 0.5
    //   1 -> u0 = 1.0 * 0.5 = 0.5, u1 = 1.0 * 0.5 + 0.5 = 1.0
    if (m_Texture.IsValid()) {
        float arrow_u0 = m_bHidden * 0.5f;
        float arrow_u1 = m_bHidden * 0.5f + 0.5f;

        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(
            flipSign * ARROW_SCALE,
            ARROW_SCALE,
            1.0f);
        // Binary @ 0x00163554-0x00163562: offset Vec3 multiplied by ARROW_SCALE
        // (96.0), and result computed as drawAt = m_DrawPos - offset.
        _Vector3<float> offset(flipSign * -0.125f * ARROW_SCALE,
                               -0.40625f * ARROW_SCALE,
                               0.0f);
        _Vector3<float> drawAt = m_DrawPos - offset;
        mat.GlobalTranslate44(drawAt);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        m_Texture->Set();
        Mortar::Mesh::DrawQuadUnCached(m_Colour, arrow_u0, arrow_u1, 0.0f, 1.0f, NULL);
        m_Texture->UnSet();
    }
}
