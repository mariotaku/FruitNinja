#ifndef FN_SLASH_ENTITY_H
#define FN_SLASH_ENTITY_H

//
// SlashEntity — blade trail visual (entity type 3)
// Matches binary 0x17C82C..0x17E504
//
// Port note: the binary's SlashEntity is an Mortar::Entity subclass with vtable slots
// for Draw/Update/CollisionResponse/DrawUpdate/TouchDown/TouchMoveX/TouchMoveY.
// This port implements SlashEntity as a standalone class (not Mortar::Entity-derived)
// with equivalent behaviour via a different internal representation
// (TrailPoint[] instead of binary's m_pLeftBuffer/m_pRightBuffer vertex arrays).
// Fields that exist in the binary but not in this port are documented as TODO.
//
// Binary addresses (ARM32):
//   LoadContent          0x17C948
//   Init                 0x17C65C
//   InitPoints           0x17C340
//   Release              0x17C60C
//   Reset                0x17B71C
//   AddPoint             0x17CE0C
//   UpdateTouchDown      0x17D2E4
//   UpdatePoints         0x17B92C
//   Update               0x17D664
//   PreUpdate            0x17C584
//   DrawSlice            0x17E424
//   Draw                 0x17B3B8  (1-instruction BX lr stub — rendering is in DrawSlice)
//   CollisionResponse    0x17B3BC  (1-instruction stub, returns 0)
//   UpdateCollisionLine  0x17B3C0  (4-byte stub, returns 0)
//   DrawUpdate           0x17B398  (sets g_state.bombSkipFlag=0, g_state.needsDrawFlag=1)
//   MissControlDeleted   0x17B388
//   TouchDown            0x17D61C
//   TouchMoveX           0x17C50C
//   TouchMoveY           0x17C490
//   CreateGhost          0x17B82C
//   PlaySwipe            0x17CCDC
//   GetHeadThicknessScale 0x17B87C
//   ~SlashEntity         0x17C774
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
    static const float POINT_SPACING;        // 64.0 — units between interpolated points
    static const float MOVE_THRESH_ACTIVE;   // 5.0  — min move² = 25 to add point
    static const float MOVE_THRESH_INACTIVE; // 50.0 — min move² = 2500 when blade off

    // Global SlashEntity::ModPowerMask (binary BSS 0x0024d8cc). A uint32_t
    // bitmask that active SlashModifier instances OR their bits into each
    // frame; cleared at the top of PowerUpManager::Update via SetDefaults.
    // Callers (Fruit, Bomb, ScrollingMenu) gate behaviours on individual
    // bits — see SlashModifier.h for the bit table.
    //
    // Port: PowerUpManager isn't ported yet, so nothing actually sets any
    // bits here and the mask stays 0 — existing gameplay sees no change.
    // The mask + helpers are wired so porting SlashModifier/PowerUpManager
    // lights the gated behaviours up automatically.
    static uint32_t s_ModPowerMask;

    SlashEntity();
    ~SlashEntity();

    // One-time global content load — matches 0x17C948. Loads blade.tex.
    static void LoadContent();
    static void ReleaseContent();

    // Matches SlashEntity::Init (0x17C65C). Allocates vertex buffers, resets state.
    // fingerId selects which of the 16 SDL finger / Bada touch slots this
    // instance receives events from. GameInit creates one per slot 0..15.
    void Init(int fingerId = 0);
    void Release() override;

    // Binary @ 0x17B71C — wipe touch/trail state, sentinel-mark positions,
    // clear vertex strips, clear 11-entry combo-slice array.
    // Port note: binary fields m_pLeftBuffer/m_pRightBuffer/m_SplitPoint/
    //   m_SliceFruitTypes don't exist in port; only the port-equivalent state
    //   (m_NumPoints, m_State, m_bHasHead) is cleared.
    void Reset();

    // Matches SlashEntity::Update (0x17D664). Polls Mortar::Touch slot 0 +
    // per-frame geometry rebuild.
    void Update(float dt) override;

    // Binary @ 0x17B3B8 frozen-branch: GameUpdate calls PostUpdate(0.0f) on
    // each slot when game is inactive (active branch drives via ActorManager).
    // No-op stub — SlashEntity has no deferred post-step work in the port.
    void PostUpdate(float dt) override;

    // Matches SlashEntity::PreUpdate (0x17C584). Ticks ghost frame counters,
    // advances palette cycle, pushes swipe-loop volume to ItemManager.
    void PreUpdate(float dt);

    // Entity vtable slot 5 (+0x14): Draw(Renderer&) override.
    // Binary Draw @ 0x17B3B8 is a 1-instruction BX lr stub -- no-op.
    // All blade rendering goes through DrawSlice, dispatched explicitly
    // from GameDraw (binary @ 0x0016b888 vtable-loop over 16 slots).
    // ASM-verified: 2026-05-18 binary @ 0x0017B3B8 (re-analyst)
    void Draw(Renderer& r) override;

    // Binary @ 0x17B3B8 — Draw is a 1-instruction BX lr stub; rendering is
    // in DrawSlice. Port's Draw() maps to DrawSlice behaviour.
    // No separate entry point needed.

    // Binary @ 0x17CCDC — mod-override swipe SFX, else "bigslice1..6" via Rand32.
    void PlaySwipe();

    // Binary @ 0x17B87C — derive head taper scale = lastPairHalfWidth /
    // (modScale.startThick * 9), clamped to >= 1.
    float GetHeadThicknessScale() const;

    // Binary @ 0x17B82C — push next ghost slot in 8-entry ring, snapshot
    // blade vertex strips for fade-out replay.
    // Port specific: SlashEntityGhost ring deferred; body is a no-op stub.
    void CreateGhost();

    // Binary @ 0x17B388 — clear back-pointer to combo MissControl when deleted.
    void MissControlDeleted(HUDControl* ctrl);

    // Binary @ 0x17B3BC — entity vtable slot; SlashEntity is pure aggressor,
    // never collides into. Returns 0.
    // Port note: non-vtable convenience overload (no args). The 4-arg vtable
    // override is declared below in the STUBS section.
    int CollisionResponse();

    // Binary @ 0x17B3C0 — 4-byte stub, returns 0.
    int UpdateCollisionLine(long dt);

    // Binary @ 0x17B398 — clears g_state.bombSkipFlag=0, sets needsDrawFlag=1.
    // Port note: g_state singleton not yet modelled; no-op stub.
    void DrawUpdate(float dt);

    // Binary @ 0x17D61C — TouchDown: gate m_SwipeEndEdge==0 && m_State==0; Reset();
    // if PER_SWIPE colour mode advance palette; call UpdateTouchDown. Returns true.
    // Port: wired via InputManager::RegisterInputCallback("TouchDown_<n>") in
    // SlashEntity::RegisterInputCallbacks (SlashEntity.cpp:205). Declared below
    // in STUBS section.
    // ASM-verified: 2026-05-20 binary @ 0x0017D61C (re-analyst)

    // Binary @ 0x17C50C — TouchMoveX: write m_RawTouchPos.x from event->x.
    // Port: wired via InputManager::RegisterInputCallback("TouchMove_X<n>").
    // ASM-verified: 2026-05-20 binary @ 0x0017C50C (re-analyst)

    // Binary @ 0x17C490 — TouchMoveY: write m_RawTouchPos.y from event->y.
    // Port: wired via InputManager::RegisterInputCallback("TouchMove_Y<n>").
    // ASM-verified: 2026-05-20 binary @ 0x0017C490 (re-analyst)

    // Binary @ 0x17B0F4 — advance palette progress by dt*lifeScale,
    // lerp between consecutive palette entries. NULL outColour = advance only.
    void UpdateModColour(Colour* outColour, float dt);

    // Test whether the current blade trail intersects a collision sphere.
    // Iterates every segment between consecutive trail points (mirrors the
    // binary's CollideWithEntity at 0x17B570, simplified to iterate the
    // full trail instead of just the m_HeadPos/m_TailPos pair — needed
    // because OnTouchActive may interpolate many points in one frame on
    // fast swipes). Returns true on the first intersecting segment.
    // Returns true if any trail segment intersects the sphere. On a hit,
    // writes the segment delta (end - start) into outBladeVel so the caller
    // can derive both magnitude and direction for OnSliced.
    bool CollideWithSphere(const ColSphere& sphere,
                           Vec3& outBladeVel) const;

