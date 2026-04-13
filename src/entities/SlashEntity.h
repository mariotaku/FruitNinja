#ifndef FN_SLASH_ENTITY_H
#define FN_SLASH_ENTITY_H

//
// SlashEntity — blade trail visual (entity type 3)
// Matches binary 0x17C82C..0x17E504 (see docs/engine/slash-entity.md — TODO)
//
// Minimal visual-only port:
//   - Two mirrored QUADCUSTOMVERTEX triangle strips (left/right of centre)
//   - Point spacing 64 units, movement threshold 5 units active / 50 inactive
//   - Fixed 162-vertex buffers, shift-down when full
//   - Skips: collision, slice/combo logic, ghost trail, particle emitter,
//     per-vertex UV fade (simplified to a per-frame point-count decay)
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
#include <cstdint>

class SlashEntity {
public:
    static const int MAX_POINTS = 160;      // m_SplitPoint default
    static const int MAX_VERTS  = 162;      // MAX_POINTS + 2 (strip + taper slot)
    static const float POINT_SPACING;       // 64.0 — units between interpolated points
    static const float MOVE_THRESH_ACTIVE;  // 5.0  — min move distance²=25 to add point
    static const float MOVE_THRESH_INACTIVE;// 50.0 — min move distance²=2500 when blade off

    SlashEntity();
    ~SlashEntity();

    // One-time global content load — matches 0x17C948. Loads blade.tex.
    static void LoadContent();
    static void ReleaseContent();

    // Matches SlashEntity::Init (0x17C65C). Allocates vertex buffers, resets state.
    void Init();
    void Release();

    // Matches SlashEntity::TouchDown (wraps UpdateTouchDown @ 0x17D2E4).
    // Called when a finger/mouse begins or continues a touch.
    void TouchDown(float x, float y);

    // Matches SlashEntity::TouchUp. Marks blade for deactivation; trail fades.
    void TouchUp();

    // Matches SlashEntity::Update (0x17D664). Per-frame geometry rebuild.
    void Update(float dt);

    // Matches SlashEntity::DrawSlice (0x17E424). Two mirrored tri-strips.
    void Draw();

private:
    // Matches SlashEntity::AddPoint (0x17CE0C).
    // dir is the normalised blade direction (movement vector).
    void AddPoint(const Vec3& pos, const Vec3& dir, float thickness);

    // Bulk-shift vertices down by 2 slots when the buffer is full.
    void ShiftDown();

    // Rebuild per-vertex perpendicular geometry each frame.
    // Matches SlashEntity::UpdatePoints (0x17B92C).
    void RebuildGeometry();

    int m_PointCount;                 // live vertex pair count (0..MAX_VERTS-2)
    QUADCUSTOMVERTEX m_Left [MAX_VERTS]; // left strip: centre - perp
    QUADCUSTOMVERTEX m_Right[MAX_VERTS]; // right strip: centre + perp

    Vec3 m_HeadPos;                   // newest point (tip)
    Vec3 m_TailPos;                   // oldest point
    Vec3 m_BladeDir;                  // current direction
    float m_SpeedScale;               // 1.0 on AddPoint, decays
    bool  m_bHasHead;                 // head/tail sentinel replacement

    // 2-bit state machine matching binary m_bBladeActive.
    // 0 = off, 1 = active, 2 = deactivating (trail fading out)
    uint8_t m_State;
};

// Global singleton instance — created in GameInit, destroyed in GameDestroy.
// The binary uses SlashEntity[2] (one per player) but the port keeps one.
extern SlashEntity* g_pSlashEntity;

#endif
