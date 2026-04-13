//
// SlashEntity — blade trail visual-only port (no collision, no combo, no ghost)
// Matches binary 0x17C82C..0x17E504. See SlashEntity.h for method addresses.
//
// Analysed: 2026-04-13T18:00
//

#include "SlashEntity.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "asset/TextureManager.h"
#include "Game.h"
#include <cstring>
#include <cmath>
#include <cstdio>

const float SlashEntity::POINT_SPACING         = 64.0f;   // DAT_0017d5fc
const float SlashEntity::MOVE_THRESH_ACTIVE    = 5.0f;    // sqrt(25)
const float SlashEntity::MOVE_THRESH_INACTIVE  = 50.0f;   // sqrt(DAT_0017d5f8 = 2500)

// Per-point trail width scale. Binary uses 9.0 × thicknessFactor; we fix
// thicknessFactor = 1.0 (would come from GetHeadThicknessScale in the binary).
static const float BLADE_HALF_WIDTH = 9.0f;

// --- Global content ---
static SmartPtr<Mortar::Texture> g_BladeTex;

// --- Global instance ---
SlashEntity* g_pSlashEntity = nullptr;

// ---------------------------------------------------------------------------
// Content load — matches LoadContent (0x17C948)
// ---------------------------------------------------------------------------
void SlashEntity::LoadContent() {
    if (!g_BladeTex.IsValid()) {
        g_BladeTex = Mortar::TextureManager::LoadLocalisedTexture("blade.tex");
        printf("[SlashEntity] LoadContent: blade.tex valid=%d\n", g_BladeTex.IsValid());
    }
}

