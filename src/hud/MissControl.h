#ifndef FN_MISS_CONTROL_H
#define FN_MISS_CONTROL_H

//
// MissControl : HUDControl3d (size = 0x94)
// Overlay label that displays "critical" / "rare" / "X" text at a slice
// point. Binary keeps a 12-slot pool shared across all MissControl
// triggers. Same pool services Fruit critical/rare slices, Bomb zen-hit
// "X", and combo indicators (combo_3.tex .. combo_10.tex).
//
// Binary addresses:
//   ctor                     0x001511a0
//   dtor                     0x001513d8 / 0x00151468 / 0x001514f0
//   vtable                   0x001e9b28  (17 slots, slot 16 = SetPlayer)
//   GetFree                  0x00150da4
//   MakeCritical             0x00151764
//   MakeRare                 0x001518d8
//   MakeDisappear            0x00151d94
//   Update                   0x00151a60
//   Draw                     0x00151f60
//   PreUpdate                0x00150e04
//   GetType                  0x00152660
//   Skip                     0x00150e3c
//   Init (vtable[4])         0x00150fa4
//   Reset (vtable[6])        0x00150f14
//
// Lifecycle:
//   1. GameInitialise constructs the 12-slot pool, which on first ctor
//      lazy-loads the 4 shared textures (critical.tex,
//      ultra_rare_plus_50.tex, hud_cross.tex, and combo_%d.tex for 3..10).
//   2. Make* picks a pool slot via GetFree, populates pos/texture/anim
//      state, sets m_bActive = 1 (binary field_0x30 = HUDControl::m_bActive).
//   3. Update fades m_FadeAlpha to 0 via linear dt*s_DtMod*m_AlphaScale,
//      then clears m_bActive so GetFree can re-use the slot.
//   4. HUD::Draw renders each busy slot via HUDControl3d::Draw.
//
// Struct layout verified by asm-inspector 2026-04-30. Field offsets are
// from the binary's Init/MakeCritical/Update/Draw writes.
//
// Analysed: 2026-04-30T04:20

#include "HUDControl3d.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "math/Vec3.h"

class MissControl : public HUDControl3d {
public:
    // NOTE: HUDControl3d base ends at +0x7b (sizeof = 0x7c). Subclass fields follow.
    // The binary's struct layout (size=0x94, audited 2026-04-30 + corrected 2026-05-18):
    //
    // +0x2c: float   m_Timer (rotation degrees; used as SinIdx(rot*182) in Draw)
    // +0x30: uint8   m_bActive (HUDControl base) -- pool busy flag; GetFree reads this
    // +0x34: uint32  m_LayerFlags / "configured" flag (Init writes 1)
    // +0x5c: uint8   RGBA tint b,g,r,a (Init copies from DAT default colour)
    // +0x74: Mortar::SmartPtr<Texture> bound texture (HUDControl3d base)
    // +0x7c: uint8   m_AnimState  (0=idle, 3=active fade)
    // +0x7d: uint8   m_bVisible (visibility-on-screen, gates jitter)
    // +0x7e: uint16  jitter shake counter (decremented in Draw)
    // +0x80: float   m_FadeAlpha (init 1.81)
    // +0x84: uint8   m_bComboActive
    // +0x85: uint8   m_bUseSound flag
    // +0x88: int32   m_ComboCount
    // +0x8c: uint8   unknown flag (Init writes 1)  [not modelled in port]
    // +0x90: float   m_AlphaScale (1.0 critical, 0.5 rare)

    // +0x7c: animation state (0=idle, 3=active fade)
    uint8_t m_AnimState;

    // +0x7d: visible on screen (set when anim starts, gates jitter)
    uint8_t m_bVisible;

    // +0x7e: jitter shake counter (decremented each Draw; adds rand offset)
    uint16_t m_JitterTimer;

    // +0x80: fade alpha. Init = 1.81 (DAT_001518b8). DIFFERS: was 0.808.
    float m_FadeAlpha;

    // +0x84: combo indicator active flag
    uint8_t m_bComboActive;

    // +0x85: "use sound" flag (gates SFXPlay in Update)
    uint8_t m_bUseSound;

    // +0x88: combo count (determines which combo_N.tex to use)
    int m_ComboCount;

    // +0x8c: unknown flag written by Init (binary Init writes 1 to this+0x8c)
    uint8_t field_0x8c;

    // +0x90: alpha scale multiplier (1.0 critical, 0.5 rare)
    float m_AlphaScale;

    MissControl();
    ~MissControl() override;

    // vtable[4] @ 0x00150fa4 -- Init override
    void Init() override;

    // vtable[6] @ 0x00150f14 -- Reset override
    void Reset() override;

