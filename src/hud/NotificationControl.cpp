// NotificationControl — HUD popup for achievement unlock / score notifications.
// v1.6.1 NotificationControl::{ctor} @ 0x001a4428 / Update @ 0x001a3c7c / Draw @ 0x001a4860.

#include "NotificationControl.h"
#include "hud/HUDLayer.h"
#include "Game.h"
#include "render/MatrixManager.h"
#include "render/Font.h"
#include "math/Matrix44.h"
#include "math/MathUtil.h"
#include "audio/GameSound.h"
#include "particle/PSPParticleManager.h"
#include "util/StringHash.h"
#include "math/Random.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include "game/GameWork.h"

// v1.6.1 NotificationControl::s_banner / s_unlockBanner — class-static preamble
// banner textures, assigned by AchievementManager::LoadAchievementInfo @0x00118198.
Mortar::SmartPtr<Mortar::Texture> NotificationControl::s_banner;
Mortar::SmartPtr<Mortar::Texture> NotificationControl::s_unlockBanner;

// Slide animation constants from binary.
// Binary: slide-in phase 0..0.2s, settled 0.2..2.7s, slide-out 2.7..2.9s, remove >=2.9s.
static const float NOTIF_SLIDE_IN_END  = 0.2f;   // DAT from binary (ctor state machine)
static const float NOTIF_SETTLE_END    = 2.7f;
static const float NOTIF_SLIDE_OUT_END = 2.9f;

// v1.6.1 NotificationControl::Update @0x001a3c7c: pos.x is pinned constant; only pos.y animates.
static const float NOTIF_X = -95.0f;

// Per-NotificationType Y constants (v1.6.1 NotificationControl::Update @0x001a3c7c).
// Type_Numeric offscreen/settled/delta = 184.0f / 147.0f / -37.0f
// Type_Named   offscreen/settled/delta = 192.0f / 128.0f / -64.0f

// v1.6.1 NotificationControl::NotificationControl @0x001a4428
NotificationControl::NotificationControl(const char* name, int points,
                                          Mortar::SmartPtr<Mortar::Texture> icon,
                                          NotificationType type)
    : m_TextScale(16.0f)
    , m_StateTimer(0.0f)
    , m_Points(points)
    , m_NotifType((uint8_t)type)
{
    _pad[0] = _pad[1] = _pad[2] = 0;

    // Binary stores icon into the base HUDControl3d::m_Texture slot (+0x74).
    m_Texture = icon;

    // OS_SPrintf(m_DisplayName, 128, "%s", name) then ASCII tolower→toupper inline.
    // Binary: copies name into buffer, then upper-cases each character.
    if (name) {
        snprintf(m_DisplayName, sizeof(m_DisplayName), "%s", name);
        for (int i = 0; m_DisplayName[i] != '\0'; ++i) {
            m_DisplayName[i] = (char)toupper((unsigned char)m_DisplayName[i]);
        }
    } else {
        m_DisplayName[0] = '\0';
    }

    float maxWidth = 185.0f;

    if (points >= 0 && type == NotificationControl::Type_Numeric) {
        snprintf(m_PointsText, sizeof(m_PointsText), "%d", points);
        maxWidth = 170.0f;
    } else {
        m_PointsText[0] = '\0';
    }

    // Type_Named: play "achievement" SFX
    if (type == NotificationControl::Type_Named) {
        // v1.6.1 NotificationControl::NotificationControl @0x001a4428: unguarded
        // GameSound::SFXPlay("achievement", 1.0, 1.0).
        game_work.mGameSound->SFXPlay("achievement", 1.0f, 1.0f);
    }

    // Measure text width and scale down if exceeds maxWidth.
    // Binary reads game_work.pM_Fonts[1] unguarded @0x001a4428.
    float measured = game_work.pFontMain->MeasureWidth(m_TextScale, m_DisplayName);
    if (measured > maxWidth) {
        m_TextScale *= maxWidth / measured;
    }

    // v1.6.1 NotificationControl::NotificationControl @0x001a4178/0x001a4428:
    // operator_new(0xc8)=200B == sizeof(BakedStringBox); allocates the TTF label
    // used for the name text in Draw() (replaces the bitmap Font::DrawString path).
    m_pBakedString = new Mortar::BakedStringBox(
        game_work.m_pTTFFontMain, 10.0f, 145, 24, (Mortar::ALIGNMENT_TYPE)0xf, 2, 3);
    m_pBakedString->SetText(m_DisplayName);
    m_pBakedString->SetHorizontalLineSpacing(-1);
    // TODO verify alpha: T_851's alpha channel for this Colour(50,50,50,?) wasn't
    // isolated in the RE pass; using 255 (opaque) to match every other
    // Colour(50,50,50,255) use in this file's Draw().
    m_pBakedString->SetColour(Colour(50, 50, 50, 255), false);

    // v1.6.1 NotificationControl::NotificationControl @0x001a4178/0x001a4428:
    // mov r3,#0x400; str r3,[r4,#0x34] -- HUDControl::m_LayerFlags = HUD_LAYER_FADE_MODAL.
    m_LayerFlags = Mortar::HUD_LAYER_FADE_MODAL;
    // Binary ctor @0x001a4428 does not write pos at all -- Update() (@0x001a3c7c)
    // always runs before the first Draw() and sets pos unconditionally.
}

