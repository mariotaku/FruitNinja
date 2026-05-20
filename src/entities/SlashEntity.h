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
    // Port note: m_pComboMissControl doesn't exist in port struct; no-op.
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

    // Binary @ 0x17D61C — Mortar::Entity::TouchDown vtable override: if idle, Reset()
    // and (PER_SWIPE mode) re-pick palette colour, then UpdateTouchDown.
    // Port: maps to OnTouchActive / OnTouchReleased input model.
    // Returns true (consumed).
    // TODO: 0x17D61C — wire when Mortar::Entity vtable input dispatch is ported.

    // Binary @ 0x17C50C — Mortar::Entity::TouchMoveX vtable override: write pos.x.
    // TODO: 0x17C50C — wire when Mortar::Entity vtable input dispatch is ported.

    // Binary @ 0x17C490 — Mortar::Entity::TouchMoveY vtable override: write pos.y.
    // TODO: 0x17C490 — wire when Mortar::Entity vtable input dispatch is ported.

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

    // True while the blade has at least 2 trail points and is not
    // deactivating — used to gate collision checks.
    bool IsBladeActive() const { return m_State != 0 && m_NumPoints >= 2; }

    // +0x134: the fruit this slasher is currently aimed at (back-channel for
    // the KillFruit unlink at Fruit::KillFruit @ 0x00176c8e..0x00176cea).
    // Self-clears when the target calls KillFruit and finds this still set.
    Fruit* m_pCurrentTarget;

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

    // Rebuilds m_pLeftBuffer / m_pRightBuffer vertex buffers from m_Points.
    // Matches SlashEntity::UpdatePoints (0x17B92C) simplified.
    void RebuildGeometry();

    TrailPoint m_Points[MAX_POINTS];
    int m_NumPoints;

    // Binary +0x50: capacity passed to InitPoints; drives heap buffer size.
    // InitPoints @ 0x17C340 sets m_SplitPoint = splitPoint (160 from Init).
    int m_SplitPoint;

    // Binary +0x5c, +0x60: heap-allocated vertex strips.
    // Sized as (m_SplitPoint+2)*sizeof(QUADCUSTOMVERTEX). Allocated in
    // InitPoints, freed in Release. Nulled by ctor and after delete[].
    // ASM-verified: 2026-05-18 binary @ 0x0017C340 (re-analyst)
    QUADCUSTOMVERTEX* m_pLeftBuffer;   // +0x5c heap-allocated by InitPoints
    QUADCUSTOMVERTEX* m_pRightBuffer;  // +0x60 heap-allocated by InitPoints

    // Particle emitter that follows the blade for smoke/sparkle trail.
    // Matches binary +0x3c (m_TrailEmitter). Created on first active touch
    // via PSPParticleManager::AddEmitter, cleared on release.
    PSPParticleEmitter* m_TrailEmitter;

    // Binary +0x44: per-vertex stamped colour (result of white -> m_HighlightColour
    // lerp by (1.0f - m_Scale)). Stamped onto every strip vertex in RebuildGeometry.
    Colour m_BaseColour;       // Binary +0x44

    // Binary +0x48: output of UpdateModColour — the current palette-cycle colour.
    // UpdateModColour writes here; RebuildGeometry reads to derive m_BaseColour.
    Colour m_HighlightColour;  // Binary +0x48

    // 2-bit state machine matching binary m_bBladeActive:
    //   0 = off, 1 = active, 2 = deactivating (fading out)
    uint8_t m_State;
    bool    m_bHasHead;

    // Which SDL finger ID / Bada touch slot this instance handles. Binary
    // has SlashEntity[16] (one per finger); port mirrors via g_pSlashEntities
    // array. Set in Init(int finger). Used by RegisterInputCallbacks to
    // bind only this finger's TouchDown_n / TouchMove_X-Y_n / TouchUp_n.
    int m_FingerId;

    // Binary +0xC4: trail-fade weight in [0, 1]. When 1.0, m_BaseColour = white
    // lerped toward m_HighlightColour by 0 = pure white. When 0 (or less),
    // m_BaseColour = m_HighlightColour directly (fully saturated).
    // Set/decayed by SlashEntity::Update @ 0x17D664.
    // TODO: 0x17D664 -- m_Scale lifecycle (1.0 on critical, -2*dt decay) not yet ported.
    float m_Scale;             // Binary +0xC4

    // Binary +0x148: cooldown timer between swipe SFX firings. PlaySwipe
    // (binary @ 0x17ccdc) resets to 6.0f after firing; per-frame decrement
    // (1.0f units / call) prevents back-to-back-to-back swipe sounds when
    // a single drag slices multiple fruits in quick succession (~0.1s @
    // 60Hz). Update ticks the decrement at top.
    float m_SwipeSoundTimer;

    // Raw touch position from the most recent OnTouchActive — used as the
    // trail emitter position so particles spawn at the true finger location,
    // not the last interpolated trail point (which can lag by up to
    // POINT_SPACING=64 units on fast swipes). Matches binary UpdateTouchDown
    // @ 0x17D2E4 which writes `m_TrailEmitter->m_Pos = this->base.pos`,
    // where base.pos is the raw touch input.
    Vec3 m_RawTouchPos;

    // Binary +0x130: dead member — only ever written (to 0) by ctor.
    // No reads anywhere in binary. Preserved for layout.
    // Defunct member: relic of removed feature; only ever written (to 0) by ctor.
    // ASM-verified: 2026-05-18 binary @ 0x0017C82C (re-analyst)
    uint32_t field_0x130;

    // Binary +0x124: combo-window accumulator. Initialized to 0.1f in Init.
    // Ticks up each Update while >= 0; reset to -1 when combo window closes.
    // The per-swipe combo counter fires AddSpeed when this >= 0 and ComboCount > 2.
    float m_ComboTimer;         // Binary +0x124

    // Binary +0x128: count of fruits sliced in the current swipe combo.
    // Incremented by CollisionResponse via the combo-slice array;
    // reset to 0 when a new swipe starts.
    int m_ComboCount;           // Binary +0x128

    // Binary +0x12c: entity type of the most recently combo'd fruit.
    int m_ComboEntityType;      // Binary +0x12c

    // Binary +0x154: 11-entry int32 combo-slice array. Each slot stores a
    // fruit-entity-type tag for the combo's sliced entities; -1 = empty.
    // m_ComboSliceArr[1] (binary +0x158) gates the AddSpeed call in Update:
    // when >= 0, a secondary slice has registered and the combo is live.
    // ASM-verified: 2026-05-18 binary @ 0x0017C65C (re-analyst)
    int m_ComboSliceArr[11]; // Binary +0x154 .. +0x17c

    // Binary +0x144: 2-bit shift-register fuse for "swipe just ended".
    // Writer (outside SlashEntity): sets bit0 on finger-lift (m_SwipeEndEdge |= 1).
    // Reader: DrawSlice shifts left each frame; fires CreateGhost + contact burst
    // on the frame when the last bit falls off.
    // ASM-verified: 2026-05-18 binary @ 0x0017E424 (re-analyst)
    uint8_t m_SwipeEndEdge;

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

    // STUB: SlashEntity::AddPoint -- binary @ 0x17CE0C (TODO RE)
    void AddPoint(Vec3 pos, Vec3 dir, float unused);

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

// Per-finger SlashEntity instances (binary has SlashEntity[16] @ BSS).
// Created/destroyed by GameInit/GameDestroy. Each registers for its slot's
// per-finger TouchDown_n/TouchMove_*n/TouchUp_n callbacks.
extern SlashEntity* g_pSlashEntities[16];

// Backward-compat: aliased to g_pSlashEntities[0]. Existing one-shot uses
// (e.g. ColoursChanged from blade-equip) operate on slot 0; for ops that
// must affect all trails, iterate g_pSlashEntities directly.
extern SlashEntity* g_pSlashEntity;

#endif
