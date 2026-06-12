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
// Port-only SDL fields (m_FingerId, m_RawTouchPos, m_State, m_bHasHead) are appended
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
//   PlaySwipe       0x17CCDC
//   GetHeadThicknessScale 0x17B87C
//   CreateGhost     0x17B82C
//   MissControlDeleted    0x17B388
//   Draw            0x17B3B8  (1-instruction BX lr stub)
//   CollisionResponse     0x17B3BC  (stub, returns 0)
//   UpdateCollisionLine   0x17B3C0  (4-byte stub, returns 0)
//   DrawUpdate      0x17B398
//

#include "Entity.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "collision/ColSphere.h"
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

    // Binary @ 0x17B3B8 frozen-branch stub -- no deferred post-step work.
    void PostUpdate(float dt) override;

    // Matches SlashEntity::PreUpdate (0x17C584). Ticks ghost frame counters,
    // advances palette cycle, pushes swipe-loop volume to ItemManager.
    void PreUpdate(float dt);

    // Entity vtable slot 5 (+0x14): Draw(Renderer&) override.
    // Binary @ 0x17B3B8 is a 1-instruction BX lr stub -- no-op.
    // ASM-verified: 2026-05-18 binary @ 0x0017B3B8 (re-analyst)
    void Draw(Renderer& r) override;

    // Binary @ 0x17CCDC -- mod-override swipe SFX, else "Sword-swipe-%d" via Rand32.
    void PlaySwipe();

    // Binary @ 0x17B87C -- derive head taper scale.
    float GetHeadThicknessScale() const;

    // Binary @ 0x17B82C -- push next ghost slot, snapshot blade vertex strips.
    // Port specific: SlashEntityGhost ring deferred; body is a no-op stub.
    void CreateGhost();

    // Binary @ 0x17B388 -- clear back-pointer to combo MissControl when deleted.
    void MissControlDeleted(HUDControl* ctrl);

    // Binary @ 0x17B3BC -- entity vtable slot; SlashEntity is pure aggressor.
    int CollisionResponse();

    // Binary @ 0x17B3C0 -- 4-byte stub, returns 0.
    int UpdateCollisionLine(long dt);

    // Binary @ 0x17B398 -- clears g_state.bombSkipFlag, sets needsDrawFlag.
    void DrawUpdate(float dt);

    // Binary @ 0x17B0F4 -- advance palette progress by dt*lifeScale,
    // lerp between consecutive palette entries.
    void UpdateModColour(Colour* outColour, float dt);

    // Test whether the current blade trail intersects a collision sphere.
    // Returns true if any trail segment intersects the sphere. On a hit,
    // writes the segment delta (end - start) into outBladeVel.
    bool CollideWithSphere(const ColSphere& sphere,
                           Vec3& outBladeVel) const;

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

    // +0x4c  uint8_t  m_SwipeEndEdge   bomb-hit one-shot. Set in Update when
    // bomb[0x68]!=0 && bomb[0x88]==0. NOT the swipe fuse (see m_SwipeFuse @+0x140).
    // ASM-verified: 2026-05-18 binary @ 0x0017E424 (re-analyst)
    uint8_t m_SwipeEndEdge;
    uint8_t _pad4d[3];

    // +0x50  int32_t  m_SplitPoint  InitPoints capacity (160 from Init).
    // v1.6.1 @ 0x1e75d0 InitPoints sets m_SplitPoint = splitPoint.
    // ASM-verified: 2026-05-18 binary @ 0x0017C340 (re-analyst)
    int m_SplitPoint;

    // +0x54  int32_t  unknown field
    int _field_0x54;

    // +0x58  int32_t  m_PointCount  count of live vertex pairs in the strips.
    int m_PointCount;

    // +0x5c  QUADCUSTOMVERTEX*  m_pLeftBuffer   heap array of (m_SplitPoint+2) = 162 verts
    // +0x60  QUADCUSTOMVERTEX*  m_pRightBuffer  heap array of 162 verts
    // Each buffer: 162 * 36 = 5832 bytes.
    // ASM-verified: 2026-05-18 binary @ 0x0017C340 (re-analyst)
    QUADCUSTOMVERTEX* m_pLeftBuffer;
    QUADCUSTOMVERTEX* m_pRightBuffer;

    // +0x64  _Vector3<float>  m_BladeDir  normalised blade direction
    Vec3 m_BladeDir;

    // +0x70  _Vector3<float>  m_TailPos  oldest visible trail point position
    Vec3 m_TailPos;

    // +0x7c  _Vector3<float>  m_HeadPos  tip (newest) trail point position
    Vec3 m_HeadPos;

    // +0x88  _Vector3<float>  m_PrevHeadPos  previous frame tip position
    Vec3 m_PrevHeadPos;

    // +0x94  float  m_SegLenSq  squared segment length (Init: -1.0f)
    float m_SegLenSq;
    // +0x98  float  m_HeadThickScale  head thickness scale
    float m_HeadThickScale;
    // +0x9c  int32_t  m_PendingSplats  signed splat-stream counter
    int   m_PendingSplats;

    // +0xa0  float  m_SliceTimerA
    float m_SliceTimerA;

    // +0xa4  float  m_SliceTimerB
    float m_SliceTimerB;

    // +0xa8  _Vector3<float>  m_BladeVelAtSlice
    Vec3 m_BladeVelAtSlice;

    // +0xb4  int32_t  m_SliceEntityType  entity type of the most recently hit entity
    int m_SliceEntityType;

    // +0xb8  float  m_SwipeSoundTimer  cooldown between swipe SFX firings (Init: DAT seed)
    float m_SwipeSoundTimer;

    // +0xbc..+0x103  Ghost direction ring: 6-entry Vec3 ring (stride 0xc = 12).
    // Init zeroes 6 entries. AddPoint writes into slot (m_GhostIndex % 6).
    // +0xbc: ghost ring slot 0 (Vec3)
    // +0xc8: ghost ring slot 1 (Vec3)
    // ... +0xf8: ghost ring slot 5 (Vec3) -- ends at +0x103
    uint8_t _gap_0xbc[72];   // ghost ring storage: 6 * Vec3(12 bytes) = 72 bytes

    // +0x104  uint32_t  m_GhostIndex  current ghost ring-buffer write index
    unsigned int m_GhostIndex;

    // +0x108  uint32_t  m_GhostCount  number of valid ghost entries
    unsigned int m_GhostCount;

    // +0x10c  _Vector3<float>  m_GhostDir  averaged ghost blade direction
    Vec3 m_GhostDir;

    // +0x118  float  (DAT seed -- Init writes from DAT; used in UpdatePoints fade calc)
    float m_field_0x118;

    // +0x11c  Vec3  m_SlicePos  position of most recently sliced entity (for splat/combo)
    Vec3 m_SlicePos;

    // +0x128..+0x12f  8-byte gap
    uint8_t _gap_0x128[8];

    // +0x130  int32_t  m_field_0x130
    int m_field_0x130;

    // +0x134  float  field_0x134  (Init: 0.0f)
    float m_field_0x134;

    // +0x138  int32_t  m_field_0x138  counter, decremented by 2 in AddPoint capacity shift; Init: -1
    int m_field_0x138;

    // +0x13c  int32_t  m_field_0x13c  Init: -1
    int m_field_0x13c;

    // +0x140  int32_t  m_SwipeFuse  DrawSlice shift-register for swipe ghost/SFX burst.
    // DISTINCT from m_SwipeEndEdge (+0x4c, bomb one-shot).
    // DrawSlice: b = m_SwipeFuse & 1; m_SwipeFuse = b<<1; if b==0 -> fire.
    int m_SwipeFuse;

    // +0x144  float  m_field_0x144  Init: 6.0f
    float m_field_0x144;

    // +0x148  int32_t  m_field_0x148  Init: -1
    int m_field_0x148;

    // +0x14c  int32_t  m_field_0x14c  Init: -1
    int m_field_0x14c;

    // +0x150  int32_t[11]  m_ComboSliceArr  11-entry combo storage.
    // v1.6.1 confirmed binary field. Init fills all 11 entries with -1 (0xFFFFFFFF).
    //   [0]..[8]  = fruit types for current combo swipe (int)
    //   [9]       = m_ComboTimer: per-swipe window accumulator (float reinterpret)  +0x174
    //   [10]      = m_ComboCount: fruits sliced in current swipe (int)              +0x178
    // Access [9] as float via m_ComboTimerRef() helper; [10] directly as int.
    // ASM-verified: 2026-05-18 binary @ 0x0017C65C (re-analyst)
    int m_ComboSliceArr[11];

    // Inline helper: returns a reference to m_ComboSliceArr[9] reinterpreted as float.
    // m_ComboTimer (+0x174) aliases element [9] in the binary layout.
    float& m_ComboTimerRef() { return *reinterpret_cast<float*>(&m_ComboSliceArr[9]); }
    float  m_ComboTimerVal() const { return *reinterpret_cast<const float*>(&m_ComboSliceArr[9]); }

    // m_ComboCount (+0x178) is element [10] directly.
    int& m_ComboCountRef()  { return m_ComboSliceArr[10]; }
    int  m_ComboCountVal()  const { return m_ComboSliceArr[10]; }

    // +0x17c  int32_t  m_ComboEntityType  entity type of most recently combo'd fruit
    int m_ComboEntityType;

    // +0x180  void*  m_pComboMissControl  pointer to the MissControl combo-popup slot.
    // ASM-verified: 2026-05-18 binary @ 0x0017C82C (re-analyst)
    MissControl* m_pComboMissControl;

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
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    // SDL finger-slot this instance handles.
    int m_FingerId;

    // Raw touch position from most recent OnTouchActive.
    Vec3 m_RawTouchPos;

    // Port-internal state machine: 0=off, 1=active, 2=fading.
    uint8_t m_State;
    // True while the blade has been seeded with at least one point this swipe.
    bool    m_bHasHead;