// v1.6.1 ~NotificationControl @0x001a4764: ~BakedStringBox() then operator_delete, then null.
NotificationControl::~NotificationControl() {
    delete m_pBakedString;
    m_pBakedString = nullptr;
}

// v1.6.1 NotificationControl::Update @0x001a3c7c
// 4-phase state machine, animates pos.y only (pos.x pinned at NOTIF_X):
//   0.0..0.2s  slide-in  (Y interpolates from off-screen to settled with t^2 * yDelta)
//   0.2..2.7s  settled rest
//   2.7..2.9s  slide-out (t = (timer - 2.7) / -0.2 + 1.0, then t^2 * yDelta)
//   >=2.9s     clamp m_StateTimer to 2.9, m_bPendingRemoval = true -- NO early return;
//              falls through and recomputes pos (t collapses to 0, snapping to offscreen Y)
//              and still runs the Type_Named particle-window gate below.
void NotificationControl::Update(float dt) {
    float offscreenY, settledY, deltaY;
    if (m_NotifType == Type_Named) {
        offscreenY = 192.0f; settledY = 128.0f; deltaY = -64.0f;
    } else {
        offscreenY = 184.0f; settledY = 147.0f; deltaY = -37.0f;
    }

    float oldTimer = m_StateTimer;
    m_StateTimer += dt;

    pos.x = NOTIF_X;
    pos.z = 0.0f;

    if (m_StateTimer <= NOTIF_SETTLE_END) {
        if (m_StateTimer >= NOTIF_SLIDE_IN_END) {
            pos.y = settledY;
        } else {
            float t = m_StateTimer / NOTIF_SLIDE_IN_END;
            pos.y = offscreenY + t * t * deltaY;
        }
    } else {
        if (m_StateTimer >= NOTIF_SLIDE_OUT_END) {
            m_StateTimer = NOTIF_SLIDE_OUT_END;
            m_bPendingRemoval = 1;
        }
        float t = (m_StateTimer - NOTIF_SETTLE_END) / -(NOTIF_SLIDE_IN_END) + 1.0f;
        pos.y = offscreenY + t * t * deltaY;
    }

    // Type_Named only: spawn "confettif" particle emitter on each 1/8s tick crossing
    // within the first 0.5s (floor(oldTimer*8) != floor(newTimer*8) && floor(newTimer*8) < 4).
    if (m_NotifType == Type_Named) {
        int oldTick = (int)(oldTimer * 8.0f);
        int newTick = (int)(m_StateTimer * 8.0f);
        if (oldTick != newTick && newTick < 4) {
            // ASM-spec v1.6.1 NotificationControl::Update @0x001a3c7c: confetti spawn block.
            // Particle name is "confettif" (9 chars, string @0x0028258f; defined in
            // Data/particles/particles_fast.xml / particles_slow.xml). X base uses
            // oldTick (binary r6) so the three spawns walk -110/-10/+90 across the
            // 257-wide banner. RNG draw ORDER is load-bearing for global-stream
            // fidelity: Rand32(0x7FFFF), Rand32(5), field writes, then Rand32(0xe38).
            // Pool constants: 524287.0f @0x001a3fbc, 160.0f @0x001a3fc0, 100.0f @0x001a3fc4.
            static const uint32_t s_ConfettiHash = StringHash("confettif");
            PSPParticleEmitter* em = PSPParticleManager::GetInstance().AddEmitter(s_ConfettiHash, 0, false);
            if (em) {
                float rx = (float)Math::g_Random.Rand32(0x7FFFF);
                float ry = (float)Math::g_Random.Rand32(5);
                em->m_Pos = _Vector3<float>((rx / 524287.0f) * 20.0f - 10.0f - 100.0f + (float)(oldTick * 100),
                                            160.0f - ry - 25.0f, 0.0f);
                em->m_bUpdateWhenPaused = 1;   // +0x4C
                em->m_SizeScale = 1.0f;        // +0x28
            }
            // Deliberate RNG burn @0x001a3f74, result discarded -- runs even when
            // AddEmitter returns null (binary beq @0x001a3ecc jumps straight here).
            // NOT dead code: keeps the global RNG stream in step with the binary.
            Math::g_Random.Rand32(0xe38);
        }
    }
}