private:
    // -----------------------------------------------------------------------
    // Binary-faithful data members (binary truth from SlashEntity.json).
    // Declaration order matches binary offset order; __bada__ static_asserts
    // below confirm every offset.  sizeof(Entity)==0x3C on ARM32, so these
    // own fields start at absolute offset 0x3C in the full object.
    // -----------------------------------------------------------------------

    // +0x3c  PSPParticleEmitter*  m_TrailEmitter
    PSPParticleEmitter* m_TrailEmitter;

    // +0x40  float  m_Scale  hit-flash weight [0..1]; set 1.0 on critical hit,
    // decays by -2*dt/frame; drives m_BaseColour lerp in RebuildGeometry.
    float m_Scale;             // binary +0x40

    // +0x44  Colour  m_BaseColour    per-vertex stamped colour (lerp result)
    Colour m_BaseColour;       // binary +0x44

    // +0x48  Colour  m_HighlightColour   current palette-cycle output colour
    Colour m_HighlightColour;  // binary +0x48

    // +0x4c  uint8_t  m_bFlag4c   (meaning TBD per binary; port uses m_SwipeEndEdge)
    // +0x4d..+0x4f  implicit 3-byte padding for int32_t alignment at +0x50
    uint8_t m_SwipeEndEdge;    // binary +0x4c
    // ASM-verified: 2026-05-18 binary @ 0x0017E424 (re-analyst)
    uint8_t _pad4d[3];

    // +0x50  int32_t  m_SplitPoint  InitPoints capacity (160 from Init)
    // InitPoints @ 0x17C340 sets m_SplitPoint = splitPoint.
    // ASM-verified: 2026-05-18 binary @ 0x0017C340 (re-analyst)
    int m_SplitPoint;          // binary +0x50

    // +0x54  int32_t  unknown field (gap in struct-DB; 4 bytes)
    int _field_0x54;

    // +0x58  int32_t  m_PointCount  count of valid points in vertex strips
    int m_PointCount;          // binary +0x58

    // +0x5c  void*  m_pLeftBuffer   heap-allocated vertex strip (left side)
    // +0x60  void*  m_pRightBuffer  heap-allocated vertex strip (right side)
    // Sized as (m_SplitPoint+2)*sizeof(QUADCUSTOMVERTEX).
    // ASM-verified: 2026-05-18 binary @ 0x0017C340 (re-analyst)
    QUADCUSTOMVERTEX* m_pLeftBuffer;   // binary +0x5c
    QUADCUSTOMVERTEX* m_pRightBuffer;  // binary +0x60

    // +0x64  _Vector3<float>  m_BladeDir  normalised blade direction
    Vec3 m_BladeDir;           // binary +0x64

    // +0x70  _Vector3<float>  m_TailPos  oldest visible trail point position
    Vec3 m_TailPos;            // binary +0x70

    // +0x7c  _Vector3<float>  m_HeadPos  tip (newest) trail point position
    Vec3 m_HeadPos;            // binary +0x7c

    // +0x88  _Vector3<float>  m_PrevHeadPos  previous frame tip position
    Vec3 m_PrevHeadPos;        // binary +0x88

    // +0x94  float  m_SegLenSq  squared segment length (written by UpdatePoints/Init as field_0x94)
    float m_SegLenSq;        // binary +0x94
    // +0x98  float  m_HeadThickScale  head thickness scale (written by UpdatePoints/Update as field_0x98)
    float m_HeadThickScale;  // binary +0x98
    // +0x9c  int32_t  m_PendingSplats  signed splat-stream counter (splat-stream loop)
    int   m_PendingSplats;   // binary +0x9c

    // +0xa0  float  m_SliceTimerA  slice-hit timer A
    float m_SliceTimerA;       // binary +0xa0

    // +0xa4  float  m_SliceTimerB  slice-hit timer B
    float m_SliceTimerB;       // binary +0xa4

    // +0xa8  _Vector3<float>  m_BladeVelAtSlice  blade direction at slice impact
    // Written in Update fruit-collision branch; read for spawn-type tagging.
    Vec3 m_BladeVelAtSlice;    // binary +0xa8

    // +0xb4  _Vector3<float>  m_SlicePos  fruit world-position at impact
    // Read by combo Coin::MakeCoins as coin spawn origin.
    Vec3 m_SlicePos;           // binary +0xb4

    // +0xc0  int32_t  m_SliceEntityType  m_FruitType of most-recently sliced entity
    int m_SliceEntityType;     // binary +0xc0

    // +0xc4  float  m_SwipeSoundTimer  cooldown between swipe SFX firings;
    // PlaySwipe resets to 6.0f after firing; decremented each frame.
    float m_SwipeSoundTimer;   // binary +0xc4

    // +0xc8  float  m_LineLengthSq  squared length of current blade segment
    // DIFFERS: binary reuses +0xc8..+0xd3 as ghost-ring slot 0 (zeroed by Init's
    // ghost-ring loop over +0xc8..+0x10f). These three field names share storage
    // with the ring and must not be given independent live semantics without RE.
    float m_LineLengthSq;      // binary +0xc8

    // +0xcc  float  m_SpeedScale   blade speed scale derived from segment length
    // Note: shares storage with ghost-ring slot 0 — see m_LineLengthSq comment.
    float m_SpeedScale;        // binary +0xcc

    // +0xd0  int32_t  m_SliceCount  total slices on current swipe
    // Note: shares storage with ghost-ring slot 0 — see m_LineLengthSq comment.
    int m_SliceCount;          // binary +0xd0

    // +0xd4..+0x10f  60-byte gap (likely ghost ring-buffer data; meaning TBD)
    uint8_t _gap_0xd4[60];

    // +0x110  int32_t  m_GhostIndex  current ghost ring-buffer write index
    int m_GhostIndex;          // binary +0x110

    // +0x114  int32_t  m_GhostCount  number of valid ghost entries
    int m_GhostCount;          // binary +0x114

    // +0x118  _Vector3<float>  m_GhostDir  ghost blade direction
    Vec3 m_GhostDir;           // binary +0x118

    // +0x124..+0x12f  12-byte gap (meaning TBD)
    uint8_t _gap_0x124[12];

    // +0x130  int32_t  field_0x130  (meaning TBD; zeroed by ctor)
    int m_field_0x130;         // binary +0x130

    // +0x134..+0x14b  24-byte gap (meaning TBD; possibly MissControl/combo state)
    uint8_t _gap_0x134[24];

    // +0x14c  int32_t  m_ExtraFieldA
    int m_ExtraFieldA;         // binary +0x14c

    // +0x150  int32_t  m_ExtraFieldB
    int m_ExtraFieldB;         // binary +0x150

    // +0x154..+0x173  32-byte gap (likely 8-entry combo fruit-type ring; TBD)
    // Port-note: the port uses m_ComboSliceArr[11] in #ifndef __bada__
    // which covers this gap plus the three fields below (+0x174/+0x178/+0x17c).
    uint8_t _gap_0x154[32];

    // +0x174  float  m_ComboTimer  combo-window accumulator; init 0.1f in Init,
    // ticks up each Update; reset to -1 when combo window closes.
    // ASM-verified: 2026-05-18 binary @ 0x0017C65C (re-analyst)
    float m_ComboTimer;        // binary +0x174

    // +0x178  int32_t  m_ComboCount  fruits sliced in current swipe combo
    int m_ComboCount;          // binary +0x178

    // +0x17c  int32_t  m_ComboEntityType  entity type of most recently combo'd fruit
    int m_ComboEntityType;     // binary +0x17c

    // +0x180  void*  m_pComboCtrl  pointer to the MissControl combo-popup slot
    // Cleared by MissControlDeleted when the pool slot is recycled.
    // Binary names this m_pComboCtrl; port names it m_pComboMissControl.
    // ASM-verified: 2026-05-18 binary @ 0x0017C82C (re-analyst)
    MissControl* m_pComboMissControl;  // binary +0x180

    // -----------------------------------------------------------------------
    // Port-only fields — NOT in the 388-byte binary struct.
    // Under __bada__ these are excluded so the binary-faithful layout above
    // passes the static_assert checks at the bottom of this file.
    // -----------------------------------------------------------------------
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    // Stored per-point metadata. The vertex buffers m_Left/m_Right are
    // regenerated from this list each frame in RebuildGeometry.
    struct TrailPoint {
        Vec3  center;    // position in centred ortho coords
        Vec3  dir;       // normalised incoming direction (from previous point)
        float arcLen;    // cumulative length from oldest point
        float age;       // seconds since this point was added (drops at lifetime)
    };

    // Port-internal trail: inline ring of arc-sampled points used to rebuild
    // the m_pLeftBuffer / m_pRightBuffer vertex strips each frame.
    // Binary stores trail state in the heap-allocated m_pLeftBuffer /
    // m_pRightBuffer directly and does not use a separate point ring.
    TrailPoint m_Points[MAX_POINTS];
    int m_NumPoints;

    // Port-internal state machine (2-bit): 0=off, 1=active, 2=fading.
    // Binary uses m_SwipeEndEdge (binary +0x4c) for equivalent gating.
    uint8_t m_State;
    bool    m_bHasHead;

    // SDL finger-slot this instance handles (binary has SlashEntity[16] @ BSS).
    int m_FingerId;

    // Raw touch position from most recent OnTouchActive — used as the trail
    // emitter position so particles spawn at the true finger location.
    Vec3 m_RawTouchPos;

    // Port-internal combo slice array: 11 int32 slots covering binary
    // offsets +0x154..+0x17f (the _gap_0x154 region + m_ComboTimer/Count/Type).
    // Under __bada__ the binary-faithful decomposed fields above are used instead.
    // ASM-verified: 2026-05-18 binary @ 0x0017C65C (re-analyst)
    int m_ComboSliceArr[11];

