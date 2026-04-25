//
// SlashEntity — blade trail visual-only port.
// Matches binary 0x17C82C..0x17E504. See SlashEntity.h for method addresses.
//
// Analysed: 2026-04-13T20:00
//

#include "SlashEntity.h"
#include "ActorManager.h"
#include "Entity.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "asset/TextureManager.h"
#include "input/Touch.h"
#include "particle/PSPParticleManager.h"
#include "collision/ColLine.h"
#include "collision/ColSphere.h"
#include "util/StringHash.h"
#include "Game.h"
#include <cstring>
#include <cmath>
#include <cstdio>

const float SlashEntity::POINT_SPACING         = 64.0f;   // DAT_0017d5fc
const float SlashEntity::MOVE_THRESH_ACTIVE    = 5.0f;    // sqrt(25)

// Binary global SlashEntity::ModPowerMask @ BSS 0x0024d8cc. See
// SlashEntity.h for bit layout + lifecycle notes.
uint32_t SlashEntity::s_ModPowerMask = 0;
// NOTE: MOVE_THRESH_INACTIVE is vestigial in the binary. The decomp of
// UpdateTouchDown (0x17D2E4) only reads DAT_0017d5f8 (= 2500 = 50²) when
// field_0x144 (the "blade active" flag) is clear — but frame 1 always
// enters the reset branch (LAB_0017d444) via the "tail uninitialised" gate
// and sets field_0x144 |= 1 at the bottom, so the 2500 threshold is never
// actually tested against a nonzero distance. Pure taps are filtered
// _implicitly_ by the 2-point minimum in RebuildGeometry and
// CollideWithSphere below — frame 1 adds exactly one point at the touch
// position, and a single point is non-renderable and non-colliding. A
// no-motion mouse click therefore cannot slice a fruit, matching the
// mobile behaviour where a tap without drag fires no move events.
const float SlashEntity::MOVE_THRESH_INACTIVE  = 50.0f;   // sqrt(DAT_0017d5f8 = 2500) — vestigial, see note above

// Per-point half-width of the blade. Binary uses 9.0 × thicknessFactor.
static const float BLADE_HALF_WIDTH = 12.0f;

// Number of trailing points to taper for the head tip. The last N points
// get progressively smaller thickness so the blade has a pinched tip.
static const int   HEAD_TAPER_COUNT = 5;

// Trail point lifetime in seconds. Each frame, points older than this are
// dropped from the front of the trail — this creates the "blade fades even
// while the finger is down" behaviour of the binary (which uses a per-frame
// perp-length extension with speed-scaled threshold — see UpdatePoints
// 0x17B92C). The port replaces that formula with simple time-based decay
// for clarity; visual feel is approximately the same.
static const float TRAIL_LIFETIME = 0.25f;

// ---------------------------------------------------------------------------
// TODO: proper slash-modifier trail emitter path.
//
// The binary's SlashEntity reads the trail emitter hash from a static member
// `SlashEntity::ModPartilcesHash` (sic: binary typo) that is populated by
// ItemManager / SlashModInfo::SetEquipped when the player changes slash
// modifier in the Dojo shop. The default item (`ORIGINAL_SLASH` in
// itemlist.xml) has no particle trail — you only see smoke/sparkle when a
// mod like `dark_blade`, `flame_blade`, `ice_blade`, etc. is equipped.
//
// Port shortcut: hard-code `dark_blade` as the trail emitter so the visual
// can be tested without porting the full item / mod-equip pipeline first.
// Replace with `SlashEntity::ModPartilcesHash` once that is wired up.
//
//   Full fix requires:
//   1. Port ItemManager XML parser for Data/xml/itemlist.xml
//   2. Port SlashModInfo struct + Parse method
//   3. Port SetEquipped / currently-equipped state
//   4. Port Dojo shop UI for changing mods (or preset via save data)
//   5. Replace the hash lookup below with ModPartilcesHash
// ---------------------------------------------------------------------------
static const char* TRAIL_EMITTER_NAME = "dark_blade";

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
    g_BladeTex.SetNull();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
SlashEntity::SlashEntity()
    : m_NumPoints(0)
    , m_TrailEmitter(nullptr)
    , m_State(0)
    , m_bHasHead(false)
    , m_RawTouchPos(0, 0, 0)
{
    memset(m_Left,  0, sizeof(m_Left));
    memset(m_Right, 0, sizeof(m_Right));
}