// v1.6.1 NotificationControl::Draw @0x001a4860
// Per-type render path:
//   Type 1 (numeric): banner quad + icon + name text + points text right-aligned.
//   Type 2 (named):   unlock-banner + larger icon + name text only.
void NotificationControl::Draw(float* hudScaleRaw) {
    const _Vector3<float>& hudScale = *reinterpret_cast<const _Vector3<float>*>(hudScaleRaw);

    // Binary @0x001a4860 has no Game::GetInstance in the body; the port needs the
    // singleton only to reach the port-side Renderer (Mortar::Mesh::DrawQuadUnCached
    // in the binary).
    Game* g = Game::GetInstance();

    MatrixManager& mm = MatrixManager::GetInstance();

    if (m_NotifType == Type_Numeric) {
        // --- Numeric notification (score/points pop-up) ---

        // Banner quad
        // ASM-spec v1.6.1 NotificationControl::Draw @0x001a4860: banner quad is
        // screen-centered (translate.x = 0.0f, NOT pos.x -- pos.x=-95 is the
        // icon/text anchor only), translate.y = pos.y - 16.0f, scale is the
        // literal 257.0f x 65.0f (not bannerTex->GetWidth()/GetHeight()).
        // s_banner is a 2-row stacked texture: top half (v 0..0.46875) when no
        // points text is shown, bottom half (v 0.5..0.96875) when it is.
        if (s_banner.IsValid()) {
            Mortar::Texture* bannerTex = s_banner.Get();
            if (bannerTex) {
                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(257.0f, 65.0f, 1.0f);
                mat.GlobalTranslate44(_Vector3<float>(0.0f, pos.y - 16.0f, pos.z));
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();
                bannerTex->Set();
                Colour col(255, 255, 255, 255);
                bool showPoints = (m_PointsText[0] != '\0');
                float vMin = showPoints ? 0.5f : 0.0f;
                float vMax = showPoints ? 0.96875f : 0.46875f;
                g->renderer.DrawQuad(col, 0.0f, 1.0f, vMin, vMax);
                bannerTex->UnSet();
            }
        }

        // Icon quad
        // ASM-verified: 2026-05-18 v1.6.1 NotificationControl::Draw @ 0x001a4860 (re-analyst)
        if (m_Texture.IsValid()) {
            Mortar::Texture* iconTex = m_Texture.Get();
            if (iconTex) {
                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(30.0f, 30.0f, 30.0f);
                mat.GlobalTranslate44(pos);
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();
                iconTex->Set();
                Colour col(255, 255, 255, 255);
                g->renderer.DrawQuad(col, 0.0f, 1.0f, 0.0f, 1.0f);
                iconTex->UnSet();
            }
        }

        // Name text -- TTF BakedStringBox path (replaces bitmap Font::DrawString).
        // v1.6.1 NotificationControl::Draw @0x001a4860
        if (m_pBakedString) {
            m_pBakedString->SetTranslation(_Vector3<float>(pos.x + 18.0f + 71.0f, pos.y + 1.0f, 0.0f), true);
            m_pBakedString->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, true);
        }

        // Points text (right-aligned)
        // ASM-verified: 2026-05-18 v1.6.1 NotificationControl::Draw @ 0x001a4860 (re-analyst)
        if (m_PointsText[0] != '\0' && game_work.pFontMain.IsValid()) {
            Colour col(50, 50, 50, 255);
            _Vector3<float> ptPos(pos.x + 186.0f, pos.y, pos.z);
            game_work.pFontMain->DrawString(m_TextScale, 1.0f, 0.0f,
                m_PointsText, ptPos, col, 0x0C);
        }

    } else if (m_NotifType == Type_Named) {
        // --- Named notification (achievement unlock banner) ---

        // Unlock-banner quad
        // ASM-spec v1.6.1 NotificationControl::Draw @0x001a4860: banner quad is
        // screen-centered (translate.x = 0.0f, NOT pos.x -- pos.x=-95 is the
        // icon/text anchor only), translate.y = pos.y (unchanged for Type_Named),
        // scale is the literal 257.0f x 65.0f (not bannerTex->GetWidth()/GetHeight()).
        // V range is 0.0 .. 0.984375, not the full 0..1.
        if (s_unlockBanner.IsValid()) {
            Mortar::Texture* bannerTex = s_unlockBanner.Get();
            if (bannerTex) {
                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(257.0f, 65.0f, 1.0f);
                mat.GlobalTranslate44(_Vector3<float>(0.0f, pos.y, pos.z));
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();
                bannerTex->Set();
                Colour col(255, 255, 255, 255);
                g->renderer.DrawQuad(col, 0.0f, 1.0f, 0.0f, 0.984375f);
                bannerTex->UnSet();
            }
        }

        // Larger icon quad
        // ASM-verified: 2026-05-18 v1.6.1 NotificationControl::Draw @ 0x001a4860 (re-analyst)
        if (m_Texture.IsValid()) {
            Mortar::Texture* iconTex = m_Texture.Get();
            if (iconTex) {
                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(32.0f, 32.0f, 32.0f);
                _Vector3<float> iconPos(pos.x, pos.y + 16.0f, pos.z);
                mat.GlobalTranslate44(iconPos);
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();
                iconTex->Set();
                Colour col(255, 255, 255, 255);
                g->renderer.DrawQuad(col, 0.0f, 1.0f, 0.0f, 1.0f);
                iconTex->UnSet();
            }
        }

        // Name text only (no points text for named type) -- TTF BakedStringBox path.
        // v1.6.1 NotificationControl::Draw @0x001a4860
        if (m_pBakedString) {
            m_pBakedString->SetTranslation(_Vector3<float>(pos.x + 18.0f + 73.0f, pos.y + 17.0f, 0.0f), true);
            m_pBakedString->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, true);
        }
    }
}

// ---- HUDControl3d lifecycle override bodies (RE'd against the vtable @ 0x1e9b80) ----
// Object vptr -> 0x1e9b80; slot order: dtor,dtor,Init,Release,Reset,BeginDraw,
// PreDraw,Draw,PreDrawOrder,DrawOrder,Update. Each body below is the exact binary thunk.

// Binary @ 0x001529EC -- thunk: ldr r3,[r0]; ldr r3,[r3,#0x10]; blx r3.
// vptr+0x10 == Reset slot, so Init() delegates to the virtual Reset().
void NotificationControl::Init() {
    Reset();
}

// Binary @ 0x00152DE0 -- adds r0,#0x74; movs r1,#0; blx SmartPtr<Texture>::SetPtr.
// Releases the icon texture held in the inherited m_Texture slot (+0x74).
void NotificationControl::Release() {
    m_Texture.SetPtr(nullptr);
}

// Binary @ 0x001529F8 -- bx lr (no-op).
void NotificationControl::Reset() {}

// Binary @ 0x001529FC -- bx lr (no-op).
void NotificationControl::PreDraw(float* /*viewVec*/) {}