public:
    // Back-pointer to the fruit this slasher is aimed at.
    // (binary equivalent is via the combo-ctrl/miss-ctrl path; no direct
    // Fruit* pointer exists in the 388-byte binary struct.)
    Fruit* m_pCurrentTarget;

    // True while the blade has at least 2 trail points and is not
    // deactivating — used to gate collision checks.
    bool IsBladeActive() const { return m_State != 0 && m_NumPoints >= 2; }

private:
#endif // !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)

    // -----------------------------------------------------------------------
    // Private methods
    // -----------------------------------------------------------------------

    // Matches SlashEntity::UpdateTouchDown (0x17D2E4). Ingests one touch
    // position, interpolating intermediate points along the movement delta.
    void OnTouchActive(float x, float y);

    // Matches SlashEntity::TouchReleased. Marks blade for deactivation;
    // the trail fades out via shift-drop over subsequent Update ticks.
    void OnTouchReleased();

    // Matches SlashEntity::AddPoint (0x17CE0C). Appends one TrailPoint.
    // Binary signature: AddPoint(_Vector3<float>, _Vector3<float>, float)
    // Vec3s by VALUE (HFA -> s0/s1/s2 and s3/s4/s5); trailing float is the
    // pressure/thickness param (semantic TBD, currently unused by body).
    // ASM-verified: 2026-05-24 binary @ 0x0016ce0c (re-analyst)
    // TODO: 0x0016ce0c -- semantic of the third `float` arg still needs
    //   RE -- likely thickness/pressure for ghost-trail thickness.
    void AddPoint(Vec3 pos, Vec3 dir, float pressure = 0.0f);

    // Rebuilds m_pLeftBuffer / m_pRightBuffer vertex buffers from m_Points.
    // Matches SlashEntity::UpdatePoints (0x17B92C) simplified.
    void RebuildGeometry();