public:
    // Back-pointer to the fruit this slasher is aimed at.
    Fruit* m_pCurrentTarget;

    // True while the blade has at least 2 trail points and is not idle.
    bool IsBladeActive() const { return m_State != 0 && m_PointCount >= 2; }

private:
#endif // !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)

    // -----------------------------------------------------------------------
    // Private methods
    // -----------------------------------------------------------------------

    // Matches SlashEntity::UpdateTouchDown. Ingests one touch position,
    // interpolating intermediate points along the movement delta.
    void OnTouchActive(float x, float y);

    // Marks blade for deactivation; the trail fades via alpha decay in UpdatePoints.
    void OnTouchReleased();

    // Binary @ 0x1e9918 (v1.6.1) -- appends one vertex pair into the heap buffers.
    // Signature: pressure FIRST (s0 register), then center, then dir.
    // Guards: IsNearZero(*dir) || IsNearZero(m_BladeDir) -> return.
    // Updates ghost ring, m_BladeDir, m_AngleIndex, m_Angle.
    void AddPoint(float pressure, const Vec3* center, const Vec3* dir);

    // Binary @ 0x1e6914 -- per-frame full geometry re-derivation (miter, UV, alpha,
    // m_Col, head cap). Replaces previous linear-fade approximation.
    void UpdatePoints(float dt);

