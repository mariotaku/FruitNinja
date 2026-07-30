#ifndef FN_MISS_CONTROL_H
#define FN_MISS_CONTROL_H

//
// MissControl : HUDControl3d (size = 0x94)
// Overlay label that displays "critical" / "rare" / "X" text at a slice
// point. Binary keeps a 12-slot pool shared across all MissControl
// triggers. Same pool services Fruit critical/rare slices, Bomb zen-hit
// "X", and combo indicators (combo_3.tex .. combo_10.tex).
//
// v1.6.1 binary addresses:
//   ctor                     v1.6.1 MissControl::MissControl @0x0019ed44
//   dtor (D0 deleting)       v1.6.1 MissControl::~MissControl @0x0019f0c0
//   dtor (D1 complete)       v1.6.1 MissControl::~MissControl @0x0019f198
//   dtor (D2 base)           // TODO: re-verify v1.6.1 addr
//   vtable                   v1.6.1 @0x002cdb48 (slot 2 = Init, slot 4 = Reset; 17 slots, slot 16 = SetPlayer)
//   GetFree                  v1.6.1 MissControl::GetFree @0x0019dcd8
//   MakeCritical             v1.6.1 MissControl::MakeCritical @0x0019e810
//   MakeRare                 v1.6.1 MissControl::MakeRare @0x0019e994
//   MakeCombo                v1.6.1 MissControl::MakeCombo @0x0019e630
//   MakeDisappear            v1.6.1 MissControl::MakeDisappear @0x0019f338
//   Update                   v1.6.1 MissControl::Update @0x0019e15c
//   Draw                     v1.6.1 MissControl::Draw @0x0019f54c
//   PreUpdate                v1.6.1 MissControl::PreUpdate @0x0019dde4
//   GetType                  v1.6.1 MissControl::GetType @0x0019fe24
//   Skip                     v1.6.1 MissControl::Skip @0x0019de28
//   Init (vtable slot 2)     v1.6.1 MissControl::Init @0x0019e07c
//   Reset (vtable slot 4)    v1.6.1 MissControl::Reset @0x0019df5c
//
// Lifecycle:
//   1. GameInit's CreatePool constructs the 12-slot pool; the first ctor
//      (s_refCount==0) lazy-loads the shared textures (critical.tex,
//      ultra_rare_plus_50.tex, hud_cross.tex, and combo_%d.tex for 3..10),
//      then every ctor calls Init() and parks the slot with m_Active = 0.
//   2. Make* picks a pool slot via GetFree, calls virtual Init() (which
//      sets m_Active = 1, binary field_0x30 = HUDControl::m_Active, and
//      tail-calls Reset()), then overrides pos/texture/anim state.
//   3. Update fades m_FadeAlpha to 0 via linear dt*s_DtMod*m_AlphaScale,
//      then clears m_bActive so GetFree can re-use the slot.
//   4. HUD::Draw renders each busy slot via HUDControl3d::Draw.
//
// Struct layout verified by asm-inspector 2026-04-30. Field offsets are
// from the binary's Init/MakeCritical/Update/Draw writes.
//
// Analysed: 2026-04-30T04:20

#include "HUDControl3d.h"

class HUD;
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "math/_Vector3.h"

class MissControl : public HUDControl3d {
public:
    // NOTE: HUDControl3d base ends at +0x7b (sizeof = 0x7c). Subclass fields follow.
    // The binary's struct layout (size=0x94, audited 2026-04-30 + corrected 2026-05-18):
    //
    // +0x2c: float   m_Timer (rotation degrees; used as SinIdx(rot*182) in Draw)
    // +0x30: uint8   m_Active (HUDControl base) -- pool busy flag; GetFree reads this
    // +0x34: uint32  m_LayerFlags / "configured" flag (Init writes 1)
    // +0x5c: uint8   RGBA tint b,g,r,a (Init copies from DAT default colour)
    // +0x74: Mortar::SmartPtr<Texture> bound texture (HUDControl3d base)
    // +0x7c: uint8   m_AnimState  (0=idle, 3=active fade)
    // +0x7d: uint8   m_bFlashing (gates jitter + particle spawn)
    // +0x7e: uint16  m_FlashTimer (decremented in Draw)
    // +0x80: float   m_LifeTimer (init 1.81)
    // +0x84: uint8   m_bComboActive
    // +0x85: uint8   m_bUseComboSound flag
    // +0x88: int32   m_ComboCount
    // +0x8c: uint8   m_bPlaySound (Init writes 1)
    // +0x90: float   m_DragScale (1.0 critical, 0.5 rare)

    // +0x7c: animation state (0=idle, 3=active fade)
    uint8_t m_AnimState;

    // +0x7d: flashing/gates jitter + particle spawn
    uint8_t m_bFlashing;

