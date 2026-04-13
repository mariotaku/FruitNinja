//
// BombBlast — shockwave ring. Ported from binary 0x171170..0x171aa0.
// See docs/entities/bomb-blast.md.
//
// Analysed: 2026-04-13T22:00
//

#include "BombBlast.h"
#include "ActorManager.h"
#include "Game.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "asset/TextureManager.h"
#include "util/SmartPtr.h"
#include "math/Matrix44.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>

// Binary constants (docs/entities/bomb-blast.md)
static const float RADIUS_GROWTH = 100.0f;   // DAT_0017120c
static const float SCALE_GROWTH  = 2500.0f;  // DAT_00171210
static const float BLAST_LIFE    = 3.0f;
static const float BLAST_Z       = 0.0f;     // field_0x6c initial (Z plane)

// Global texture for the shockwave ring. Binary loads from
// DrawActiveBlasts using DAT at +0x108 of game data; the actual filename
// needs resolution. For now we reuse bomb_explode.tex.
static SmartPtr<Mortar::Texture> g_BlastTex;

// --------------------------------------------------------------------------

BombBlast::BombBlast()
    : m_BlastRadius(0.0f)
    , m_Scale(0.0f)
    , m_Angle(0)
    , m_PosA(0, 0, 0)
    , m_PosB(0, 0, 0)
    , m_Vel1(0, 0, 0)
    , m_Vel2(0, 0, 0)
    , m_Lifetime(0.0f)
{
    entityType = 4;
    // Binary ctor clears 0x11 (collision + kill); we start without both.
    flags &= ~0x11;
}

BombBlast::~BombBlast() {}

void BombBlast::LoadContent() {
    if (!g_BlastTex.IsValid()) {
        // Binary uses its own blast texture; fall back to bomb_explode.tex
        // until the exact filename is resolved (see docs TODO).
        g_BlastTex = Mortar::TextureManager::LoadLocalisedTexture("bomb_explode.tex");
    }
}

void BombBlast::ReleaseContent() {
    g_BlastTex.Clear();
}

// Matches BombBlast::Init (0x1718ac)
void BombBlast::Init(int p1, int p2, int p3) {
    (void)p1; (void)p2; (void)p3;

    active = true;
    flags &= ~0x11;

    pos.z = BLAST_Z;

    // Random 16-bit angle in [0, 0xFFFF). Binary uses Random::Rand32(524287)
    // then normalises to 360° * 182 — we just use a fresh rand.
    m_Angle = (uint16_t)(rand() & 0xFFFF);

    const float rad = (float)m_Angle * 6.2831853f / 65536.0f;
    const float c = cosf(rad);
    const float s = sinf(rad);

    m_Vel1 = Vec3(c, s, 0.0f) * 0.5f;
    // Perpendicular = angle + 0x3FFC (~90°)
    const float rad2 = rad + 1.5707963f;
    m_Vel2 = Vec3(cosf(rad2), sinf(rad2), 0.0f);

    m_PosA = m_Vel1;
    m_PosB = m_Vel2;

    m_BlastRadius = 0.0f;
    m_Scale = 5.0f;
    m_Lifetime = 0.0f;

    m_Col.radius = 0.0f; // doesn't collide
}

// Matches BombBlast::Update (0x171170)
void BombBlast::Update(float dt) {
    if (!active) return;

    m_BlastRadius += dt * RADIUS_GROWTH;
    m_Lifetime    += dt;

    // Binary scales m_PosA / m_PosB outward by growing factor. Use the
    // lifetime-scaled blast radius as the outward factor.
    m_PosA = m_Vel1 * m_BlastRadius;
    m_PosB = m_Vel2 * m_BlastRadius;

    m_Scale += SCALE_GROWTH * dt;

    if (m_Lifetime >= BLAST_LIFE) {
        flags |= 0x10;   // kill
    }
}

// Binary's vtable Draw for BombBlast is empty (0x171034). Rendering happens
// via the global DrawActiveBlasts pass from GameDraw. We keep Entity::Draw
// empty here and rely on DrawActiveBlasts below.
void BombBlast::Draw(Renderer& r) { (void)r; }

// Matches DrawActiveBlasts (0x171aa0) + DrawBlast (0x171354).
// Draws a textured quad at each active blast's position, scaled by
// m_BlastRadius and faded by age.
void BombBlast::DrawActiveBlasts() {
    ActorManager* am = ActorManager::GetInstance();
    if (!am) return;

    if (!g_BlastTex.IsValid()) {
        LoadContent();
        if (!g_BlastTex.IsValid()) return;
    }

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();

    g_BlastTex->Set();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive

    for (auto it = am->entities.begin(); it != am->entities.end(); ++it) {
        Entity* e = *it;
        if (!e || e->entityType != 4 || !e->IsActive()) continue;

        BombBlast* blast = static_cast<BombBlast*>(e);

        // Fade alpha over lifetime.
        float t = blast->m_Lifetime / BLAST_LIFE;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        const uint8_t alpha = (uint8_t)((1.0f - t) * 255.0f);
        const Colour tint(255, 255, 255, alpha);

        // Radius grows from 0 → 100*BLAST_LIFE = 300.
        const float r = blast->m_BlastRadius;
        if (r <= 0.0f) continue;

        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(r, r, 1.0f);
        mat.GlobalTranslate44(Vec3(blast->pos.x, blast->pos.y, blast->pos.z));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        if (Renderer* rr = Renderer::GetInstance()) {
            rr->DrawQuad(tint);
        }
    }

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    g_BlastTex->UnSet();
}

// Matches RemoveFlashEntities (0x169ca0) — called by UpdateBombHit when
// Game.bombHitTimer drops below 1.55s.
void BombBlast::RemoveAll() {
    ActorManager* am = ActorManager::GetInstance();
    if (!am) return;
    for (auto it = am->entities.begin(); it != am->entities.end(); ++it) {
        Entity* e = *it;
        if (e && e->entityType == 4) {
            e->flags |= 0x11;   // kill + skip
        }
    }
}
