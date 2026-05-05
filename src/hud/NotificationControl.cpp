// Analysed: 2026-05-03T00:00
// NotificationControl — HUD popup for achievement unlock / score notifications.
// Binary @ 0x00152ed0 (ctor) / 0x00152a00 (Update) / 0x001531f8 (Draw).

#include "NotificationControl.h"
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

    m_AchIcon = icon;

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
        if (g && g->pGameSound) {
            g->pGameSound->SFXPlay("achievement", 1.0f, 1.0f);
        }
    }

    // Measure text width and scale down if exceeds maxWidth
    Game* g = Game::GetInstance();
    if (g && g->pFontMain.IsValid()) {
        float measured = g->pFontMain->MeasureWidth(m_TextScale, m_DisplayName);
        if (measured > maxWidth) {
            m_TextScale *= maxWidth / measured;
        }
    }

    m_LayerFlags = 8;

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
void NotificationControl::Draw(const Vec3& hudScale, int layerMask) {
    (void)layerMask;

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
                float bw = (float)bannerTex->m_Width;
                float bh = (float)bannerTex->m_Height;
                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(bw, bh, 1.0f);
                mat.GlobalTranslate44(pos);
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, bannerTex->m_TexId);
                Colour col(255, 255, 255, 255);
                g->renderer.DrawQuad(col, 0.0f, 0.0f, 1.0f, 1.0f);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }

        // Icon quad
        if (m_AchIcon.IsValid()) {
            Mortar::Texture* iconTex = m_AchIcon.Get();
            if (iconTex) {
                float iw = (float)iconTex->m_Width;
                float ih = (float)iconTex->m_Height;
                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(iw, ih, 1.0f);
                // TODO: exact icon offset from binary DAT not yet resolved
                // DIFFERS: offset placeholder until DAT constants are RE'd
                Vec3 iconPos(pos.x, pos.y - 20.0f, pos.z);
                mat.GlobalTranslate44(iconPos);
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, iconTex->m_TexId);
                Colour col(255, 255, 255, 255);
                g->renderer.DrawQuad(col, 0.0f, 0.0f, 1.0f, 1.0f);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }

        // Name text
        // TODO: exact x/y offsets from binary not yet resolved
        if (g->pFontMain.IsValid()) {
            Colour col(255, 255, 255, 255);
            Vec3 textPos(pos.x + 10.0f, pos.y, pos.z);
            g->pFontMain->DrawString(m_TextScale, 1.0f, 0.0f,
                m_DisplayName, textPos, col, Mortar::FONT_ALIGN_LEFT);
        }

        // Points text (right-aligned)
        if (m_PointsText[0] != '\0' && g->pFontMain.IsValid()) {
            Colour col(255, 255, 255, 255);
            // TODO: exact right-align offset from binary not yet resolved
            Vec3 ptPos(pos.x + 160.0f, pos.y, pos.z);
            g->pFontMain->DrawString(m_TextScale, 1.0f, 0.0f,
                m_PointsText, ptPos, col, Mortar::FONT_ALIGN_RIGHT);
        }

    } else if (m_NotifType == Type_Named) {
        // --- Named notification (achievement unlock banner) ---

        // Unlock-banner quad
        // TODO: draw s_unlockBanner quad when banner texture is loaded
        if (s_unlockBanner.IsValid()) {
            Mortar::Texture* bannerTex = s_unlockBanner.Get();
            if (bannerTex) {
                float bw = (float)bannerTex->m_Width;
                float bh = (float)bannerTex->m_Height;
                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(bw, bh, 1.0f);
                mat.GlobalTranslate44(pos);
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, bannerTex->m_TexId);
                Colour col(255, 255, 255, 255);
                g->renderer.DrawQuad(col, 0.0f, 0.0f, 1.0f, 1.0f);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }

        // Larger icon quad
        if (m_AchIcon.IsValid()) {
            Mortar::Texture* iconTex = m_AchIcon.Get();
            if (iconTex) {
                // Binary: larger scale for named achievement icon than numeric
                float iw = (float)iconTex->m_Width  * 1.5f;
                float ih = (float)iconTex->m_Height * 1.5f;
                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(iw, ih, 1.0f);
                // TODO: exact icon offset from binary DAT not yet resolved
                Vec3 iconPos(pos.x, pos.y - 24.0f, pos.z);
                mat.GlobalTranslate44(iconPos);
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, iconTex->m_TexId);
                Colour col(255, 255, 255, 255);
                g->renderer.DrawQuad(col, 0.0f, 0.0f, 1.0f, 1.0f);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }

        // Name text only (no points text for named type)
        if (g->pFontMain.IsValid()) {
            Colour col(255, 255, 255, 255);
            Vec3 textPos(pos.x + 10.0f, pos.y, pos.z);
            g->pFontMain->DrawString(m_TextScale, 1.0f, 0.0f,
                m_DisplayName, textPos, col, Mortar::FONT_ALIGN_LEFT);
        }
    }
}

// STUB: NotificationControl::Init -- binary @ 0x???? (TODO RE)
void NotificationControl::Init() {}

// STUB: NotificationControl::Release -- binary @ 0x???? (TODO RE)
void NotificationControl::Release() {}

// STUB: NotificationControl::Reset -- binary @ 0x???? (TODO RE)
void NotificationControl::Reset() {}

// STUB: NotificationControl::PreDraw(float*) -- binary @ 0x???? (TODO RE)
void NotificationControl::PreDraw(float* /*viewVec*/) {}

// STUB: NotificationControl::Draw(float*) -- binary @ 0x???? (TODO RE)
void NotificationControl::Draw(float* /*viewVec*/) {}
