#ifndef FN_SLASH_ENTITY_H
#define FN_SLASH_ENTITY_H

//
// SlashEntity -- blade trail visual (entity type 3)
// v1.6.1 binary-faithful port: heap-allocated vertex buffers, no inline TrailPoint ring.
//
// sizeof(SlashEntity) v1.6.1 = 0x188 (392 bytes).
//   m_pLeftBuffer/m_pRightBuffer each hold (m_SplitPoint+2) = 162 QUADCUSTOMVERTEX records.
//   Trail state is stored directly in the vertex buffers; no separate TrailPoint ring.
//
// Port-only SDL fields (m_FingerId, m_RawTouchPos) are appended
// AFTER offset 0x188 so they do not perturb the binary field offsets.
//
// Binary addresses (v1.6.1 ARM32):
//   CreateEntity    0x1d909c
//   Init            0x1e7a34  (also vtable slot 2 @ 3-arg form)
//   InitPoints      0x1e75d0
//   Release         0x1e79b0
//   Update          0x1e867c
//   ctor            0x1e7cac
//   dtors           0x1e7ba8 / 0x1e7c50
//   vtable          0x2cea08
//   DrawSlice       0x1e83b0
//   AddPoint        0x1e9918
//   UpdatePoints    0x1e6914
//
// (Older v1.5 addresses remain on methods not yet confirmed at v1.6.1 offsets.)
//   LoadContent     0x17C948
//   Reset           0x17B71C
//   PreUpdate       0x17C584
//   TouchDown       0x17D61C
//   TouchMoveX      0x17C50C
//   TouchMoveY      0x17C490
//   PlaySwipe       v1.6.1 0x001e8550
//   GetHeadThicknessScale v1.6.1 0x1e684c
//   CreateGhost     v1.6.1 0x001e67f4
//   MissControlDeleted    0x17B388
//   Draw            v1.6.1 0x001e6168  (4 bytes, single BX lr)
//   CollisionResponse     v1.6.1 0x001e616c  (8 bytes, `mov r0,#0; bx lr`)
//   UpdateCollisionLine   0x17B3C0  (4-byte stub, returns 0)
//   DrawUpdate      0x17B398
//

#include "Entity.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "collision/ColSphere.h"
#include "engine/util/Event.h"
#include <cstdint>

struct PSPParticleEmitter;
struct InputEvent;

class Fruit;
class HUDControl;
class MissControl;

class SlashEntity : public Mortar::Entity {
public:
    static const int MAX_POINTS = 96;        // trail length (was 160 in binary)
    static const int MAX_VERTS  = MAX_POINTS * 2; // 2 verts per strip per point
    static const float POINT_SPACING;        // 64.0 -- units between interpolated points
    static const float MOVE_THRESH_ACTIVE;   // 5.0  -- min move^2 = 25 to add point
    static const float MOVE_THRESH_INACTIVE; // 50.0 -- min move^2 = 2500 when blade off

    // Global SlashEntity::ModPowerMask (binary BSS 0x0024d8cc). A uint32_t
    // bitmask that active SlashModifier instances OR their bits into each
    // frame; cleared at the top of PowerUpManager::Update via SetDefaults.
    static uint32_t s_ModPowerMask;

    SlashEntity();
    ~SlashEntity();

    // One-time global content load -- matches 0x17C948. Loads blade.tex.
    static void LoadContent();
    static void ReleaseContent();

    // Port-only convenience: stores fingerId, calls binary-faithful 3-arg Init,
    // then registers per-finger input callbacks (done by GameTaskInitInput in binary).
    void Init(int fingerId = 0);
    void Release() override;

    // Binary @ 0x17B71C -- wipe touch/trail state, sentinel-mark positions,
    // clear vertex strips, clear 11-entry combo-slice array.
    void Reset();

    // Matches SlashEntity::Update (v1.6.1 @ 0x1e867c). Per-frame update.
    void Update(float dt) override;