public:
    // -----------------------------------------------------------------------
    // Blade modifier apply functions (called from SlashModInfo::SetEquipped).
    // -----------------------------------------------------------------------

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

    // ResetModScales -- reset all 6 blade-mod scale fields to 1.0f.
    // ASM-verified: 2026-05-18 binary @ 0x00117a80 / 0x00119b08 (re-analyst)
    static void ResetModScales();

    static void SetModScales(
        float startThick,
        float endThick,
        float scaleLen,
        float uvLen,
        bool  flipUD,
        bool  loop,
        float loopUVLen
    );

    // ColoursChanged @ 0x0017c41c. Per-instance live-update.
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

    // Binary @ 0x17B570 -- tests blade ColLine against entity->m_Col (ColSphere).
    // Port's Update slice loop calls CollideWithSphere() per-entity directly;
    // this binary entry point is unreached.
    bool CollideWithEntity(Mortar::Entity* entity);

    // Binary @ 0x17B3BC -- 2-instruction stub `movs r0,#0; bx lr`.
    // ASM-verified: 2026-06-07 binary @ 0x0017B3BC (re-analyst)
    int CollisionResponse(Mortar::Entity* hitter, unsigned long mask1, unsigned long mask2, Vec3* bladeVel) override;

    // DrawSlice -- binary @ 0x1e83b0. Main blade render (two mirrored tri-strips).
    // Called from GameDraw's 16-slot loop, NOT from ActorManager::Draw.
    void DrawSlice();

    // Init (3-arg binary form) -- v1.6.1 @ 0x1e7a34. Vtable slot 2.
    // Allocates ColLine (new(0x20)), calls InitPoints(160),
    // inits ghost ring + combo array.
    // ASM-verified: 2026-05-18 binary @ 0x0017C65C (re-analyst)
    void Init(void* param1, long param2, Vec3* param3) override;

    // InitPoints -- v1.6.1 @ 0x1e75d0. Heap-allocates m_pLeftBuffer /
    // m_pRightBuffer (each m_SplitPoint+2 = 162 QUADCUSTOMVERTEX records)
    // and fills with zeroed pos/normal, u=1.0f, white colour.
    // ASM-verified: 2026-05-18 binary @ 0x0017C340 (re-analyst)
    void InitPoints(long count);

    // Binary non-const Colour* overload of SetModColours (@ 0x17CA0C).
    static void SetModColours(Colour* colours, int colourCount, int colourType,
                              float lifeScale, const char* particlePath,
                              const char* textureName2, bool directional,
                              const char* contactParticle, const char* particle2);

    // ASM-spec: SlashEntity::TouchDown @ 0x17D61C
    bool TouchDown(InputEvent* event);

    // ASM-spec: SlashEntity::TouchMoveX @ 0x17C50C -- writes pos.x.
    bool TouchMoveX(InputEvent* event);

    // ASM-spec: SlashEntity::TouchMoveY @ 0x17C490 -- writes pos.y.
    bool TouchMoveY(InputEvent* event);

    // Binary @ 0x17D2E4 -- UpdateTouchDown: trail builder, forwards to OnTouchActive.
    void UpdateTouchDown(InputEvent* event);

    // Port-only: explicit touch-release handler (SDL FINGERUP/MOUSEBUTTONUP).
    bool TouchUp(InputEvent* event);

    // Port-helper for registering per-finger callbacks on InputManager.
    void RegisterInputCallbacks();
    // ---- end STUBS ----
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
#include <cstddef>
static_assert(sizeof(SlashEntity)                              == 0x188, "SlashEntity size");
static_assert(offsetof(SlashEntity, m_TrailEmitter)            == 0x3c,  "SlashEntity::m_TrailEmitter");
static_assert(offsetof(SlashEntity, m_Scale)                   == 0x40,  "SlashEntity::m_Scale");
static_assert(offsetof(SlashEntity, m_BaseColour)              == 0x44,  "SlashEntity::m_BaseColour");
static_assert(offsetof(SlashEntity, m_HighlightColour)         == 0x48,  "SlashEntity::m_HighlightColour");
static_assert(offsetof(SlashEntity, m_SwipeEndEdge)            == 0x4c,  "SlashEntity::m_SwipeEndEdge");
static_assert(offsetof(SlashEntity, m_SplitPoint)              == 0x50,  "SlashEntity::m_SplitPoint");
static_assert(offsetof(SlashEntity, m_PointCount)              == 0x58,  "SlashEntity::m_PointCount");
static_assert(offsetof(SlashEntity, m_pLeftBuffer)             == 0x5c,  "SlashEntity::m_pLeftBuffer");
static_assert(offsetof(SlashEntity, m_pRightBuffer)            == 0x60,  "SlashEntity::m_pRightBuffer");
static_assert(offsetof(SlashEntity, m_BladeDir)                == 0x64,  "SlashEntity::m_BladeDir");
static_assert(offsetof(SlashEntity, m_TailPos)                 == 0x70,  "SlashEntity::m_TailPos");
static_assert(offsetof(SlashEntity, m_HeadPos)                 == 0x7c,  "SlashEntity::m_HeadPos");
static_assert(offsetof(SlashEntity, m_PrevHeadPos)             == 0x88,  "SlashEntity::m_PrevHeadPos");
static_assert(offsetof(SlashEntity, m_SegLenSq)                == 0x94,  "SlashEntity::m_SegLenSq");
static_assert(offsetof(SlashEntity, m_HeadThickScale)          == 0x98,  "SlashEntity::m_HeadThickScale");
static_assert(offsetof(SlashEntity, m_PendingSplats)           == 0x9c,  "SlashEntity::m_PendingSplats");
static_assert(offsetof(SlashEntity, m_SliceTimerA)             == 0xa0,  "SlashEntity::m_SliceTimerA");
static_assert(offsetof(SlashEntity, m_SliceTimerB)             == 0xa4,  "SlashEntity::m_SliceTimerB");
static_assert(offsetof(SlashEntity, m_BladeVelAtSlice)         == 0xa8,  "SlashEntity::m_BladeVelAtSlice");
static_assert(offsetof(SlashEntity, m_SliceEntityType)         == 0xb4,  "SlashEntity::m_SliceEntityType");
static_assert(offsetof(SlashEntity, m_SwipeSoundTimer)         == 0xb8,  "SlashEntity::m_SwipeSoundTimer");
static_assert(offsetof(SlashEntity, _gap_0xbc)                 == 0xbc,  "SlashEntity::ghost ring");
static_assert(offsetof(SlashEntity, m_GhostIndex)              == 0x104, "SlashEntity::m_GhostIndex");
static_assert(offsetof(SlashEntity, m_GhostCount)              == 0x108, "SlashEntity::m_GhostCount");
static_assert(offsetof(SlashEntity, m_GhostDir)                == 0x10c, "SlashEntity::m_GhostDir");
static_assert(offsetof(SlashEntity, m_field_0x118)             == 0x118, "SlashEntity::m_field_0x118");
static_assert(offsetof(SlashEntity, m_SlicePos)                == 0x11c, "SlashEntity::m_SlicePos");
static_assert(offsetof(SlashEntity, m_field_0x130)             == 0x130, "SlashEntity::m_field_0x130");
static_assert(offsetof(SlashEntity, m_field_0x134)             == 0x134, "SlashEntity::m_field_0x134");
static_assert(offsetof(SlashEntity, m_field_0x138)             == 0x138, "SlashEntity::m_field_0x138");
static_assert(offsetof(SlashEntity, m_field_0x13c)             == 0x13c, "SlashEntity::m_field_0x13c");
static_assert(offsetof(SlashEntity, m_SwipeFuse)               == 0x140, "SlashEntity::m_SwipeFuse");
static_assert(offsetof(SlashEntity, m_field_0x144)             == 0x144, "SlashEntity::m_field_0x144");
static_assert(offsetof(SlashEntity, m_field_0x148)             == 0x148, "SlashEntity::m_field_0x148");
static_assert(offsetof(SlashEntity, m_field_0x14c)             == 0x14c, "SlashEntity::m_field_0x14c");
static_assert(offsetof(SlashEntity, m_ComboSliceArr)           == 0x150, "SlashEntity::m_ComboSliceArr");
static_assert(offsetof(SlashEntity, m_ComboEntityType)         == 0x17c, "SlashEntity::m_ComboEntityType");
static_assert(offsetof(SlashEntity, m_pComboMissControl)       == 0x180, "SlashEntity::m_pComboMissControl");
static_assert(offsetof(SlashEntity, m_AngleIndex)              == 0x184, "SlashEntity::m_AngleIndex");
#endif

// Per-finger SlashEntity instances (binary has SlashEntity[16] @ BSS).
extern SlashEntity* g_pSlashEntities[16];

// Backward-compat: aliased to g_pSlashEntities[0].
extern SlashEntity* g_pSlashEntity;

#endif