void SlashEntity::ReleaseContent() {
    g_BladeTex.Clear();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
SlashEntity::SlashEntity()
    : m_PointCount(0)
    , m_HeadPos(-65535.0f, -65535.0f, -65535.0f)
    , m_TailPos(-65535.0f, -65535.0f, -65535.0f)
    , m_BladeDir(0.0f, 0.0f, 0.0f)
    , m_SpeedScale(0.0f)
    , m_bHasHead(false)
    , m_State(0)
{
    memset(m_Left,  0, sizeof(m_Left));
    memset(m_Right, 0, sizeof(m_Right));
}

SlashEntity::~SlashEntity() {
    Release();
}

void SlashEntity::Init() {
    m_PointCount = 0;
    m_HeadPos = Vec3(-65535.0f, -65535.0f, -65535.0f);
    m_TailPos = m_HeadPos;
    m_BladeDir = Vec3(0, 0, 0);
    m_SpeedScale = 0.0f;
    m_bHasHead = false;
    m_State = 0;

    // Pre-fill UV u for each vertex (tail→head = 0→0.98). Binary does this
    // in InitPoints; we can just zero and set u in RebuildGeometry each frame.
    for (int i = 0; i < MAX_VERTS; ++i) {
        m_Left[i].nx = 0;  m_Left[i].ny = 0;  m_Left[i].nz = 1.0f;
        m_Right[i].nx = 0; m_Right[i].ny = 0; m_Right[i].nz = 1.0f;
        m_Left[i].colour  = 0xFFFFFFFF;
        m_Right[i].colour = 0xFFFFFFFF;
    }
}

void SlashEntity::Release() {
    m_PointCount = 0;
}

// ---------------------------------------------------------------------------
// Touch ingestion — matches UpdateTouchDown (0x17D2E4) / AddPoint (0x17CE0C)
// ---------------------------------------------------------------------------
void SlashEntity::TouchDown(float x, float y) {
    Vec3 newPos(x, y, 0.0f);

    if (!m_bHasHead) {
        // First touch: initialise head/tail and state.
        m_HeadPos = newPos;
        m_TailPos = newPos;
        m_BladeDir = Vec3(0, 0, 0);
        m_bHasHead = true;
        m_State = 1;
        AddPoint(newPos, Vec3(0, 0, 0), 1.0f);
        return;
    }

    // Movement threshold: 5 units² = 25 when active, 50 units² = 2500 when
    // blade is inactive (matches binary DAT_0017d5f8 = 2500, hardcoded 25).
    const Vec3 delta = newPos - m_HeadPos;
    const float distSq = delta.x * delta.x + delta.y * delta.y;
    const float thresh = (m_State != 0) ? (MOVE_THRESH_ACTIVE * MOVE_THRESH_ACTIVE)
                                        : (MOVE_THRESH_INACTIVE * MOVE_THRESH_INACTIVE);
    if (distSq < thresh) return;

    // Interpolate intermediate points every POINT_SPACING = 64 units along
    // the movement vector. Matches binary's per-step stepSize=64.0 loop.
    const float dist = sqrtf(distSq);
    Vec3 dir(delta.x / dist, delta.y / dist, 0.0f);

    float travelled = POINT_SPACING;
    while (travelled < dist) {
        Vec3 step(m_HeadPos.x + dir.x * travelled,
                  m_HeadPos.y + dir.y * travelled, 0.0f);
        AddPoint(step, dir, 1.0f);
        travelled += POINT_SPACING;
    }

    // Final point at current touch position.
    AddPoint(newPos, dir, 1.0f);
    m_BladeDir = dir;
    m_State = 1;
}

void SlashEntity::TouchUp() {
    // Matches binary state-machine bit shift: 1 → 2 (deactivating).
    // Trail fades out via RebuildGeometry's decay, then we reset on the
    // following TouchDown.
    if (m_State == 1) m_State = 2;
    m_bHasHead = false;
}

// ---------------------------------------------------------------------------
// AddPoint — matches binary 0x17CE0C (simplified, no multiplayer flip, no
// ghost ring buffer, no per-point colour variation).
// ---------------------------------------------------------------------------
void SlashEntity::ShiftDown() {
    // Drop the two oldest vertices so we have room for two new ones.
    const int keep = m_PointCount - 2;
    if (keep > 0) {
        memmove(m_Left,  m_Left  + 2, sizeof(QUADCUSTOMVERTEX) * keep);
        memmove(m_Right, m_Right + 2, sizeof(QUADCUSTOMVERTEX) * keep);
    }
    m_PointCount -= 2;
    if (m_PointCount < 0) m_PointCount = 0;
}

void SlashEntity::AddPoint(const Vec3& pos, const Vec3& dir, float thickness) {
    if (m_PointCount >= MAX_POINTS) ShiftDown();

    // Perpendicular to the blade direction in XY plane.
    // Binary uses CosIdx(angle) / SinIdx(angle) where angle = Atan2Idx(-dir.x, dir.y).
    // Equivalent: perp = (-dir.y, dir.x) rotated 90° CCW.
    Vec3 perp(-dir.y, dir.x, 0.0f);
    const float halfW = BLADE_HALF_WIDTH * thickness;
    perp.x *= halfW;
    perp.y *= halfW;

    const int i0 = m_PointCount;
    const int i1 = m_PointCount + 1;

    // Two vertex pairs — centre + pair offset toward strip edges.
    // Binary: left[n]   = pos - perp  (outer edge)
    //         left[n+1] = pos + perp  (inner / centre)
    //         right[n]  = pos - perp
    //         right[n+1] = pos + perp
    // For a simple double-sided strip we treat left/right as mirrored halves
    // so the tri-strip produces a quad between consecutive points.
    m_Left [i0].x = pos.x - perp.x;  m_Left [i0].y = pos.y - perp.y;  m_Left [i0].z = pos.z;
    m_Left [i1].x = pos.x;           m_Left [i1].y = pos.y;           m_Left [i1].z = pos.z;
    m_Right[i0].x = pos.x;           m_Right[i0].y = pos.y;           m_Right[i0].z = pos.z;
    m_Right[i1].x = pos.x + perp.x;  m_Right[i1].y = pos.y + perp.y;  m_Right[i1].z = pos.z;

    // Strip-edge V: left strip v ∈ {0, 0.5}, right strip v ∈ {0.5, 1}.
    m_Left [i0].v = 0.0f; m_Left [i1].v = 0.5f;
    m_Right[i0].v = 0.5f; m_Right[i1].v = 1.0f;

    // Colour solid white; fade handled by RebuildGeometry via vertex alpha.
    m_Left [i0].colour = 0xFFFFFFFF;
    m_Left [i1].colour = 0xFFFFFFFF;
    m_Right[i0].colour = 0xFFFFFFFF;
    m_Right[i1].colour = 0xFFFFFFFF;

    m_PointCount += 2;
    m_HeadPos = pos;
    if (m_PointCount == 2) m_TailPos = pos;
    m_SpeedScale = 1.0f;
}

// ---------------------------------------------------------------------------
// Per-frame geometry rebuild — matches UpdatePoints (0x17B92C).
// Simplified: re-writes U coordinate per vertex (tail=0 → head=0.98) and
// decays the trail via shift-drop when m_SpeedScale falls to zero.
// ---------------------------------------------------------------------------
void SlashEntity::RebuildGeometry() {
    if (m_PointCount < 2) return;

    // Uniform U across trail segments. Binary accumulates real arc length
    // into a 100-float stack array; we linearise for now.
    const int pairs = m_PointCount / 2;
    for (int p = 0; p < pairs; ++p) {
        const float u = 0.98f * ((float)p / (float)(pairs > 1 ? pairs - 1 : 1));
        m_Left [p*2    ].u = u; m_Left [p*2 + 1].u = u;
        m_Right[p*2    ].u = u; m_Right[p*2 + 1].u = u;
    }
}

// ---------------------------------------------------------------------------
// Update — matches SlashEntity::Update (0x17D664)
// ---------------------------------------------------------------------------
void SlashEntity::Update(float dt) {
    // State machine: if bit 0 set, shift-left → deactivating path.
    // Binary: m_bBladeActive = 1 (active) → 2 (deactivating) → 0 (off).
    // We approximate: when State == 2, decay the trail and clear.
    if (m_State == 2) {
        // Drop two oldest vertices each frame until the trail is empty.
        if (m_PointCount > 0) {
            ShiftDown();
        } else {
            m_State = 0;
            m_SpeedScale = 0.0f;
        }
    }

    // Speed scale decays toward 0 when no new points this frame.
    m_SpeedScale -= dt * 4.0f;
    if (m_SpeedScale < 0.0f) m_SpeedScale = 0.0f;

    RebuildGeometry();
}

// ---------------------------------------------------------------------------
// DrawSlice — matches 0x17E424
// ---------------------------------------------------------------------------
void SlashEntity::Draw() {
    if (m_PointCount < 4) return;
    if (!g_BladeTex.IsValid()) return;

    // Matrix setup: reset world stack and upload MVP. The binary calls
    // TranslateMatrix_SlashEntity(g_globalOffset) which applies a (480, 320, 0)
    // offset — but that's tied to the old port-specific ortho (see
    // docs/engine/coordinate-system.md). With our binary-centred ortho the
    // blade positions are already in the correct space.
    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindTexture(GL_TEXTURE_2D, g_BladeTex->m_TexId);

    if (Renderer* r = Renderer::GetInstance()) {
        // Two mirrored triangle strips — one for each half of the swipe.
        r->DrawTriStrip(m_Left,  m_PointCount);
        r->DrawTriStrip(m_Right, m_PointCount);
    }
}