    // Frozen-branch stub -- no deferred post-step work.
    // TODO: v1.6.1 <addr unresolved> (SlashEntity::PostUpdate) -- the address this
    // slot previously cited belongs to SlashEntity::Draw (v1.6.1 @0x001e6168), so
    // it was a mis-stamp rather than a stale remap. PostUpdate's own entry point is
    // unresolved; re-RE before trusting any body here.
    void PostUpdate(float dt) override;

    // Matches SlashEntity::PreUpdate (0x17C584). Ticks ghost frame counters,
    // advances palette cycle, pushes swipe-loop volume to ItemManager.
    void PreUpdate(float dt);

    // Entity vtable slot 5 (+0x14): Draw(Renderer&) override.
    // v1.6.1 SlashEntity::Draw @0x001e6168 is 4 bytes, a single `bx lr` -- no-op.
    // (The blade is actually rendered by DrawSlice from GameDraw's 16-slot loop.)
    // ASM-verified: 2026-05-18 v1.6.1 SlashEntity::Draw @ 0x001e6168 (re-analyst)
    void Draw(Renderer& r) override;

    // v1.6.1 SlashEntity::PlaySwipe @0x001e8550 -- mod-override swipe SFX, else "Sword-swipe-%d" via Rand32.
    void PlaySwipe();

    // v1.6.1 @ 0x1e684c -- derive head taper scale.
    float GetHeadThicknessScale() const;

    // v1.6.1 SlashEntity::CreateGhost @0x001e67f4 -- advance the global ghost ring
    // and snapshot the current blade vertex strip into the new slot:
    //     s_currentSlashIdx = (s_currentSlashIdx + 1) % 8;
    //     s_ghosts[s_currentSlashIdx].StartEffect(&m_pLeftBuffer, m_PointCount);
    // Ring size is 8; SlashEntityGhost stride is 0x10.
    // TODO: v1.6.1 0x001e67f4 (SlashEntity::CreateGhost) -- SlashEntityGhost is not
    // ported yet, so the body is a no-op stub. Its API for whoever ports the ring:
    //   StartEffect @0x001eb048, Update @0x001eaf4c, Draw @0x001eb0f8,
    //   Reset @0x001eaaec, Release @0x001eaf10.
    void CreateGhost();

    // Binary @ 0x17B388 -- clear back-pointer to combo MissControl when deleted.
    void MissControlDeleted(HUDControl* ctrl);

    // v1.6.1 SlashEntity::CollisionResponse @0x001e616c -- entity vtable slot;
    // SlashEntity is pure aggressor, so the body is `mov r0,#0; bx lr`.
    int CollisionResponse();

    // Binary @ 0x17B3C0 -- 4-byte stub, returns 0.
    int UpdateCollisionLine(long dt);

    // Binary @ 0x17B398 -- clears g_state.bombSkipFlag, sets needsDrawFlag.
    void DrawUpdate(float dt);

    // Binary @ 0x17B0F4 -- advance palette progress by dt*lifeScale,
    // lerp between consecutive palette entries.
    void UpdateModColour(Colour* outColour, float dt);

private:
    // -----------------------------------------------------------------------
    // Binary-faithful data members.
    // Declaration order matches binary offset order; __bada__ static_asserts
    // below confirm every offset. sizeof(Entity)==0x3C on ARM32, so these
    // own fields start at absolute offset 0x3C in the full object.
    // -----------------------------------------------------------------------

    // +0x3c  PSPParticleEmitter*  m_TrailEmitter
    PSPParticleEmitter* m_TrailEmitter;

    // +0x40  float  m_Scale  hit-flash weight [0..1]
    float m_Scale;

    // +0x44  Colour  m_BaseColour    per-vertex stamped colour (lerp result)
    Colour m_BaseColour;

    // +0x48  Colour  m_HighlightColour   current palette-cycle output colour
    Colour m_HighlightColour;

    // +0x4c  uint8_t  m_BombHitEdge   bomb-hit one-shot. Set in Update when
    // bomb[0x68]!=0 && bomb[0x88]==0. NOT the swipe fuse (see m_BladeActive @+0x140).
    // Ghidra-authoritative name: m_BombHitEdge.
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0017E424 (re-analyst)
    uint8_t m_BombHitEdge;
    uint8_t _pad4d[3];