    // +0x7e: flash timer (decremented each Draw; adds rand offset)
    uint16_t m_FlashTimer;

    // +0x80: life timer. Init = 1.81 (DAT_001518b8). DIFFERS: was 0.808.
    float m_LifeTimer;

    // +0x84: combo indicator active flag
    uint8_t m_bComboActive;

    // +0x85: "use combo sound" flag (gates combo SFXPlay in Update)
    uint8_t m_bUseComboSound;

    // +0x88: combo count (determines which combo_N.tex to use)
    int m_ComboCount;

    // +0x8c: play-sound flag (binary Init writes 1 to this+0x8c)
    uint8_t m_bPlaySound;

    // +0x90: drag scale multiplier (1.0 critical, 0.5 rare)
    float m_DragScale;

    MissControl();
    ~MissControl() override;

    // vtable slot 2 -- v1.6.1 MissControl::Init @0x0019e07c. Marks the slot
    // busy (m_Active=1), restores every Make*-shared default (texture =
    // s_TexCross, timers, flags, size), then tail-calls Reset() (virtual
    // slot-4 dispatch @0x0019e134). Called by the ctor and by every Make*.
    void Init() override;

    // vtable slot 4 -- v1.6.1 MissControl::Reset @0x0019df5c. Restores tint /
    // flash state; frees the slot (m_Active=0, alpha=0) when m_LifeTimer > 0.
    void Reset() override;

    // vtable[14] @ 0x00152660 -- returns 2 (class-type tag)
    int GetType() override { return 2; }

    // vtable[15] -- v1.6.1 MissControl::Skip @0x0019de28 -- fast-forward spawn animation
    void Skip() override;

    // Called by HUD::Update at the start of every frame tick.
    // Maintains s_NumCriticals and s_DtMod for combo separation scaling.
    // binary @ 0x00150e04
    static void PreUpdate(float dt);

    // Shared texture load, idempotent (guarded by s_TexturesLoaded). The
    // binary loads these inline in the ctor gated on s_refCount==0; the port
    // ctor calls this under the same gate. Task #147 removed a GameInitialise
    // boot-time pre-warm call that ran with no MissControl instance alive yet
    // (s_refCount never left 0, so a splash-only exit before GameInit ran left
    // these 11 textures with no owner and no destructor to release them). The
    // Wii FN_BLOCK_PRELOAD path (BlockLoader.cpp) still calls this directly,
    // but only from RES_BLOCK_INGAME preload, well after GameInit's CreatePool
    // has already constructed the pool (s_refCount>0), so it's a cache hit,
    // never a pre-warm-before-any-instance.
    static void LoadContent();

    // Allocate the pool, construct each slot, register all with the HUD,
    // set m_bNoDestructor=1 per slot AFTER AddControl.
    // v1.6.1 MissControl::CreatePool @0x0019ef44
    // Flat contiguous block: operator new[](count*sizeof(MissControl)+8);
    // 8-byte [slotSize][count] header, then count placement-newed objects.
    // Pool globals: s_pPool @0x003164a8, s_PoolCount @0x003164ac, s_CurentFree @0x003164b0.
    static void CreatePool(int count, HUD* hud);

    // v1.6.1 MissControl::GetFree @0x0019dcd8
    // Round-robin through pool; cursor left at FOUND slot (not +1).
    static MissControl* GetFree();

    // v1.6.1 MissControl::MakeCritical @0x0019e810 -- activate critical-hit
    // label at a slice point. Calls virtual Init() first, then overrides.
    void MakeCritical(_Vector3<float> pos, int playerIdx);

    // v1.6.1 MissControl::MakeRare @0x0019e994 -- activate rare/special-fruit
    // label. Init() first; like MakeCritical but m_DragScale=0.5, no SetPlayer.
    void MakeRare(_Vector3<float> pos);

    // v1.6.1 MissControl::MakeDisappear @0x0019f338 -- zen-bomb X overlay and
    // miss-penalty indicator. Calls virtual Init() first.
    // Binary signature: MakeDisappear(_Vector3<float>, int, SmartPtr<Texture>).
    // Vec3 + SmartPtr are passed BY VALUE (no reference prefix in mangling).
    void MakeDisappear(_Vector3<float> pos, int sizeMult,
                       Mortar::SmartPtr<Mortar::Texture> tex);

    // v1.6.1 MissControl::MakeCombo @0x0019e630 -- activate combo indicator
    // (combo_N.tex for N=clamp(combo,2,10)). Calls virtual Init() first.
    void MakeCombo(_Vector3<float> pos, int comboCount, int entityType);

    // vtable[12] -- v1.6.1 MissControl::Update @0x0019e15c -- fade state machine
    void Update(float dt) override;

