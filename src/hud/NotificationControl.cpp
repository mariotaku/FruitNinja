// Analysed: 2026-05-03T00:00
// NotificationControl — HUD popup for achievement unlock / score notifications.
// Binary @ 0x00152ed0 (ctor) / 0x00152a00 (Update) / 0x001531f8 (Draw).

#include "NotificationControl.h"
#include "hud/HUDLayer.h"
#include "Game.h"
#include "render/MatrixManager.h"
#include "render/Font.h"
#include "math/Matrix44.h"
#include "math/MathUtil.h"
#include "audio/GameSound.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include "game/GameWork.h"

// File-scope banner texture statics.
// TODO: load notification banner textures — loader function not yet identified in binary.
static Mortar::SmartPtr<Mortar::Texture> s_banner;        // numeric-type banner (notification_banner.tex or similar)
static Mortar::SmartPtr<Mortar::Texture> s_unlockBanner;  // named-type unlock banner

// Slide animation constants from binary.
// Binary: slide-in phase 0..0.2s, settled 0.2..2.7s, slide-out 2.7..2.9s, remove >=2.9s.
static const float NOTIF_SLIDE_IN_END  = 0.2f;   // DAT from binary (ctor state machine)
static const float NOTIF_SETTLE_END    = 2.7f;
static const float NOTIF_SLIDE_OUT_END = 2.9f;

// Y positions from binary (origin = settled target position, slide comes from off-screen)
// TODO: exact DAT addresses not resolved — use placeholder positions until RE'd.
// DIFFERS: original DAT constants not yet extracted; these are structural placeholders.
static const float NOTIF_Y_SETTLED   = 195.0f;  // settled Y in centered coords (DIFFERS: original DAT unknown)
static const float NOTIF_Y_OFFSCREEN = 260.0f;  // Y when fully off-screen   (DIFFERS: original DAT unknown)

// Interval between particle spawns during unlock flash (binary: every 0.125s, first 0.5s)
static const float NOTIF_PARTICLE_INTERVAL = 0.125f;
static const float NOTIF_PARTICLE_WINDOW   = 0.5f;

// Binary @ 0x00152ed0
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
        Game* g = Game::GetInstance();
        if (g && game_work.mGameSound) {
            game_work.mGameSound->SFXPlay("achievement", 1.0f, 1.0f);
        }
    }

    // Measure text width and scale down if exceeds maxWidth
    Game* g = Game::GetInstance();
    if (g && game_work.pFontMain.IsValid()) {
        float measured = game_work.pFontMain->MeasureWidth(m_TextScale, m_DisplayName);
        if (measured > maxWidth) {
            m_TextScale *= maxWidth / measured;
        }
    }

    m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;

    // Initial position: off-screen above
    pos.x = NOTIF_Y_OFFSCREEN;
    pos.y = 0.0f;
    pos.z = 0.0f;
}

NotificationControl::~NotificationControl() {}

// Binary @ 0x00152a00
// 4-phase state machine:
//   0.0..0.2s  slide-in  (Y interpolates from off-screen to settled with t^2 * yDelta)
//   0.2..2.7s  settled rest
//   2.7..2.9s  slide-out (t = (timer - 2.7) / -0.2 + 1.0, then t^2 * yDelta)
//   >=2.9s     m_bPendingRemoval = true
void NotificationControl::Update(float dt) {
    m_StateTimer += dt;

    float yDelta = NOTIF_Y_SETTLED - NOTIF_Y_OFFSCREEN;

    if (m_StateTimer < NOTIF_SLIDE_IN_END) {
        float t = m_StateTimer / NOTIF_SLIDE_IN_END;
        pos.x = NOTIF_Y_OFFSCREEN + t * t * yDelta;
    } else if (m_StateTimer < NOTIF_SETTLE_END) {
        pos.x = NOTIF_Y_SETTLED;
    } else if (m_StateTimer < NOTIF_SLIDE_OUT_END) {
        float t = (m_StateTimer - NOTIF_SETTLE_END) / -(NOTIF_SLIDE_IN_END) + 1.0f;
        pos.x = NOTIF_Y_OFFSCREEN + t * t * yDelta;
    } else {
        m_bPendingRemoval = 1;
        return;
    }

    // Type_Named only: spawn "achievement_unlock" particle emitter every 0.125s in first 0.5s
    if (m_NotifType == Type_Named && m_StateTimer < NOTIF_PARTICLE_WINDOW) {
        // TODO: spawn "achievement_unlock" particle emitter at pos
        // Requires Mortar::ActorManager / ParticleEmitter to be ported.
        // Binary: Mortar::ActorManager::GetInstance()->SpawnEmitter("achievement_unlock", pos, ...)
        (void)NOTIF_PARTICLE_INTERVAL;
    }
}