    // +0x50  int32_t  m_SplitPoint  InitPoints capacity (160 from Init).
    // v1.6.1 @ 0x1e75d0 InitPoints sets m_SplitPoint = splitPoint.
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0017C340 (re-analyst)
    int m_SplitPoint;

    // +0x54  int32_t  m_ComboBaseIdx  combo base index
    int m_ComboBaseIdx;

    // +0x58  int32_t  m_PointCount  count of live vertex pairs in the strips.
    int m_PointCount;

    // +0x5c  QUADCUSTOMVERTEX*  m_pLeftBuffer   heap array of (m_SplitPoint+2) = 162 verts
    // +0x60  QUADCUSTOMVERTEX*  m_pRightBuffer  heap array of 162 verts
    // Each buffer: 162 * 36 = 5832 bytes.
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0017C340 (re-analyst)
    QUADCUSTOMVERTEX* m_pLeftBuffer;
    QUADCUSTOMVERTEX* m_pRightBuffer;

    // +0x64  _Vector3<float>  m_BladeDir  normalised blade direction
    _Vector3<float> m_BladeDir;

    // +0x70  _Vector3<float>  m_TailPos  oldest visible trail point position
    _Vector3<float> m_TailPos;

    // +0x7c  _Vector3<float>  m_HeadPos  tip (newest) trail point position
    _Vector3<float> m_HeadPos;

    // +0x88  _Vector3<float>  m_PrevHeadPos  previous frame tip position
    _Vector3<float> m_PrevHeadPos;

    // +0x94  float  m_SegLenSq  squared segment length (Init: -1.0f)
    float m_SegLenSq;
    // +0x98  float  m_HeadThickScale  head thickness scale
    float m_HeadThickScale;
    // +0x9c  _Vector3<float>  m_SliceBladeDir  blade direction at slice (for splat velocity)
    _Vector3<float> m_SliceBladeDir;

    // +0xa8  _Vector3<float>  m_SliceFruitPos  position of most recently sliced entity
    _Vector3<float> m_SliceFruitPos;

    // +0xb4  int32_t  m_SliceFruitType  fruit type of most recently sliced entity
    int m_SliceFruitType;

    // +0xb8  float  m_SwipeSoundTimer  cooldown between swipe SFX firings
    float m_SwipeSoundTimer;

    // +0xbc  Vec3[6]  m_GhostDirRing  6-entry ghost direction ring (stride 0xc = 12)
    _Vector3<float> m_GhostDirRing[6];

    // +0x104  uint32_t  m_GhostIndex  current ghost ring-buffer write index
    unsigned int m_GhostIndex;

    // +0x108  uint32_t  m_GhostCount  number of valid ghost entries
    unsigned int m_GhostCount;

    // +0x10c  _Vector3<float>  m_GhostDir  averaged ghost blade direction
    _Vector3<float> m_GhostDir;

    // +0x118  float  m_ComboTimer  per-swipe combo window accumulator (fractional seconds)
    //         Reset to 0 on slice; fires at 0.095 to close combo window.
    float m_ComboTimer;

    // +0x11c  MissControl*  m_pComboMissControl  combo popup control (or nullptr)
    MissControl* m_pComboMissControl;

    // +0x120  float  m_GhostSpawnTimer  accumulator for ghost-spawn delay
    float m_GhostSpawnTimer;

    // +0x124  uint8_t  m_GhostSpawnPending  one-shot flag to spawn ghost on next Update
    uint8_t m_GhostSpawnPending;
    uint8_t _pad125[3];

    // +0x128  Fruit*  m_pLastComboFruit  pointer to last fruit added to combo (for same-fruit skip)
    Fruit* m_pLastComboFruit;

    // +0x12c  int32_t  m_PendingSplats  signed splat-stream counter
    int m_PendingSplats;

    // +0x130  float  m_SplatTimer  accumulator between splat emissions (floored at -1.0)
    float m_SplatTimer;

    // +0x134  float  m_SplatInterval  inter-splat random delay interval
    float m_SplatInterval;

    // +0x138  int32_t  m_TrailShiftA  counter, decremented by 2 in AddPoint capacity shift; Init: -1
    int m_TrailShiftA;