    // v1.6.1 MissControl::Draw @0x0019f54c -- render textured quad with UV
    // crop + rotation. The m_LifeTimer<=0 (passive X-marker) path draws only
    // when FailureEnabled() && !IsMultiplayer() && fabs(m_PauseAmount) < 1.0
    // (Zen / MP / settled-pause show no X-marks); see @0x0019f6a4.
    void Draw(float* hudScaleRaw) override;

    // v1.6.1 MissControl::Release @0x0019f0b8 -- vtable[5]. Tail-calls m_Texture(+0x74).SetPtr(NULL); no base chain.
    void Release() override;

    // Binary @ 0x00150e00 — vtable[8]. No-op shadow of HUDControl::PreDraw base.
    void PreDraw(float* hudScale) override;

    // v1.6.1 MissControl::SetPlayer @0x0019dd6c — vtable[16]. New virtual not in HUDControl
    // base; extends the vtable to 17 slots. Body is empty in the binary, but the symbol is
    // LIVE (called from MakeCritical/MakeCombo/MakeDisappear) — NOT a defunct stub. Callers
    // must keep invoking it. Returns its argument; the binary returns void (no caller reads it).
    virtual int SetPlayer(int player);

    // Port specific: F1 overlay support. MissControl::Draw renders at
    // pos + (480 * m_HudScale.x, 320 * m_HudScale.y, 0) (see binary @
    // 0x0015215c..0x00152186). For pool slots m_HudScale defaults to
    // (0,0,0) so this is a no-op; for the 3 GameInit passive widgets it
    // shifts the AABB to the top-right cluster where the X markers render.
    _Vector3<float> GetDrawPos() const override;

    // v1.6.1 MissControl::MakeEmAllDissappear @0x0019dd74
    // Contiguous walk: clamp busy slots' m_FadeAlpha to 0.06917 ceiling.
    static void MakeEmAllDissappear();

    // v1.6.1 MissControl::CleanPool @0x0019de80
    // Backward dtor loop + single operator delete[] on header-prefixed block.
    // s_PoolCount reset is unconditional even when pool was null.
    static void CleanPool();

    // Accessor for the file-static `s_TexCross` (hud_cross.tex). Used by
    // GameInit step 3 to seed the 3 passive miss-counter widgets'
    // m_Texture so MissControl::Draw doesn't early-return.
    static const Mortar::SmartPtr<Mortar::Texture>& GetCrossTexture();

    // --- Statics (file-scope in binary, exposed here for PreUpdate) ---
    static int   s_NumCriticals;  // 0x0023123c -- incremented per busy slot in Update
    static float s_DtMod;         // 0x001f3d6c -- (float)s_NumCriticals + 0.5, set by PreUpdate

    // Combo overlay textures: [0..1]=NULL, [2..9]=combo_3..combo_10 (loaded in
    // the ctor tex-block, v1.6.1 MissControl::MissControl @0x0019ed44).
    // MakeCombo index = clamp(comboCount-1, 0, 9) — see MissControl.cpp.
    static Mortar::SmartPtr<Mortar::Texture> s_ComboTextures[10];

    // Instance refcount for the shared static textures (s_TexCritical/s_TexRare/
    // s_TexCross/s_ComboTextures). Incremented per ctor, decremented per dtor;
    // hitting 0 releases the shared textures so the next CreatePool cycle's
    // ctor (gated on s_refCount==0) reloads them fresh.
    // v1.6.1 MissControl::MissControl @0x0019ed44 / ~MissControl @0x0019f198
    static int s_refCount;
};

#ifdef __bada__
static_assert(__builtin_offsetof(MissControl, m_AnimState)    == 0x7C, "MissControl m_AnimState offset");
static_assert(__builtin_offsetof(MissControl, m_bFlashing)    == 0x7D, "MissControl m_bFlashing offset");
static_assert(__builtin_offsetof(MissControl, m_FlashTimer)   == 0x7E, "MissControl m_FlashTimer offset");
static_assert(__builtin_offsetof(MissControl, m_LifeTimer)    == 0x80, "MissControl m_LifeTimer offset");
static_assert(__builtin_offsetof(MissControl, m_bComboActive) == 0x84, "MissControl m_bComboActive offset");
static_assert(__builtin_offsetof(MissControl, m_bUseComboSound) == 0x85, "MissControl m_bUseComboSound offset");
static_assert(__builtin_offsetof(MissControl, m_bPlaySound)  == 0x8C, "MissControl m_bPlaySound offset");
static_assert(__builtin_offsetof(MissControl, m_DragScale)    == 0x90, "MissControl m_DragScale offset");
static_assert(sizeof(MissControl) == 0x94, "MissControl sizeof mismatch");
#endif

// v1.6.1 GetCurrentMissCount @0x11a10c: returns global miss count (ignores player arg).
// Binary: ldrb r0,[<game_work.missCount>]; bx lr.
unsigned char GetCurrentMissCount(int player);

#endif