SlashEntity::~SlashEntity() {
    Release();
}

void SlashEntity::Init() {
    m_NumPoints = 0;
    m_State = 0;
    m_bHasHead = false;
    m_TrailEmitter = nullptr;

    for (int i = 0; i < MAX_VERTS; ++i) {
        m_Left[i].nx  = 0; m_Left[i].ny  = 0; m_Left[i].nz  = 1.0f;
        m_Right[i].nx = 0; m_Right[i].ny = 0; m_Right[i].nz = 1.0f;
        m_Left[i].colour  = 0xFFFFFFFF;
        m_Right[i].colour = 0xFFFFFFFF;
    }
}

void SlashEntity::Release() {
    m_NumPoints = 0;
    if (m_TrailEmitter) {
        Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_TrailEmitter);
        m_TrailEmitter = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Touch ingestion — matches UpdateTouchDown (0x17D2E4) / AddPoint (0x17CE0C)
// ---------------------------------------------------------------------------
void SlashEntity::OnTouchActive(float x, float y) {
    Vec3 newPos(x, y, 0.0f);
    // Capture the raw touch position every frame. The interpolated trail
    // points we push below can lag the true finger by up to POINT_SPACING
    // units on fast swipes — the binary emitter follows base.pos (raw)
    // not the last trail point, so store the raw value for Update to read.
    m_RawTouchPos = newPos;

    if (!m_bHasHead) {
        // First touch: start a fresh trail. Matches the reset branch in
        // binary UpdateTouchDown at LAB_0017d444 — seeds tail=head=prevHead
        // to the current touch pos and adds one point at that position,
        // then sets the "blade active" flag. A single point is intentional:
        // RebuildGeometry and CollideWithSphere both early-out at
        // m_NumPoints < 2, so a zero-motion click produces neither a visible
        // blade nor a slice. Subsequent motion of >5 units (MOVE_THRESH_ACTIVE)
        // adds the second point and the trail becomes visible/sliceable.
        m_NumPoints = 0;
        m_bHasHead = true;
        m_State = 1;
        AddPoint(newPos, Vec3(0, 0, 0));
        return;
    }

    // Movement threshold check: 5 units² = 25 when active, 50² = 2500 off.
    const Vec3 lastCenter = m_NumPoints > 0
                          ? m_Points[m_NumPoints - 1].center
                          : newPos;
    const Vec3 delta(newPos.x - lastCenter.x, newPos.y - lastCenter.y, 0.0f);
    const float distSq = delta.x * delta.x + delta.y * delta.y;
    const float thresh = (m_State != 0)
        ? (MOVE_THRESH_ACTIVE   * MOVE_THRESH_ACTIVE)
        : (MOVE_THRESH_INACTIVE * MOVE_THRESH_INACTIVE);
    if (distSq < thresh) return;

    // Interpolate intermediate points along the movement vector at
    // POINT_SPACING intervals. Matches binary's stepSize=64 loop.
    const float dist = sqrtf(distSq);
    const Vec3 dir(delta.x / dist, delta.y / dist, 0.0f);

    float travelled = POINT_SPACING;
    while (travelled < dist) {
        Vec3 step(lastCenter.x + dir.x * travelled,
                  lastCenter.y + dir.y * travelled, 0.0f);
        AddPoint(step, dir);
        travelled += POINT_SPACING;
    }

    // Final point at current touch position.
    AddPoint(newPos, dir);
    m_State = 1;
}

void SlashEntity::OnTouchReleased() {
    // Matches binary state-machine bit shift: 1 → 2 (deactivating).
    if (m_State == 1) m_State = 2;
    m_bHasHead = false;
}

// ---------------------------------------------------------------------------
// AddPoint — matches binary 0x17CE0C (simplified)
// ---------------------------------------------------------------------------
void SlashEntity::AddPoint(const Vec3& pos, const Vec3& dir) {
    if (m_NumPoints >= MAX_POINTS) {
        // Shift-drop the oldest point (overflow guard; time-based decay in
        // Update normally keeps the trail well below MAX_POINTS).
        for (int i = 1; i < MAX_POINTS; ++i) {
            m_Points[i - 1] = m_Points[i];
        }
        m_NumPoints = MAX_POINTS - 1;
    }

    TrailPoint& p = m_Points[m_NumPoints];
    p.center = pos;
    p.dir    = dir;
    p.age    = 0.0f;

    // Cumulative arc length from the oldest point.
    if (m_NumPoints == 0) {
        p.arcLen = 0.0f;
    } else {
        const TrailPoint& prev = m_Points[m_NumPoints - 1];
        const float dx = pos.x - prev.center.x;
        const float dy = pos.y - prev.center.y;
        p.arcLen = prev.arcLen + sqrtf(dx * dx + dy * dy);
    }

    m_NumPoints++;
}

// ---------------------------------------------------------------------------
// RebuildGeometry — matches UpdatePoints (0x17B92C) simplified.
// Generates left/right triangle-strip vertex buffers from m_Points with
// miter-joined perpendiculars, arc-length U, alpha fade, and head taper.
// ---------------------------------------------------------------------------
void SlashEntity::RebuildGeometry() {
    if (m_NumPoints < 2) return;

    const float totalArc = m_Points[m_NumPoints - 1].arcLen;
    const float invArc   = (totalArc > 0.0f) ? (1.0f / totalArc) : 0.0f;

    for (int i = 0; i < m_NumPoints; ++i) {
        const TrailPoint& p = m_Points[i];

        // Miter-join direction: average of incoming and outgoing dirs at
        // interior points; endpoints use their own dir. This smooths out
        // the "kink" the original code had at direction changes.
        Vec3 d = p.dir;
        if (i + 1 < m_NumPoints) {
            const Vec3& next = m_Points[i + 1].dir;
            d.x = (d.x + next.x) * 0.5f;
            d.y = (d.y + next.y) * 0.5f;
        }
        // Normalise d (fallback to incoming if both zero).
        const float dlen = sqrtf(d.x * d.x + d.y * d.y);
        if (dlen > 0.0001f) {
            d.x /= dlen;
            d.y /= dlen;
        } else {
            d = p.dir;
        }

        // Head taper: last HEAD_TAPER_COUNT points shrink toward tip.
        float thickness = 1.0f;
        const int headDist = (m_NumPoints - 1) - i;  // 0 at tip
        if (headDist < HEAD_TAPER_COUNT) {
            thickness = (float)headDist / (float)HEAD_TAPER_COUNT;
        }

        // Perpendicular: 90° CW rotation of miter direction, scaled.
        const float half = BLADE_HALF_WIDTH * thickness;
        const float perpX = -d.y * half;
        const float perpY =  d.x * half;

        // Arc-length U (0 at tail, approaching 1 at head).
        const float u = p.arcLen * invArc * 0.98f;

        // Alpha fade by age: full at 0, zero at TRAIL_LIFETIME. Oldest
        // points (the tail) fade out visually as they approach expiry.
        float alphaFrac = 1.0f - (p.age / TRAIL_LIFETIME);
        if (alphaFrac < 0.0f) alphaFrac = 0.0f;
        if (alphaFrac > 1.0f) alphaFrac = 1.0f;
        const uint32_t alpha = (uint32_t)(alphaFrac * 255.0f);
        const uint32_t col = (alpha << 24) | 0x00FFFFFF;

        // Left strip: outer edge → centre.
        QUADCUSTOMVERTEX& l0 = m_Left[i * 2    ];
        QUADCUSTOMVERTEX& l1 = m_Left[i * 2 + 1];
        // Right strip: centre → outer edge.
        QUADCUSTOMVERTEX& r0 = m_Right[i * 2    ];
        QUADCUSTOMVERTEX& r1 = m_Right[i * 2 + 1];

        l0.x = p.center.x - perpX;
        l0.y = p.center.y - perpY;
        l0.z = p.center.z;
        l0.u = u; l0.v = 0.0f;
        l0.colour = col;

        l1.x = p.center.x;
        l1.y = p.center.y;
        l1.z = p.center.z;
        l1.u = u; l1.v = 0.5f;
        l1.colour = col;

        r0.x = p.center.x;
        r0.y = p.center.y;
        r0.z = p.center.z;
        r0.u = u; r0.v = 0.5f;
        r0.colour = col;

        r1.x = p.center.x + perpX;
        r1.y = p.center.y + perpY;
        r1.z = p.center.z;
        r1.u = u; r1.v = 1.0f;
        r1.colour = col;
    }
}

// ---------------------------------------------------------------------------
// Update — matches SlashEntity::Update (0x17D664) + UpdateTouchDown (0x17D2E4)
// ---------------------------------------------------------------------------
void SlashEntity::Update(float dt) {
    // Poll Mortar::Touch slot 0 (single-player).
    const Mortar::TouchState* s = Mortar::Touch::GetInstance().GetSlot(0);
    if (s) {
        if (s->phase <= 0) {
            OnTouchActive((float)s->currX, (float)s->currY);
        } else if (m_bHasHead) {
            OnTouchReleased();
        }
    }

    // Trail particle emitter — matches binary UpdateTouchDown (0x17D2E4).
    // Created on first active touch, follows the head each frame, cleared
    // on release. See TRAIL_EMITTER_NAME TODO above for the full ItemManager
    // path this should come from eventually.
    Mortar::PSPParticleManager& pm = Mortar::PSPParticleManager::GetInstance();
    const bool bladeActive = (m_State != 0) && (m_NumPoints > 0);
    if (bladeActive) {
        if (!m_TrailEmitter) {
            const uint32_t hash = StringHash(TRAIL_EMITTER_NAME);
            m_TrailEmitter = pm.AddEmitter(hash, &m_TrailEmitter, /*persistent=*/true);
            if (m_TrailEmitter) {
                m_TrailEmitter->m_bUpdateWhenPaused = true;
            }
        }
        if (m_TrailEmitter) {
            // Follow the raw finger position, not the last interpolated
            // trail point — matches UpdateTouchDown @ 0x17D2E4 which writes
            // m_TrailEmitter->m_Pos = this->base.pos (the raw touch).
            m_TrailEmitter->m_Pos = m_RawTouchPos;
        }
    } else if (m_TrailEmitter) {
        pm.ClearEmitter(m_TrailEmitter);
        m_TrailEmitter = nullptr;
    }

    // Age every point by dt. This runs unconditionally — even while the
    // finger is still down — so the trail naturally fades from the tail
    // even during a continuous swipe. Mirrors the binary's per-frame
    // UpdatePoints pass at 0x17B92C where each pair's perp length is
    // extended toward a drop threshold.
    for (int i = 0; i < m_NumPoints; ++i) {
        m_Points[i].age += dt;
    }

    // Drop expired points from the front of the trail (oldest = tail).
    int dropCount = 0;
    while (dropCount < m_NumPoints &&
           m_Points[dropCount].age >= TRAIL_LIFETIME) {
        dropCount++;
    }
    if (dropCount > 0) {
        for (int i = dropCount; i < m_NumPoints; ++i) {
            m_Points[i - dropCount] = m_Points[i];
        }
        m_NumPoints -= dropCount;
    }

    // State machine collapse: if the finger was released and the trail
    // drained, reset to idle. The binary's m_bBladeActive state machine
    // (1 → 2 → 0) is simulated by the age-based drop above.
    if (m_State == 2 && m_NumPoints == 0) {
        m_State = 0;
    }

    // Slice-test pass. Matches the FRUIT/BOMB collision loops inside
    // SlashEntity::Update (0x17D664). Only runs when the blade has at
    // least 2 points, isn't deactivating, and the post-explosion game-over
    // window isn't already ticking (binary gate: `if (game->bombTimer > 0)
    // return` at 0x17D664 line ~442). This stops the blade from registering
    // more slices once a bomb has already gone off.
    Game* game = Game::GetInstance();
    const bool bombHitActive = game && game->bombHitTimer > 0.0f;

    if (m_NumPoints >= 2 && m_State != 0 && !bombHitActive) {
        ActorManager* am = ActorManager::GetInstance();
        if (am) {
            // Only fruit (0) and bomb (1) participate in blade collision
            // — matches binary.
            for (int t = 0; t <= 1; t++) {
                const std::list<Entity*>& list = am->GetTypeList(t);
                for (auto it = list.begin(); it != list.end(); ++it) {
                    Entity* e = *it;
                    if (!e || !e->IsActive()) continue;
                    if (e->m_Col.radius <= 0.0f) continue;

                    Vec3 bladeVel;
                    if (CollideWithSphere(e->m_Col, bladeVel)) {
                        e->OnSliced(bladeVel);
                    }
                }
            }
        }
    }

    RebuildGeometry();
}

// ---------------------------------------------------------------------------
// CollideWithSphere — matches CollideWithEntity (0x17B570) simplified.
// The binary tests a single blade ColLine (head↔tail of this frame's swipe
// delta) against a fruit/bomb ColSphere. The port instead iterates every
// segment between consecutive trail points so that a fast swipe — which
// OnTouchActive interpolates into many POINT_SPACING=64 sub-points within a
// single frame — still registers the hit.
// ---------------------------------------------------------------------------
bool SlashEntity::CollideWithSphere(const Mortar::ColSphere& sphere,
                                     Vec3& outBladeVel) const {
    if (m_State == 0 || m_NumPoints < 2) {
        outBladeVel = Vec3(0, 0, 0);
        return false;
    }

    // Scan every segment; return the direction+length of the one that hit so
    // OnSliced can derive impulse magnitude AND slice angle. Binary path:
    // CollideWithEntity (0x17B570) uses the per-frame blade delta — one
    // segment per update. The port has N interpolated sub-segments per frame,
    // so we pick the segment that actually intersects.
    for (int i = 0; i + 1 < m_NumPoints; ++i) {
        Mortar::ColLine seg(m_Points[i].center, m_Points[i + 1].center);
        if (sphere.IntersectsLine(seg)) {
            outBladeVel = m_Points[i + 1].center - m_Points[i].center;
            return true;
        }
    }
    outBladeVel = Vec3(0, 0, 0);
    return false;
}

// ---------------------------------------------------------------------------
// DrawSlice — matches 0x17E424
// ---------------------------------------------------------------------------
void SlashEntity::Draw() {
    if (m_NumPoints < 2) return;
    if (!g_BladeTex.IsValid()) return;

    // Matrix reset + MVP upload. Matches binary Draw 0x17E424 prelude.
    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    glEnable(GL_BLEND);
    // Additive-like blend for a brighter blade trail. Binary inherits blend
    // state from Mortar::Texture::Set — we pick a reasonable fallback.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindTexture(GL_TEXTURE_2D, g_BladeTex->m_TexId);

    if (Renderer* r = Renderer::GetInstance()) {
        const int vertCount = m_NumPoints * 2;
        r->DrawTriStrip(m_Left,  vertCount);
        r->DrawTriStrip(m_Right, vertCount);
    }
}

// ---------------------------------------------------------------------------
// Blade modifier apply functions (called from SlashModInfo::SetEquipped)
// ---------------------------------------------------------------------------

// SetModColours @ 0x0017ca0c
// TODO: implement when blade palette + particle manager + actor iterate land.
// Full RE spec in docs/structs/items.md §SetModColours.
void SlashEntity::SetModColours(
    const Colour*  /*colours*/,
    int            /*colourCount*/,
    int            /*colourType*/,
    float          /*lifeScale*/,
    const char*    /*particlePath*/,
    const char*    /*textureName2*/,
    bool           /*directional*/,
    const char*    /*contactParticle*/,
    const char*    /*particle2*/)
{
    // TODO: copy colour palette; load textureName2 via LoadLocalisedTexture;
    //       resolve particlePath/contactParticle/particle2 to emitter hashes;
    //       if game active walk ActorManager type-3 entities, call ColoursChanged().
}

// InitModColours @ 0x0017cc38
// TODO: implement when blade colour palette / overlay texture wiring lands.
// Resets palette to defaults, nulls overlay texture SmartPtr, zeroes emitter hashes.
void SlashEntity::InitModColours()
{
    // TODO: reset colourCount=0, colourType=0, null overlay texture SmartPtr,
    //       zero particle hashes + emitter-type flags, copy default colour palette.
}

// SetModScales @ 0x0017b328
// TODO: implement when SlashEntity internal scale fields are modelled.
// Default call (no blade mod): SetModScales(1.0f, 1.0f, 0.0f, 1.0f, false, false, 0.0f).
void SlashEntity::SetModScales(
    float  /*startThick*/,
    float  /*endThick*/,
    float  /*scaleLen*/,
    float  /*uvLen*/,
    bool   /*flipUD*/,
    bool   /*loop*/,
    float  /*loopUVLen*/)
{
    // TODO: write to g_pSlashEntity singleton scale fields when they are modelled.
}