    // +0x13c  int32_t  m_TrailShiftB  Init: -1
    int m_TrailShiftB;

    // +0x140  uint8_t  m_BladeActive  binary uchar shift-register for swipe ghost/SFX burst.
    //         DrawSlice latch: old=m_BladeActive; if(old){ nv=(old<<1)&2; m_BladeActive=nv; if(nv==0) fire; }
    //         OnTouchActive re-arms |= 1 each active frame.
    //         DISTINCT from m_BombHitEdge (+0x4c, bomb one-shot).
    uint8_t m_BladeActive;
    uint8_t _pad141[3];

    // +0x144  float  m_ComboScoreScale  combo scale factor (swipe sound timing); Init: 6.0f
    float m_ComboScoreScale;

    // +0x148  int32_t  m_field_0x148  Init: -1
    int m_field_0x148;

    // +0x14c  int32_t  m_field_0x14c  Init: -1
    int m_field_0x14c;

    // +0x150  int32_t[10]  m_ComboFruitTypes  10-entry combo fruit type history.
    //         v1.6.1 binary-faithful: fruit type indices. Init fills all 10 with -1.
    //         Written at m_ComboCounter before increment.
    //         ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0017C65C (re-analyst)
    int m_ComboFruitTypes[10];

    // +0x178  int32_t  m_ComboCount  number of fruits sliced in current combo swing
    int m_ComboCount;

    // +0x17c  int32_t  m_ComboCounter  next-write index into m_ComboFruitTypes (also live count)
    int m_ComboCounter;

    // +0x180  int32_t  m_ComboOnlineMode  online MP mode for current combo (0/1/2)
    int m_ComboOnlineMode;

    // +0x184  int16_t  m_AngleIndex  blade angle index (Atan2Idx result).
    // High 2 bytes unused. Init does NOT init this (seeded on first AddPoint).
    // AddPoint stores here AND to m_Angle (Entity base +0x36).
    int16_t m_AngleIndex;
    uint8_t _pad186[2];

    // -----------------------------------------------------------------------
    // Port-only fields -- NOT in the 0x188-byte binary struct.
    // Appended AFTER the binary layout so they never perturb binary offsets.
    // Under __bada__ these are excluded so the layout assertions pass.
    // -----------------------------------------------------------------------
#if !defined(__bada__)
    // SDL finger-slot this instance handles.
    int m_FingerId;

    // Raw touch position from most recent OnTouchActive.
    // Port specific: caches SDL event coordinates; the binary reads from the Bada InputEvent pipeline.
    _Vector3<float> m_RawTouchPos;

    // Port specific: EMA-smoothed |m_BladeDir| (px/sim-tick), updated in
    // Update() alongside the swipe-loop-volume bladeMag calc. Used only by
    // the FN::g_MotionMode speed gate (see Update()'s cut-decision block) --
    // for the pointer blade (m_FingerId == FN::POINTER_FINGER_CHANNEL), a
    // fruit/bomb cut is skipped while this stays below
    // FN::g_MotionSpeedThreshold (slow = aim, fast flick = cut). Zero effect
    // on touch blades or when motion mode is OFF.
    float m_SmoothedSpeed;

public:
    // Back-pointer to the fruit this slasher is aimed at.
    Fruit* m_pCurrentTarget;

    // Port specific: test-seam accessor for blade colour diagnostics.
    const Colour& GetBaseColour() const { return m_BaseColour; }

    // Port specific: test-seam accessor for first vertex buffer colour (what DrawSlice uploads).
    // Returns 0 if buffer is not allocated.
    uint32_t GetFirstVertexColour() const {
        if (m_pLeftBuffer && m_PointCount > 0) return m_pLeftBuffer[0].colour;
        return 0;
    }

    // Port specific: test-seam accessor for the U coordinate of vertex[i] in the left buffer.
    // Returns -99.0f if buffer is not allocated or index is out of range.
    float GetVertexU(int i) const {
        if (m_pLeftBuffer && i >= 0 && i < m_PointCount) return m_pLeftBuffer[i].u;
        return -99.0f;
    }