// Binary @ 0x001531f8
// Per-type render path:
//   Type 1 (numeric): banner quad + icon + name text + points text right-aligned.
//   Type 2 (named):   unlock-banner + larger icon + name text only.
void NotificationControl::Draw(float* hudScaleRaw) {
    const Vec3& hudScale = *reinterpret_cast<const Vec3*>(hudScaleRaw);

    Game* g = Game::GetInstance();
    if (!g) return;

    MatrixManager& mm = MatrixManager::GetInstance();

    if (m_NotifType == Type_Numeric) {
        // --- Numeric notification (score/points pop-up) ---

        // Banner quad
        // TODO: draw s_banner quad via Mesh::DrawQuadUnCached when banner texture is loaded
        // Binary: MatrixManager::SetCurrentMatrix(Scale(banner_w, banner_h)) then DrawQuad
        if (s_banner.IsValid()) {
            Mortar::Texture* bannerTex = s_banner.Get();
            if (bannerTex) {
                float bw = (float)bannerTex->GetWidth();
                float bh = (float)bannerTex->GetHeight();
                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(bw, bh, 1.0f);
                mat.GlobalTranslate44(pos);
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();
                bannerTex->Set();
                Colour col(255, 255, 255, 255);
                g->renderer.DrawQuad(col, 0.0f, 0.0f, 1.0f, 1.0f);
                bannerTex->UnSet();
            }
        }

        // Icon quad
        // ASM-verified: 2026-05-18 binary @ 0x001531f8 (re-analyst)
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
                g->renderer.DrawQuad(col, 0.0f, 0.0f, 1.0f, 1.0f);
                iconTex->UnSet();
            }
        }

        // Name text
        // ASM-verified: 2026-05-18 binary @ 0x001531f8 (re-analyst)
        if (game_work.pFontMain.IsValid()) {
            Colour col(50, 50, 50, 255);
            Vec3 textPos(pos.x + 18.0f, pos.y, pos.z);
            game_work.pFontMain->DrawString(m_TextScale, 1.0f, 0.0f,
                m_DisplayName, textPos, col, 0x0C);
        }

        // Points text (right-aligned)
        // ASM-verified: 2026-05-18 binary @ 0x001531f8 (re-analyst)
        if (m_PointsText[0] != '\0' && game_work.pFontMain.IsValid()) {
            Colour col(50, 50, 50, 255);
            Vec3 ptPos(pos.x + 186.0f, pos.y, pos.z);
            game_work.pFontMain->DrawString(m_TextScale, 1.0f, 0.0f,
                m_PointsText, ptPos, col, 0x0C);
        }

    } else if (m_NotifType == Type_Named) {
        // --- Named notification (achievement unlock banner) ---

        // Unlock-banner quad
        // TODO: draw s_unlockBanner quad when banner texture is loaded
        if (s_unlockBanner.IsValid()) {
            Mortar::Texture* bannerTex = s_unlockBanner.Get();
            if (bannerTex) {
                float bw = (float)bannerTex->GetWidth();
                float bh = (float)bannerTex->GetHeight();
                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(bw, bh, 1.0f);
                mat.GlobalTranslate44(pos);
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();
                bannerTex->Set();
                Colour col(255, 255, 255, 255);
                g->renderer.DrawQuad(col, 0.0f, 0.0f, 1.0f, 1.0f);
                bannerTex->UnSet();
            }
        }

        // Larger icon quad
        // ASM-verified: 2026-05-18 binary @ 0x001531f8 (re-analyst)
        if (m_Texture.IsValid()) {
            Mortar::Texture* iconTex = m_Texture.Get();
            if (iconTex) {
                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(32.0f, 32.0f, 32.0f);
                Vec3 iconPos(pos.x, pos.y + 16.0f, pos.z);
                mat.GlobalTranslate44(iconPos);
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();
                iconTex->Set();
                Colour col(255, 255, 255, 255);
                g->renderer.DrawQuad(col, 0.0f, 0.0f, 1.0f, 1.0f);
                iconTex->UnSet();
            }
        }

        // Name text only (no points text for named type)
        // ASM-verified: 2026-05-18 binary @ 0x001531f8 (re-analyst)
        if (game_work.pFontMain.IsValid()) {
            Colour col(50, 50, 50, 255);
            Vec3 textPos(pos.x + 18.0f, pos.y + 16.0f, pos.z);
            game_work.pFontMain->DrawString(m_TextScale, 1.0f, 0.0f,
                m_DisplayName, textPos, col, 0x0C);
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

