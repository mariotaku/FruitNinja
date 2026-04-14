#ifndef FN_SLASH_ENTITY_H
#define FN_SLASH_ENTITY_H

//
// SlashEntity — blade trail visual (entity type 3)
// Matches binary 0x17C82C..0x17E504 (see docs/engine/slash-entity.md — TODO)
//
// Visual-only port. Skips: collision, slice/combo, ghost trail, particle
// emitter, blade colour palette. Renders two mirrored triangle strips from
// a per-point trail buffer. Each frame RebuildGeometry regenerates the
// vertex arrays from the TrailPoint list applying:
//   - miter-join perpendicular at each point (average of incoming + outgoing)
//   - arc-length U coordinate along the trail
//   - per-vertex alpha fade toward the tail
//   - head taper (last few points get narrower thickness)
//
// Binary addresses (ARM32):
//   LoadContent       0x17C948
//   Init              0x17C65C
//   Release           0x17C60C
//   AddPoint          0x17CE0C
//   UpdateTouchDown   0x17D2E4
//   UpdatePoints      0x17B92C
//   Update            0x17D664
//   PreUpdate         0x17C584
//   DrawSlice         0x17E424
//

#include "math/Vec3.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "collision/ColSphere.h"
#include <cstdint>

namespace Mortar { struct PSPParticleEmitter; }

class SlashEntity {
public:
    static const int MAX_POINTS = 96;        // trail length (was 160 in binary)
    static const int MAX_VERTS  = MAX_POINTS * 2; // 2 verts per strip per point
    static const float POINT_SPACING;        // 64.0 — units between interpolated points
    static const float MOVE_THRESH_ACTIVE;   // 5.0  — min move² = 25 to add point
    static const float MOVE_THRESH_INACTIVE; // 50.0 — min move² = 2500 when blade off

    SlashEntity();
    ~SlashEntity();

    // One-time global content load — matches 0x17C948. Loads blade.tex.
    static void LoadContent();
    static void ReleaseContent();

    // Matches SlashEntity::Init (0x17C65C). Allocates vertex buffers, resets state.
    void Init();
    void Release();

    // Matches SlashEntity::Update (0x17D664). Polls Mortar::Touch slot 0 +
    // per-frame geometry rebuild.
    void Update(float dt);

    // Matches SlashEntity::DrawSlice (0x17E424). Two mirrored tri-strips.
    void Draw();

    // Test whether the current blade trail intersects a collision sphere.
    // Iterates every segment between consecutive trail points (mirrors the
    // binary's CollideWithEntity at 0x17B570, simplified to iterate the
    // full trail instead of just the m_HeadPos/m_TailPos pair — needed
    // because OnTouchActive may interpolate many points in one frame on
    // fast swipes). Returns true on the first intersecting segment.
    // Returns true if any trail segment intersects the sphere. On a hit,
    // writes the segment delta (end - start) into outBladeVel so the caller
    // can derive both magnitude and direction for OnSliced.
    bool CollideWithSphere(const Mortar::ColSphere& sphere,
                           Vec3& outBladeVel) const;

    // True while the blade has at least 2 trail points and is not
    // deactivating — used to gate collision checks.
    bool IsBladeActive() const { return m_State != 0 && m_NumPoints >= 2; }

private:
    // Stored per-point metadata. The vertex buffers m_Left/m_Right are
    // regenerated from this list each frame in RebuildGeometry.
    struct TrailPoint {
        Vec3  center;    // position in centred ortho coords
        Vec3  dir;       // normalised incoming direction (from previous point)
        float arcLen;    // cumulative length from oldest point
        float age;       // seconds since this point was added (drops at lifetime)
    };

    // Matches SlashEntity::UpdateTouchDown (0x17D2E4). Ingests one touch
    // position, interpolating intermediate points along the movement delta.
    void OnTouchActive(float x, float y);

    // Matches SlashEntity::TouchReleased. Marks blade for deactivation;
    // the trail fades out via shift-drop over subsequent Update ticks.
    void OnTouchReleased();

    // Matches SlashEntity::AddPoint (0x17CE0C). Appends one TrailPoint.
    // Bulk-shifts the array when full (drops the oldest point).
    void AddPoint(const Vec3& pos, const Vec3& dir);

    // Rebuilds m_Left / m_Right vertex buffers from m_Points. Matches
    // SlashEntity::UpdatePoints (0x17B92C) simplified.
    void RebuildGeometry();

    TrailPoint m_Points[MAX_POINTS];
    int m_NumPoints;

    QUADCUSTOMVERTEX m_Left [MAX_VERTS];  // centre → upper edge strip
    QUADCUSTOMVERTEX m_Right[MAX_VERTS];  // centre → lower edge strip

    // Particle emitter that follows the blade for smoke/sparkle trail.
    // Matches binary +0x3c (m_TrailEmitter). Created on first active touch
    // via PSPParticleManager::AddEmitter, cleared on release.
    Mortar::PSPParticleEmitter* m_TrailEmitter;

    // 2-bit state machine matching binary m_bBladeActive:
    //   0 = off, 1 = active, 2 = deactivating (fading out)
    uint8_t m_State;
    bool    m_bHasHead;

    // Raw touch position from the most recent OnTouchActive — used as the
    // trail emitter position so particles spawn at the true finger location,
    // not the last interpolated trail point (which can lag by up to
    // POINT_SPACING=64 units on fast swipes). Matches binary UpdateTouchDown
    // @ 0x17D2E4 which writes `m_TrailEmitter->m_Pos = this->base.pos`,
    // where base.pos is the raw touch input.
    Vec3 m_RawTouchPos;
};

// Global singleton instance — created in GameInit, destroyed in GameDestroy.
extern SlashEntity* g_pSlashEntity;

#endif