    // Port specific: test-seam accessor for the Y position of vertex[i] in the left buffer.
    // Upper bound is m_SplitPoint (not m_PointCount) so the head-cap slot is reachable.
    // Returns -99999.0f if buffer is not allocated or index is out of range.
    float GetVertexY(int i) const {
        if (m_pLeftBuffer && i >= 0 && i < m_SplitPoint) return m_pLeftBuffer[i].y;
        return -99999.0f;
    }

    // Port specific: number of live trail points.
    int GetPointCount() const { return m_PointCount; }

    // Port specific: debug accessor for blade trail endpoints (used by DebugBladeTrails_Draw).
    const _Vector3<float>& GetTailPos() const { return m_TailPos; }
    const _Vector3<float>& GetHeadPos() const { return m_HeadPos; }

#ifdef FN_TEST
    // Test-seam: bomb-hit latch (m_BombHitEdge, +0x4c) access. Lets
    // test_slash_input simulate the post-bomb game-over state where the
    // latch blocks TouchDown's per-press Reset(). Test targets only.
    void    TestSetBombHitEdge(uint8_t v) { m_BombHitEdge = v; }
    uint8_t TestGetBombHitEdge() const    { return m_BombHitEdge; }
#endif
#endif // !defined(__bada__)

public:
    // True while the blade has at least 2 trail points and is not idle.
    // Driven by m_BladeActive (+0x140), the binary's uchar shift-register latch.
    bool IsBladeActive() const { return m_BladeActive != 0 && m_PointCount >= 2; }

    // v1.6.1 SuperFruitControl::Sliced @0x001bb994: binary does a direct same-engine
    // member write `slasher.m_HeadPos.x = 0` (SlashEntity+0x7c) to sever the linked
    // slasher's trail tip. Minimal public mutator exposing that single write without
    // widening access to the whole field or changing its offset/layout. Not gated by
    // __bada__ (unlike GetHeadPos() above) so portable callers compiled under the
    // cross-build can reach it.
    void ClearHeadPosX() { m_HeadPos.x = 0.0f; }

    // v1.6.1 SuperFruitHitControl::RemoveQuickly @0x001bee10: floors m_TailPos.z
    // (SlashEntity+0x78) to 0.8f. (The binary's RemoveQuickly operates on a
    // SlashEntity* reinterpret-cast to a phantom SuperFruitHitControl*.)
    void ClampTailPosZ() { if (m_TailPos.z <= 0.8f) m_TailPos.z = 0.8f; }

    // v1.6.1 SuperFruitControl::Sliced @0x001bbcdc: on slow hardware the binary
    // writes m_PendingSplats (SlashEntity+0x12c) = -1 directly to cancel the
    // pending splat stream. Minimal public mutator exposing that single write.
    void CancelPendingSplats() { m_PendingSplats = -1; }

private:

    // -----------------------------------------------------------------------
    // Private methods
    // -----------------------------------------------------------------------

    // Matches SlashEntity::UpdateTouchDown. Ingests one touch position,
    // interpolating intermediate points along the movement delta.
    void OnTouchActive(float x, float y);

    // Marks blade for deactivation; the trail fades via alpha decay in UpdatePoints.
    void OnTouchReleased();

    // ASM-spec v1.6.1 SlashEntity::AddPoint @0x001e9918 -- appends one vertex pair
    // into the heap buffers. Binary demangled order/types: (center, dir, pressure), all
    // BY VALUE. Guards: IsNearZero(dir) || IsNearZero(m_BladeDir) -> return.
    // Updates ghost ring, m_BladeDir, m_AngleIndex, m_Angle.
    // dir zero-case mutates the LOCAL by-value copy (dir = m_BladeDir) -- since the
    // binary passes dir by value, this mutation is never caller-visible.
    void AddPoint(_Vector3<float> center, _Vector3<float> dir, float pressure);