    // vtable[14] @ 0x00152660 -- returns 2 (class-type tag)
    int GetType() override { return 2; }

    // vtable[15] @ 0x00150e3c -- fast-forward spawn animation
    void Skip() override;

    // Called by HUD::Update at the start of every frame tick.
    // Maintains s_NumCriticals and s_DtMod for combo separation scaling.
    // binary @ 0x00150e04
    static void PreUpdate(float dt);

    // One-time shared texture load. Must be called once at startup.
    static void LoadContent();

    // Allocate the static 12-slot pool (binary: CreatePool(0xC, hud)).
    // binary @ 0x001512d8
    static void AllocatePool();

    // 0x00150da4 -- round-robin through pool returning first non-busy slot.
    // binary leaves cursor at the FOUND slot (not +1). Port DIFFERS was advancing past.
    static MissControl* GetFree();

    // 0x00151764 -- activate critical-hit label at a slice point.
    void MakeCritical(Vec3 pos, int playerIdx);

    // 0x001518d8 -- activate rare/special-fruit label.
    void MakeRare(Vec3 pos);

    // 0x00151d94 -- zen-bomb X overlay and miss-penalty indicator.
    void MakeDisappear(const Vec3& pos, int sizeMult,
                       const Mortar::SmartPtr<Mortar::Texture>& tex);

    // 0x001515a4 -- activate combo indicator (combo_N.tex for N=clamp(combo,2,10)).
    void MakeCombo(Vec3 pos, int comboCount, int entityType);

    // vtable[12] @ 0x00151a60 -- fade state machine
    void Update(float dt) override;

    // vtable[9] @ 0x00151f60 -- render textured quad with UV crop + rotation
    void Draw(const Vec3& hudScale, int layerMask) override;

    // Binary @ 0x001513cc — vtable[5]. Drops m_Texture ref (single-line helper).
    void Release() override;

    // Binary @ 0x00150e00 — vtable[8]. No-op shadow of HUDControl::PreDraw base.
    void PreDraw(const Vec3& hudScale) override;

    // Binary @ 0x00150dfc — vtable[16]. New virtual not in HUDControl base; extends vtable to 17 slots.
    // Defunct: same-screen MP player-index hook — no-op stub; binary @ 0x00150dfc
    virtual int SetPlayer(int player);

    // Port specific: F1 overlay support. MissControl::Draw renders at
    // pos + (480 * m_HudScale.x, 320 * m_HudScale.y, 0) (see binary @
    // 0x0015215c..0x00152186). For pool slots m_HudScale defaults to
    // (0,0,0) so this is a no-op; for the 3 GameInit passive widgets it
    // shifts the AABB to the top-right cluster where the X markers render.
    Vec3 GetDrawPos() const override;

    // Binary @ 0x00150e74 — delete every pool slot, null the pool ptr. Called from GameExit.
    static void CleanPool();

    // Accessor for the file-static `s_TexCross` (hud_cross.tex). Used by
    // GameInit step 3 to seed the 3 passive miss-counter widgets'
    // m_Texture so MissControl::Draw doesn't early-return.
    static const Mortar::SmartPtr<Mortar::Texture>& GetCrossTexture();

    // --- Statics (file-scope in binary, exposed here for PreUpdate) ---
    static int   s_NumCriticals;  // 0x0023123c -- incremented per busy slot in Update
    static float s_DtMod;         // 0x001f3d6c -- (float)s_NumCriticals + 0.5, set by PreUpdate

    // Binary @ 0x001515a4 — combo overlay textures: slots [0..9] = combo_2..combo_11.
    // Binary ctor loop iVar3=1..10: loads combo_%d.tex for iVar3>=3 -> names combo_3..combo_11.
    static Mortar::SmartPtr<Mortar::Texture> s_ComboTextures[10];
};

#ifdef __bada__
static_assert(__builtin_offsetof(MissControl, m_AnimState)    == 0x7C, "MissControl m_AnimState offset");
static_assert(__builtin_offsetof(MissControl, m_bVisible)     == 0x7D, "MissControl m_bVisible offset");
static_assert(__builtin_offsetof(MissControl, m_JitterTimer)  == 0x7E, "MissControl m_JitterTimer offset");
static_assert(__builtin_offsetof(MissControl, m_FadeAlpha)    == 0x80, "MissControl m_FadeAlpha offset");
static_assert(__builtin_offsetof(MissControl, m_bComboActive) == 0x84, "MissControl m_bComboActive offset");
static_assert(__builtin_offsetof(MissControl, m_AlphaScale)   == 0x90, "MissControl m_AlphaScale offset");
static_assert(sizeof(MissControl) == 0x94, "MissControl sizeof mismatch");
#endif

#endif