public:
    // -----------------------------------------------------------------------
    // Blade modifier (SlashModInfo) apply functions.
    // Called by SlashModInfo::SetEquipped / ItemManager::SetEquippedItem.
    // Binary: SetModColours @ 0x0017ca0c (thunk 0x000f870c),
    //         InitModColours @ 0x0017cc38 (thunk 0x000f77b8),
    //         SetModScales   @ 0x0017b328 (thunk 0x000fada0).
    // All operate on the global g_pSlashEntity singleton (double-dereferenced
    // through GOT in binary). Port: operate on g_pSlashEntity directly.
    // -----------------------------------------------------------------------

    // SetModColours @ 0x0017ca0c
    // Copies colour palette, loads blade overlay texture, resolves particle
    // emitter hashes, and (if game active) notifies type-3 actors via
    // ColoursChanged(). Full RE in docs/structs/items.md §SetModColours.
    // TODO: implement when particle manager + actor iterate + blade overlay
    //       texture loading are all wired.
    static void SetModColours(
        const Colour*  colours,          // param_1 -- Colour array (NULL if count==0)
        int            colourCount,      // param_2 -- number of entries in colours[]
        int            colourType,       // param_3 -- NONE=0, PER_SLASH=1, etc.
        float          lifeScale,        // param_4 -- particle life scale factor
        const char*    particlePath,     // param_5 -- trail emitter name (e.g. "tex_sparkle")
        const char*    textureName2,     // param_6 -- blade overlay texture name
        bool           directional,      // param_7 -- directional particles flag
        const char*    contactParticle,  // param_8 -- contact particle emitter name
        const char*    particle2         // param_9 -- second particle emitter name
    );

    // InitModColours @ 0x0017cc38
    // Resets all mod-colour state to defaults: clears palette, nulls texture
    // SmartPtr, resets particle hashes and emitter-type flags.
    // Binary: `this` param is ignored -- accesses global singleton directly.
    // TODO: implement when blade colour palette / overlay texture wiring lands.
    static void InitModColours();

    // ResetModScales — reset all 6 blade-mod scale fields to 1.0f.
    // Called by PowerUpManager::SetDefaults and ::Reset to undo any active SlashModifier.
    // ASM-verified: 2026-05-18 binary @ 0x00117a80 / 0x00119b08 (re-analyst)
    static void ResetModScales();

    // SetModScales @ 0x0017b328
    // Writes trail thickness/length/UV scale fields into the global singleton.
    // Default no-mod call: SetModScales(1.0f, 1.0f, 0.0f, 1.0f, false, false, 0.0f).
    static void SetModScales(
        float startThick,  // param_1 -- start (head) thickness scale
        float endThick,    // param_2 -- end (tail) thickness scale
        float scaleLen,    // param_3 -- trail length scale
        float uvLen,       // param_4 -- UV length scale
        bool  flipUD,      // param_5 -- flip for upside-down
        bool  loop,        // param_6 -- loop texture
        float loopUVLen    // param_7 -- loop UV length
    );

    // ColoursChanged @ 0x0017c41c. Per-instance live-update — fired by
    // SetModColours's actor walker on every active SlashEntity. NOT virtual.
    void ColoursChanged();

    // @ 0x0016ba84 — blade pre-pass (sets up blend state before actor draw).
    void PreDraw();

    // Accessors for the file-scope blade-mod globals. Render consumers in
    // SlashEntity.cpp use these instead of direct global access so they can
    // be tested in isolation. Defined inline in the .cpp.
    static const Mortar::SmartPtr<Mortar::Texture>& GetModTexture();
    static uint32_t GetTrailEmitterHash();
    static uint32_t GetContactEmitterHash();
    static uint32_t GetSecondEmitterHash();
    static uint8_t  GetDirectionalFlag();
    static int      GetColourCount();
    static int      GetColourType();
    static const Colour* GetPalette();

    // ---- STUBS (binary) ----
    // Binary-faithful overloads whose signatures differ from the port-internal
    // equivalents above. All bodies are no-ops pending full RE+port.

    // (SlashEntity::AddPoint declared above with canonical 3-arg signature.)

    // STUB: SlashEntity::CollideWithEntity -- binary @ 0x17B570 (TODO RE)
    bool CollideWithEntity(Mortar::Entity* entity);

    // STUB: SlashEntity::CollisionResponse (4-arg vtable override) -- binary @ 0x17B3BC (TODO RE)
    int CollisionResponse(Mortar::Entity* hitter, unsigned long mask1, unsigned long mask2, Vec3* bladeVel) override;

    // DrawSlice -- binary @ 0x17E424. Main blade render (two mirrored tri-strips).
    // Called explicitly from GameDraw's 16-slot vtable loop (binary @ 0x0016b888),
    // NOT from ActorManager::Draw which hits the BX lr Draw stub instead.
    // ASM-verified: 2026-05-18 binary @ 0x0017E424 (re-analyst)
    void DrawSlice();

    // Init (3-arg binary form) -- binary @ 0x17C65C. Vtable slot 2.
    // Allocates ColLine, calls InitPoints(160), inits ghost ring + combo array.
    // ASM-verified: 2026-05-18 binary @ 0x0017C65C (re-analyst)
    void Init(void* param1, long param2, Vec3* param3) override;

    // InitPoints -- binary @ 0x17C340. Heap-allocates m_pLeftBuffer /
    // m_pRightBuffer and fills with sentinel/white records.
    // ASM-verified: 2026-05-18 binary @ 0x0017C340 (re-analyst)
    void InitPoints(long count);

    // STUB: SlashEntity::SetModColours (non-const Colour* binary form) -- binary @ 0x17CA0C (TODO RE)
    static void SetModColours(Colour* colours, int colourCount, int colourType,
                              float lifeScale, const char* particlePath,
                              const char* textureName2, bool directional,
                              const char* contactParticle, const char* particle2);

    // ASM-spec: SlashEntity::TouchDown @ 0x17D61C
    // Press-edge handler. Reset() trail on idle; advances PER_SWIPE palette.
    bool TouchDown(InputEvent* event);

    // ASM-spec: SlashEntity::TouchMoveX @ 0x17C50C -- writes pos.x.
    bool TouchMoveX(InputEvent* event);

    // ASM-spec: SlashEntity::TouchMoveY @ 0x17C490 -- writes pos.y.
    bool TouchMoveY(InputEvent* event);

    // STUB: SlashEntity::UpdatePoints -- binary @ 0x17B92C (TODO RE)
    void UpdatePoints(float dt);

    // ASM-spec: SlashEntity::UpdateTouchDown (InputEvent* form) @ 0x17D2E4
    // Trail-builder; forwards to OnTouchActive(pos.x, pos.y).
    void UpdateTouchDown(InputEvent* event);

    // Port-only release handler. Binary doesn't register a TouchReleased_n
    // event (Bada delivers move events as TouchDown_n; release just stops
    // them). SDL has explicit FINGERUP/MOUSEBUTTONUP -> InputTranslatorSDL
    // dispatches TouchUp_n which routes here for the trail-fade transition.
    bool TouchUp(InputEvent* event);

    // Port-helper -- binary equivalent is GameTaskInitInput @ 0x00169670 which
    // registers per-finger callbacks on InputManager. Called from Init().
    void RegisterInputCallbacks();
    // ---- end STUBS ----
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
#include <cstddef>
static_assert(sizeof(SlashEntity)                              == 0x184, "SlashEntity size");
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
static_assert(offsetof(SlashEntity, m_SlicePos)                == 0xb4,  "SlashEntity::m_SlicePos");
static_assert(offsetof(SlashEntity, m_SliceEntityType)         == 0xc0,  "SlashEntity::m_SliceEntityType");
static_assert(offsetof(SlashEntity, m_SwipeSoundTimer)         == 0xc4,  "SlashEntity::m_SwipeSoundTimer");
static_assert(offsetof(SlashEntity, m_LineLengthSq)            == 0xc8,  "SlashEntity::m_LineLengthSq");
static_assert(offsetof(SlashEntity, m_SpeedScale)              == 0xcc,  "SlashEntity::m_SpeedScale");
static_assert(offsetof(SlashEntity, m_SliceCount)              == 0xd0,  "SlashEntity::m_SliceCount");
static_assert(offsetof(SlashEntity, m_GhostIndex)              == 0x110, "SlashEntity::m_GhostIndex");
static_assert(offsetof(SlashEntity, m_GhostCount)              == 0x114, "SlashEntity::m_GhostCount");
static_assert(offsetof(SlashEntity, m_GhostDir)                == 0x118, "SlashEntity::m_GhostDir");
static_assert(offsetof(SlashEntity, m_field_0x130)             == 0x130, "SlashEntity::m_field_0x130");
static_assert(offsetof(SlashEntity, m_ExtraFieldA)             == 0x14c, "SlashEntity::m_ExtraFieldA");
static_assert(offsetof(SlashEntity, m_ExtraFieldB)             == 0x150, "SlashEntity::m_ExtraFieldB");
static_assert(offsetof(SlashEntity, m_ComboTimer)              == 0x174, "SlashEntity::m_ComboTimer");
static_assert(offsetof(SlashEntity, m_ComboCount)              == 0x178, "SlashEntity::m_ComboCount");
static_assert(offsetof(SlashEntity, m_ComboEntityType)         == 0x17c, "SlashEntity::m_ComboEntityType");
static_assert(offsetof(SlashEntity, m_pComboMissControl)       == 0x180, "SlashEntity::m_pComboMissControl");
#endif

// Per-finger SlashEntity instances (binary has SlashEntity[16] @ BSS).
// Created/destroyed by GameInit/GameDestroy. Each registers for its slot's
// per-finger TouchDown_n/TouchMove_*n/TouchUp_n callbacks.
extern SlashEntity* g_pSlashEntities[16];

// Backward-compat: aliased to g_pSlashEntities[0]. Existing one-shot uses
// (e.g. ColoursChanged from blade-equip) operate on slot 0; for ops that
// must affect all trails, iterate g_pSlashEntities directly.
extern SlashEntity* g_pSlashEntity;

#endif