    // v1.6.1 SlashEntity::UpdatePoints @0x001e6914 -- per-frame full geometry
    // re-derivation (miter, UV, alpha, m_Col, head cap). The ColLine collision
    // segment + m_SegLenSq are built from m_HeadPos/m_TailPos transformed through
    // FruitCamera::TranslatePos(pos, inverse=true, useZeroCenter=false), so the
    // blade's hit segment tracks the drawn blade during camera zoom (identity
    // while m_ZoomT <= 0).
    void UpdatePoints(float dt);

public:
    // -----------------------------------------------------------------------
    // Blade modifier apply functions (called from SlashModInfo::SetEquipped).
    // -----------------------------------------------------------------------

    // Port-added const-correct convenience overload; forwards to the
    // binary-mangled non-const overload (@0x001e7f24) which owns the body.
    static void SetModColours(
        const Colour*  colours,
        int            colourCount,
        int            colourType,
        float          lifeScale,
        const char*    particlePath,
        const char*    textureName2,
        bool           directional,
        const char*    contactParticle,
        const char*    particle2
    );

    static void InitModColours();

    // ResetModScales -- port convenience: folds SetModScales(NULL,1,1,0,1,false,0,0)
    // from ItemManager::SetEquippedItem @0x00139ba0 v1.6.1.
    // No binary SlashEntity::ResetModScales symbol (prior marker was a mis-RE).
    static void ResetModScales();

    // Binary param order @0x1e60a8: (length, thickness, endThickness, pointScale, flipUD, loop, uvNormalLen)
    static void SetModScales(
        float length,
        float thickness,
        float endThickness,
        float pointScale,
        bool  flipUD,
        bool  loop,
        float uvNormalLen
    );

    // ColoursChanged v1.6.1 @ 0x1e76fc. Per-instance live-update.
    void ColoursChanged();

    // @ 0x0016ba84 -- blade pre-pass.
    void PreDraw();

    static const Mortar::SmartPtr<Mortar::Texture>& GetModTexture();
    static uint32_t GetTrailEmitterHash();
    static uint32_t GetContactEmitterHash();
    static uint32_t GetSecondEmitterHash();
    static uint8_t  GetDirectionalFlag();
    static int      GetColourCount();
    static int      GetColourType();
    static const Colour* GetPalette();

    // ---- STUBS (binary) ----

    // ASM-spec v1.6.1 SlashEntity::CollideWithEntity @0x001e6420 -- ColLine vs entity
    // collider. Infinite-line broad phase (ColSphereLine) + m_SegLenSq endpoint bound;
    // returns true iff the blade segment actually reaches the hit chord.
    bool CollideWithEntity(Mortar::Entity* entity);

    // v1.6.1 SlashEntity::CollisionResponse @0x001e616c -- 8 bytes,
    // `mov r0,#0; bx lr`.
    // ASM-verified: 2026-06-07 v1.6.1 SlashEntity::CollisionResponse @ 0x001e616c (re-analyst)
    int CollisionResponse(Mortar::Entity* hitter, unsigned long mask1, unsigned long mask2, _Vector3<float>* bladeVel) override;

    // DrawSlice -- binary @ 0x1e83b0. Main blade render (two mirrored tri-strips).
    // Called from GameDraw's 16-slot loop, NOT from ActorManager::Draw.
    void DrawSlice();

    // Init (3-arg binary form) -- v1.6.1 @ 0x1e7a34. Vtable slot 2.
    // Allocates ColLine (new(0x20)), calls InitPoints(160),
    // inits ghost ring + combo array.
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0017C65C (re-analyst)
    void Init(void* param1, long param2, _Vector3<float>* param3) override;

    // InitPoints -- v1.6.1 @ 0x1e75d0. Heap-allocates m_pLeftBuffer /
    // m_pRightBuffer (each m_SplitPoint+2 = 162 QUADCUSTOMVERTEX records)
    // and fills with zeroed pos/normal.xy, normal.z=1.0, u=0.0, v=0.0, white colour.
    // ASM-verified: 2026-05-18 v1.6.1 InitPoints @ 0x1e75d0 (re-analyst)
    void InitPoints(long count);

    // v1.6.1 SlashEntity::SetModColours @0x001e7f24 -- binary signature (non-const
    // Colour*); owns the real body. The const overload above forwards here.
    static void SetModColours(Colour* colours, int colourCount, int colourType,
                              float lifeScale, const char* particlePath,
                              const char* textureName2, bool directional,
                              const char* contactParticle, const char* particle2);

    // ASM-spec v1.6.1 SlashEntity::TouchDown @0x001ea420
    // Port note: on the stroke-reset branch the port first syncs
    // m_RawTouchPos from the event so a NEW stroke seeds at the press
    // position -- the SDL layer emits no TouchMove on a motionless press
    // (a tap never moves the blade; see InputTranslatorSDL.h). When the
    // bomb-hit latch blocks Reset, no sync happens either: the stale
    // position keeps OnTouchActive in its skip path, so taps append
    // nothing (no post-bomb tap-bridge).
    bool TouchDown(InputEvent* event);

    // ASM-spec v1.6.1 SlashEntity::TouchMoveX @0x001e785c -- writes pos.x.
    bool TouchMoveX(InputEvent* event);

    // ASM-spec v1.6.1 SlashEntity::TouchMoveY @0x001e77b4 -- writes pos.y.
    bool TouchMoveY(InputEvent* event);

    // Binary @ 0x17D2E4 -- UpdateTouchDown: trail builder, forwards to OnTouchActive.
    void UpdateTouchDown(InputEvent* event);

    // Port-only: explicit touch-release handler (SDL FINGERUP/MOUSEBUTTONUP).
    bool TouchUp(InputEvent* event);

    // Port-helper for registering per-finger callbacks on InputManager.
    void RegisterInputCallbacks();

    // Accessor for the file-scope global g_OnComboCancel event (binary GOT 0x332bd8).
    // Binary subscribe sites load [GOT,0x77f8] to get the event address; port uses this
    // accessor for cross-TU subscribe/unsubscribe.
    // DIFFERS: original = direct GOT access on every subscribe site; using static accessor
    // because port has no GOT, preserving single-definition semantics.
    static Mortar::Event1<SlashEntity*>& OnComboCancelEvent();

    // ---- end STUBS ----

#ifdef __bada__
    friend struct SlashEntityLayoutAssert;
#endif
    friend class ComboModifier;
};

#if defined(__bada__)
#include <cstddef>
struct SlashEntityLayoutAssert {
    static_assert(sizeof(SlashEntity)                                        == 0x188, "SlashEntity size");
    static_assert(offsetof(SlashEntity, m_TrailEmitter)                      == 0x3c,  "m_TrailEmitter offset");
    static_assert(offsetof(SlashEntity, m_Scale)                             == 0x40,  "m_Scale offset");
    static_assert(offsetof(SlashEntity, m_BaseColour)                        == 0x44,  "m_BaseColour offset");
    static_assert(offsetof(SlashEntity, m_HighlightColour)                   == 0x48,  "m_HighlightColour offset");
    static_assert(offsetof(SlashEntity, m_BombHitEdge)                       == 0x4c,  "m_BombHitEdge offset");
    static_assert(offsetof(SlashEntity, m_SplitPoint)                        == 0x50,  "m_SplitPoint offset");
    static_assert(offsetof(SlashEntity, m_PointCount)                        == 0x58,  "m_PointCount offset");
    static_assert(offsetof(SlashEntity, m_pLeftBuffer)                       == 0x5c,  "m_pLeftBuffer offset");
    static_assert(offsetof(SlashEntity, m_pRightBuffer)                      == 0x60,  "m_pRightBuffer offset");
    static_assert(offsetof(SlashEntity, m_BladeDir)                          == 0x64,  "m_BladeDir offset");
    static_assert(offsetof(SlashEntity, m_TailPos)                           == 0x70,  "m_TailPos offset");
    static_assert(offsetof(SlashEntity, m_HeadPos)                           == 0x7c,  "m_HeadPos offset");
    static_assert(offsetof(SlashEntity, m_PrevHeadPos)                       == 0x88,  "m_PrevHeadPos offset");
    static_assert(offsetof(SlashEntity, m_SegLenSq)                          == 0x94,  "m_SegLenSq offset");
    static_assert(offsetof(SlashEntity, m_HeadThickScale)                    == 0x98,  "m_HeadThickScale offset");
    static_assert(offsetof(SlashEntity, m_SliceBladeDir)                     == 0x9c,  "m_SliceBladeDir offset");
    static_assert(offsetof(SlashEntity, m_SliceFruitPos)                     == 0xa8,  "m_SliceFruitPos offset");
    static_assert(offsetof(SlashEntity, m_SliceFruitType)                    == 0xb4,  "m_SliceFruitType offset");
    static_assert(offsetof(SlashEntity, m_SwipeSoundTimer)                   == 0xb8,  "m_SwipeSoundTimer offset");
    static_assert(offsetof(SlashEntity, m_GhostDirRing)                      == 0xbc,  "m_GhostDirRing offset");
    static_assert(offsetof(SlashEntity, m_GhostIndex)                        == 0x104, "m_GhostIndex offset");
    static_assert(offsetof(SlashEntity, m_GhostCount)                        == 0x108, "m_GhostCount offset");
    static_assert(offsetof(SlashEntity, m_GhostDir)                          == 0x10c, "m_GhostDir offset");
    static_assert(offsetof(SlashEntity, m_ComboTimer)                        == 0x118, "m_ComboTimer offset");
    static_assert(offsetof(SlashEntity, m_pComboMissControl)                 == 0x11c, "m_pComboMissControl offset");
    static_assert(offsetof(SlashEntity, m_GhostSpawnTimer)                   == 0x120, "m_GhostSpawnTimer offset");
    static_assert(offsetof(SlashEntity, m_GhostSpawnPending)                 == 0x124, "m_GhostSpawnPending offset");
    static_assert(offsetof(SlashEntity, m_pLastComboFruit)                   == 0x128, "m_pLastComboFruit offset");
    static_assert(offsetof(SlashEntity, m_PendingSplats)                     == 0x12c, "m_PendingSplats offset");
    static_assert(offsetof(SlashEntity, m_SplatTimer)                        == 0x130, "m_SplatTimer offset");
    static_assert(offsetof(SlashEntity, m_SplatInterval)                     == 0x134, "m_SplatInterval offset");
    static_assert(offsetof(SlashEntity, m_TrailShiftA)                       == 0x138, "m_TrailShiftA offset");
    static_assert(offsetof(SlashEntity, m_TrailShiftB)                       == 0x13c, "m_TrailShiftB offset");
    static_assert(offsetof(SlashEntity, m_BladeActive)                       == 0x140, "m_BladeActive offset");
    static_assert(offsetof(SlashEntity, m_ComboScoreScale)                   == 0x144, "m_ComboScoreScale offset");
    static_assert(offsetof(SlashEntity, m_field_0x148)                       == 0x148, "m_field_0x148 offset");
    static_assert(offsetof(SlashEntity, m_field_0x14c)                       == 0x14c, "m_field_0x14c offset");
    static_assert(offsetof(SlashEntity, m_ComboFruitTypes)                   == 0x150, "m_ComboFruitTypes offset");
    static_assert(offsetof(SlashEntity, m_ComboCount)                        == 0x178, "m_ComboCount offset");
    static_assert(offsetof(SlashEntity, m_ComboCounter)                      == 0x17c, "m_ComboCounter offset");
    static_assert(offsetof(SlashEntity, m_ComboOnlineMode)                   == 0x180, "m_ComboOnlineMode offset");
    static_assert(offsetof(SlashEntity, m_AngleIndex)                        == 0x184, "m_AngleIndex offset");
};
#endif

// Per-finger SlashEntity instances (binary has SlashEntity[16] @ BSS).
extern SlashEntity* g_pSlashEntities[16];

// Backward-compat: aliased to g_pSlashEntities[0].
extern SlashEntity* g_pSlashEntity;

// v1.6.1 CleanupSlash @ 0x001e8204 -- nulls 3 slash SmartPtr<Texture> globals,
// releases 8 SlashEntityGhost ring entries (deferred: SlashEntityGhost not yet ported),
// clears the loaded flag. Called from GameDestroy after CleanUpSplat.
void CleanupSlash();

#endif
