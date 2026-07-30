// Analysed: 2026-05-03T00:00
#include "MissControl.h"
#include "particle/PSPParticleManager.h"
#include "HUD.h"
#include "HUDLayer.h"
#include "asset/TextureManager.h"
#include "Game.h"
#include "game/GameMode.h"
#include "engine/audio/GameSound.h"
#include "game/ItemManager.h"
#include "game/WaveManager.h"
#include "game/FruitCamera.h"
#include "network/P2PMessageHandling.h"
#include "math/Matrix44.h"
#include "math/MathUtil.h"
#include "math/Random.h"
#include "render/MatrixManager.h"
#include "render/Renderer.h"
#include "asset/Mesh.h"
#include "render/gl_funcs.h"
#include "debug/Logger.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include "game/GameWork.h"
#include "render/Layout.h"

// Pool size: binary CreatePool(0xC, hud) = 12 slots. DIFFERS: was 9.
static constexpr int MISS_POOL_SIZE = 12;

// Flat contiguous object pool matching the binary's operator new[](count*sizeof+8) block.
// Layout: [int slotSize][int count] header, then count MissControl objects placement-newed
// end-to-end. s_pPool points at header+8 (first object). s_pPool[-1 word] = count.
// Globals mirror binary BSS: pool @0x003164a8, poolCount @0x003164ac, curentFree @0x003164b0.
// Binary's typo "curent" (single 'r') is preserved in s_CurentFree.
static MissControl* s_pPool      = 0;  // points to first object (base+8)
static int          s_PoolCount  = 0;  // mirrors binary poolCount @0x003164ac
static int          s_CurentFree = 0;  // round-robin cursor; binary's typo "curent"

static Mortar::SmartPtr<Mortar::Texture> s_TexCritical;
static Mortar::SmartPtr<Mortar::Texture> s_TexRare;
static Mortar::SmartPtr<Mortar::Texture> s_TexCross;
static bool s_TexturesLoaded = false;

// MakeCritical / MakeRare fade init. DAT_001518b8 = 1.81f.
// binary @ 0x00151764 (MakeCritical), 0x001518d8 (MakeRare)
static constexpr float MISS_FADE_INIT = 1.81f;

// Screen clamp rectangle (centred ortho). binary @ 0x001518c0..0x001518cc
// X bounds are Layout::HalfWidth()-derived at each call site (opt-in
// widescreen); only the Y bounds stay fixed constants here.
static constexpr float CLAMP_Y_HI =  160.0f;
static constexpr float CLAMP_Y_LO = -160.0f;

// DIFFERS: opt-in widescreen -- screen-edge X clamp half-width used by
// MakeCritical/MakeRare/MakeCombo/MakeDisappear. Faithful 240.0f under
// __bada__ (mirrors Layout::HalfWidth()'s own identity there); real build
// reads the live (possibly widened) Layout::HalfWidth().
static inline float MissClampHalfX() {
#ifdef __bada__
    return 240.0f;
#else
    return Layout::HalfWidth();
#endif
}

// Combo separation base radius (scaled by zoom^2 in Update). Binary DAT = 70.0f.
static constexpr float SEP_BASE = 70.0f;

// Sound threshold crossing. v1.6.1 MissControl::Draw @0x0019f54c literal pool @0x0019f908 = 1.66f
static constexpr float SOUND_THRESH = 1.66f;

// Screen-clamp half-extents (centred ortho). DAT_00151f50, DAT_00151f54
// X bound is Layout::HalfWidth()-derived at the MakeDisappear call site.
static constexpr float MISS_CLAMP_HALF_Y = 160.0f;

// MakeDisappear path-2 quad size scalar. DAT_00151f4c = 0x42780000 = 62.0f.
// Multiplied into the global Vec3::One (GOT+0x77CC) -> size (62,62,62).
static constexpr float MISS_DISAPPEAR_SIZE = 62.0f;

// Pulse banding thresholds, from the literal pool of v1.6.1 MissControl::Draw @0x0019f54c.
// ASM-verified: 2026-07-28T00:00Z v1.6.1 MissControl::Draw @ 0x0019f54c (re-analyst) — byte-exact IEEE-754.
static constexpr float MISS_PULSE_FLOOR       = 0.65f;     // pool @0x0019f924
static constexpr float MISS_PULSE_PHASE_LO    = 16380.0f;  // pool @0x0019f914
static constexpr float MISS_PULSE_PHASE_HI    = 376740.0f; // pool @0x0019f918
// TODO: v1.6.1 0x0019f54c (MissControl::Draw) — the two NARROW constants below still carry
// their stale v1.5.x DAT addresses; locate them in the v1.6.1 Draw literal pool and restamp.
static constexpr float MISS_PULSE_NARROW_LO   = 32760.0f;  // DAT_001522ac = 0x46fff000
static constexpr float MISS_PULSE_NARROW_HI   = 360360.0f; // DAT_001522b0 = 0x48aff500

// --- Static members -------------------------------------------------------

int   MissControl::s_NumCriticals = 0;
float MissControl::s_DtMod        = 0.5f;  // (float)0 + 0.5 initial

// Combo overlay textures: [0..1]=NULL, [2..9]=combo_3..combo_10
// (loaded in ctor tex-block, v1.6.1 MissControl::MissControl @0x0019ed44).
Mortar::SmartPtr<Mortar::Texture> MissControl::s_ComboTextures[10];

int MissControl::s_refCount = 0;

// --- ctor / dtor -----------------------------------------------------------

// ASM-spec v1.6.1 MissControl::MissControl @0x0019ed44: if(s_refCount==0) load 3 tex + combo_3..10 inline; ++s_refCount; Init(); m_Active=0.
MissControl::MissControl()
    : m_AnimState(0)
    , m_bFlashing(0)
    , m_FlashTimer(0)
    , m_LifeTimer(0.0f)
    , m_bComboActive(0)
    , m_bUseComboSound(0)
    , m_ComboCount(0)
    , m_DragScale(1.0f)
{
    // v1.6.1 MissControl::MissControl @0x0019ed44: mov r3,#1; strb r3,[r5,#0x4]
    // (HUDControl::m_Singular, +0x4). Without this every MissControl is swept
    // by HUDControl::SetToMultiplayerState() on Game::TellGameToStart.
    m_Singular      = 1;
    // Binary lazy-loads the shared textures inline here, gated on s_refCount==0.
    // The port keeps the load body in LoadContent() (guarded by s_TexturesLoaded).
    // Task #147 removed a GameInitialise boot pre-warm call that ran with no
    // MissControl instance alive yet, leaking the textures on a splash-only exit.
    if (s_refCount == 0) {
        LoadContent();
    }
    ++s_refCount;
    Init();
    m_Active        = 0;   // pool slot starts free; Make* re-runs Init() and sets it to 1
    // m_bNoDestructor is NOT set here -- binary writes it in CreatePool AFTER
    // HUD::AddControl. Setting it in the ctor would diverge from binary's
    // initialisation order (cosmetic in practice since both routes end with
    // m_bNoDestructor == 1 before the first Update tick).
}

// ASM-spec v1.6.1 MissControl::~MissControl @0x0019f198: read directly from Ghidra
// disassembly (not yet asm-inspector-diffed). Binary order: MissControl::Release(this)
// @0x0019f1b8 (nulls m_Texture) -> --s_refCount
// -> explicit SmartPtr<Texture>::SetPtr(&m_Texture, NULL) @0x0019f1d4 (redundant with
// Release's own null-out, but binary-faithful; SmartPtr::SetPtr(NULL) is idempotent, no
// double-release) -> if (s_refCount<1) release the 4 shared statics.
MissControl::~MissControl() {
    Release();
    --s_refCount;
    m_Texture.SetNull();
    if (s_refCount <= 0) {
        s_TexCritical.SetNull();
        s_TexRare.SetNull();
        s_TexCross.SetNull();
        for (int i = 0; i < 10; ++i) s_ComboTextures[i].SetNull();
        s_TexturesLoaded = false;  // allow next LoadContent() call to reload
    }
}

// --- vtable overrides -------------------------------------------------------

// v1.6.1 MissControl::Release @0x0019f0b8 -- vtable[5]. Tail-calls m_Texture(+0x74).SetPtr(NULL); no base chain.
void MissControl::Release() {
    m_Texture.SetNull();
}

// ASM-spec v1.6.1 MissControl::Init @0x0019e07c (vtable slot 2, vtable @0x002cdb48):
// m_Active=1, m_Timer=0, m_bPlaySound=1, m_LayerFlags=1, m_bComboActive=0,
// m_AnimState=0, m_Texture=s_TexCross, m_bPendingRemoval=0, m_LifeTimer=0,
// m_bUseComboSound=0, m_ComboCount=0, m_DragScale=1, size=((w>>1)+1,(w>>1)+1,0);
// tail = virtual slot-4 dispatch (Reset).
// Live call sites: ctor @0x0019ed44 and every Make* (each starts with Init()).
void MissControl::Init() {
    m_bComboActive = 0;
    m_Active       = 1;   // binary field_0x30 = 1; marks slot as busy/active
    m_Timer        = 0.0f;  // rotation (+0x2c)
    m_bPlaySound     = 1;   // +0x8c = 1 (sound-enable gate)
    m_LayerFlags   = Mortar::HUD_LAYER_DEFAULT;  // "configured" flag (+0x34 = 1)
    m_AnimState    = 0;
    // Init defaults m_Texture to s_TexCross (hud_cross.tex), NOT s_TexCritical.
    // This is the red X used by path 2 of MakeDisappear (fruit-miss penalty).
    // MakeCritical/MakeRare/MakeCombo override with their respective textures.
    m_Texture      = s_TexCross;
    m_LifeTimer    = 0.0f;
    m_Active       = 1;   // binary writes field_0x30 twice (second write is redundant but faithful)
    m_ComboCount   = 0;
    m_bPendingRemoval = 0;  // +0x33 = 0
    m_bUseComboSound    = 0;
    m_DragScale   = 1.0f;
    // Binary reads s_TexCritical->GetWidth() TWICE (not GetHeight) for the
    // default size -- binary-faithful; Make* overwrite size anyway.
    if (s_TexCritical.IsValid()) {
        size = _Vector3<float>((float)(s_TexCritical->GetWidth() >> 1) + 1,
                               (float)(s_TexCritical->GetWidth() >> 1) + 1,
                               0.0f);
    } else {
        size = _Vector3<float>(0.0f, 0.0f, 0.0f);
    }
    // ASM-verified v1.6.1 MissControl::Init @0x0019e134: tail-calls vtable slot4 = MissControl::Reset @0x0019df5c (not base Init).
    Reset();
}

// ASM-spec v1.6.1 MissControl::Reset @0x0019df5c (vtable slot 4, vtable @0x002cdb48).
// Also reached as Init()'s tail (virtual slot-4 dispatch @0x0019e134).
void MissControl::Reset() {
    m_DrawColour   = Colour(255, 255, 255, 255);  // restore RGBA tint from DAT_00150f7c
    m_DrawColour.a = 0xff;
    m_FlashTimer  = 0;
    m_bFlashing     = 0;
    if (m_LifeTimer > 0.0f) {
        m_Active       = 0;   // binary field_0x30 = 0; frees slot
        m_DrawColour.a = 0;
    }
}

// Binary @ 0x00150e00 -- vtable[8]. No-op shadow of HUDControl::PreDraw base.
// Binary's MissControl::PreDraw is a no-op (single bx lr in the original).
// m_HudScale (+0x14) is initialised once in GameInit and not refreshed
// per-frame; the Draw call uses the stored value directly.
void MissControl::PreDraw(float* /*hudScale*/) {}

// v1.6.1 MissControl::SetPlayer @0x0019dd6c -- vtable[16].
// The BODY is empty in the binary (single `bx lr`), but the symbol is LIVE, not defunct:
// its PLT thunk @0x00105cf0 is called from MakeCritical @0x0019e96c, MakeCombo @0x0019e7e0
// and MakeDisappear @0x0019f40c + @0x0019f478. Keep every call site. m_PlayerIdx is the
// P2P/EntityTracker partition, not a same-screen split (#158), so the earlier
// "Defunct: same-screen MP player-index hook" label was wrong on both counts.
// DIFFERS: original returns void, port returns the argument -- no caller reads it; the
// Itanium mangled name (_ZN11MissControl9SetPlayerEi) and the vtable slot are unaffected.
int MissControl::SetPlayer(int player) {
    return player;
}

// Port specific: mirrors the quad-origin formula from MissControl::Draw
// (v1.6.1 MissControl::Draw @0x0019f54c) so the F1 boundary tracks the rendered quad.
// Jitter term is omitted (non-deterministic RandUint; short-lived and cosmetic).
_Vector3<float> MissControl::GetDrawPos() const
{
    _Vector3<float> p = pos;
    if (m_LifeTimer <= 0.0f) {
        // Mirrors Draw's lifeTimer<=0 gates (v1.6.1 MissControl::Draw @0x0019f6a4):
        // Draw early-returns (no quad) unless FailureEnabled() && !IsMultiplayer()
        // && fabs(m_PauseAmount) < 1.0, so the offset only applies under the
        // same condition. No else-branch (binary has none).
        const float pause = fabsf(game_work.m_PauseAmount);
        if (Mortar::FailureEnabled(game_work.gameMode) && !IsMultiplayer() &&
            pause < 1.0f) {
            p.y -= 3.0f * pos.y * pause;
        }
    }
    return _Vector3<float>(p.x + 480.0f * m_HudScale.x,
                           p.y + 320.0f * m_HudScale.y,
                           p.z);
}

// ASM-verified: 2026-05-24 v1.6.1 MissControl::Skip @ 0x0019de28 (re-analyst)
// vtable[15]
void MissControl::Skip() {
    // Binary reads GameWork.missCount (+0x14) as the cap, not hardcoded 1.
    // v1.6.1 MissControl::Skip @0x0019de28: ldrb r3,[r2,#0x14]; cmp r3,r4 (r4 = m_AnimState)
    uint8_t cap = game_work.missCount;
    if (m_AnimState < cap) {
        m_FlashTimer  = 0;
        m_DrawColour.a = 0xff;
        m_bFlashing     = 1;
    }
}

// --- Static: PreUpdate -----------------------------------------------------

// binary @ 0x00150e04: saves old s_NumCriticals, resets to 0, writes s_DtMod = (float)old + 0.5
void MissControl::PreUpdate(float /*dt*/) {
    int n = s_NumCriticals;
    s_NumCriticals = 0;
    s_DtMod = (float)n + 0.5f;
}

// --- Shared texture load ---------------------------------------------------

void MissControl::LoadContent() {
    if (s_TexturesLoaded) return;
    s_TexCritical = Mortar::TextureManager::LoadLocalisedTexture("critical.tex");
    s_TexRare     = Mortar::TextureManager::LoadLocalisedTexture("ultra_rare_plus_50.tex");
    s_TexCross    = Mortar::TextureManager::LoadLocalisedTexture("hud_cross.tex");
    // ASM-spec v1.6.1 MissControl::MissControl @0x0019ed44 (tex-load block):
    // loop iVar3=1..10; loads combo_%d.tex only for iVar3>=3.
    // Slots [0] and [1] are intentionally NULL (combo=1,2 have no texture).
    // Names: combo_3.tex .. combo_10.tex -> array indices [2..9].
    for (int i = 0; i < 10; ++i) {
        if (i < 2) {
            s_ComboTextures[i].SetNull();
        } else {
            char name[32];
            snprintf(name, sizeof(name), "combo_%d.tex", i + 1);
            s_ComboTextures[i] = Mortar::TextureManager::LoadLocalisedTexture(name);
        }
    }
    s_TexturesLoaded = true;
    LOG_DEBUG("MissControl", "LoadContent: critical=%d rare=%d cross=%d",
              s_TexCritical.IsValid(), s_TexRare.IsValid(), s_TexCross.IsValid());
}

// --- Pool allocation -------------------------------------------------------

// v1.6.1 MissControl::CreatePool @0x0019ef44
// One contiguous operator new[](count*sizeof(MissControl)+8) block:
//   header[0] = (int)sizeof(MissControl); header[1] = count;
//   s_pPool = (MissControl*)(block+8);
// Placement-new each slot, then register with HUD + set m_bNoDestructor.
// x64 caveat: use sizeof(MissControl) for stride/header, NOT the binary literal 0x94
// (vtable ptr + SmartPtr widen the struct on x64).
void MissControl::CreatePool(int count, HUD* hud) {
    // Step 1: tear down existing pool (mirrors binary @ 0x0019ef44 CleanPool path)
    CleanPool();

    // Step 2: allocate flat block with 8-byte [slotSize][count] header.
    char* blk = reinterpret_cast<char*>(::operator new[](
        static_cast<size_t>(count) * sizeof(MissControl) + 8));
    reinterpret_cast<int*>(blk)[0] = static_cast<int>(sizeof(MissControl));
    reinterpret_cast<int*>(blk)[1] = count;
    s_pPool = reinterpret_cast<MissControl*>(blk + 8);

    // Step 3: placement-new each slot end-to-end.
    for (int i = 0; i < count; ++i) {
        new (&s_pPool[i]) MissControl();
    }
    s_PoolCount  = count;
    s_CurentFree = 0;

    // Step 4: register each slot with HUD, THEN set m_bNoDestructor (binary order).
    for (int i = 0; i < count; ++i) {
        hud->AddControl(&s_pPool[i], false);  // pushFront = false per binary
        s_pPool[i].m_bNoDestructor = 1;       // base+0x32; AFTER AddControl
    }

    LOG_DEBUG("MissControl", "CreatePool: %d slots registered to HUD %p",
              count, static_cast<void*>(hud));
}

const Mortar::SmartPtr<Mortar::Texture>& MissControl::GetCrossTexture() {
    return s_TexCross;
}

// --- MakeEmAllDissappear ---------------------------------------------------

// v1.6.1 MissControl::MakeEmAllDissappear @0x0019dd74
// Contiguous walk: for i<s_PoolCount, clamp busy slots' m_LifeTimer to 0.06917 ceiling.
// DAT_0019ddd4=0x3d8da741 exact IEEE-754 value.
void MissControl::MakeEmAllDissappear() {
    for (int i = 0; i < s_PoolCount; ++i) {
        if (s_pPool[i].m_Active != 0) {
            if (s_pPool[i].m_LifeTimer >= 0.06916667f)
                s_pPool[i].m_LifeTimer = 0.06916667f;
        }
    }
}

// --- CleanPool -------------------------------------------------------------

// v1.6.1 MissControl::CleanPool @0x0019de80
// Iterates BACKWARD (per binary), explicitly dtors each slot, then one
// operator delete[] on the whole header-prefixed block (base - 8).
// s_PoolCount reset is UNCONDITIONAL even when s_pPool was null (binary does this).
void MissControl::CleanPool() {
    if (s_pPool) {
        for (int i = s_PoolCount - 1; i >= 0; --i) {
            s_pPool[i].~MissControl();
        }
        ::operator delete[](reinterpret_cast<char*>(s_pPool) - 8);
        s_pPool = 0;
    }
    s_PoolCount = 0;  // unconditional per binary
}

// --- GetFree ---------------------------------------------------------------

// v1.6.1 MissControl::GetFree @0x0019dcd8
// Round-robin: cursor left at FOUND slot (not +1). On full-pool exhaustion,
// the binary's break-before-advance while-loop leaves the cursor frozen at
// the ORIGINAL entry idx (revisits it after poolCount steps), so sustained
// overflow always evicts the same slot -- NOT idx+1.
MissControl* MissControl::GetFree() {
    if (!s_pPool) return 0;
    int idx = s_CurentFree;
    int tries = 0;
    while (true) {
        if (!s_pPool[idx].m_Active) {
            s_CurentFree = idx;
            return &s_pPool[idx];
        }
        if (tries >= s_PoolCount) break;  // break BEFORE advancing idx (binary order)
        idx = (idx + 1) % s_PoolCount;
        ++tries;
    }
    s_CurentFree = idx;
    return &s_pPool[idx];
}

// --- Make* -----------------------------------------------------------------

// ASM-spec v1.6.1 MissControl::MakeCritical @0x0019e810: calls virtual Init() (vtbl slot2 @0x0019e07c) first, then overrides tex/flags/life/pos/size; MakeRare @0x0019e994 identical + m_DragScale=0.5, no SetPlayer.
void MissControl::MakeCritical(_Vector3<float> pos, int playerIdx) {
    LOG_INFO("MissControl", "MakeCritical pos=(%.1f,%.1f,%.1f) player=%d this=%p",
             pos.x, pos.y, pos.z, playerIdx, static_cast<void*>(this));
    Init();
    m_Texture      = s_TexCritical;
    m_bFlashing     = 1;
    m_bComboActive = 1;
    m_LifeTimer    = MISS_FADE_INIT;
    m_DrawColour.a = 0xff;
    m_AnimState    = 3;
    m_FlashTimer  = 0;
    this->pos      = pos;
    if (s_TexCritical.IsValid()) {
        uint32_t w = (uint32_t)s_TexCritical->GetWidth();
        uint32_t h = (uint32_t)s_TexCritical->GetHeight();
        // size = (w+1, h+1, 0), halved for clamp, then restored
        size.x = (float)(w + 1);
        size.y = (float)(h + 1);
        size.z = 0.0f;  // DAT_001518bc = 0.0
        size.x *= 0.5f;
        size.y *= 0.5f;
        // Screen clamps use HALF size (binary @ 0x001518c0..0x001518cc)
        // DIFFERS: opt-in widescreen -- clamp against the (possibly widened)
        // screen-edge half-width so critical popups stay on-screen.
        const float clampXHi =  MissClampHalfX();
        const float clampXLo = -MissClampHalfX();
        if (size.x + this->pos.x >  clampXHi) this->pos.x =  clampXHi - size.x;
        if (size.y + this->pos.y >  CLAMP_Y_HI) this->pos.y =  CLAMP_Y_HI - size.y;
        if (this->pos.x - size.x <  clampXLo) this->pos.x =  clampXLo + size.x;
        if (this->pos.y - size.y <  CLAMP_Y_LO) this->pos.y =  CLAMP_Y_LO + size.y;
        // Restore full size
        size.x += size.x;
        size.y += size.y;
    }
    SetPlayer(playerIdx);
}

// ASM-spec v1.6.1 MissControl::MakeRare @0x0019e994: calls virtual Init() (vtbl slot2 @0x0019e07c) first, then identical to MakeCritical but uses s_TexRare, sets m_DragScale=0.5 (before HalfScale), and does NOT call SetPlayer.
void MissControl::MakeRare(_Vector3<float> pos) {
    LOG_INFO("MissControl", "MakeRare pos=(%.1f,%.1f,%.1f) this=%p",
             pos.x, pos.y, pos.z, static_cast<void*>(this));
    Init();
    m_Texture      = s_TexRare;
    m_bFlashing     = 1;
    m_bComboActive = 1;
    m_LifeTimer    = MISS_FADE_INIT;
    m_DrawColour.a = 0xff;
    m_AnimState    = 3;
    m_FlashTimer  = 0;
    this->pos      = pos;
    if (s_TexRare.IsValid()) {
        uint32_t w = (uint32_t)s_TexRare->GetWidth();
        uint32_t h = (uint32_t)s_TexRare->GetHeight();
        size.x = (float)(w + 1);
        size.y = (float)(h + 1);
        size.z = 0.0f;
        m_DragScale = 0.5f;  // written between size init and HalfScale (v1.6.1 MakeRare @0x0019e994)
        size.x *= 0.5f;
        size.y *= 0.5f;
        // Screen clamps use HALF size (binary @ 0x00151a2c..0x00151a40)
        // DIFFERS: opt-in widescreen -- see MakeCritical's clamp note.
        const float clampXHi =  MissClampHalfX();
        const float clampXLo = -MissClampHalfX();
        if (size.x + this->pos.x >  clampXHi) this->pos.x =  clampXHi - size.x;
        if (size.y + this->pos.y >  CLAMP_Y_HI) this->pos.y =  CLAMP_Y_HI - size.y;
        if (this->pos.x - size.x <  clampXLo) this->pos.x =  clampXLo + size.x;
        if (this->pos.y - size.y <  CLAMP_Y_LO) this->pos.y =  CLAMP_Y_LO + size.y;
        // Restore full size
        size.x += size.x;
        size.y += size.y;
    }
    // MakeRare does NOT call SetPlayer (unlike MakeCritical)
}

// ASM-spec v1.6.1 MissControl::MakeCombo @0x0019e630: calls virtual Init() (vtbl slot2 @0x0019e07c) first, then overrides texture/combo flags/life/pos/size.
// Picks combo_N.tex where N = clamp(comboCount, 2, 11); maps to s_ComboTextures[idx].
// Sets m_bComboActive=1, m_bUseComboSound=1, m_ComboCount=combo, m_LifeTimer=1.81, anim=3, visible=1.
void MissControl::MakeCombo(_Vector3<float> pos, int comboCount, int entityType) {
    LOG_INFO("MissControl", "MakeCombo pos=(%.1f,%.1f,%.1f) count=%d entityType=%d this=%p",
             pos.x, pos.y, pos.z, comboCount, entityType, static_cast<void*>(this));
    Init();
    // Texture pick uses CALLER's comboCount (before arcade override).
    // v1.6.1 MakeCombo @0x0019e630: idx computed before arcade m_ComboCount override.
    int idx;
    if (comboCount < 2)       idx = 0;
    else if (comboCount < 10) idx = comboCount - 1;
    else                      idx = 9;
    if (s_ComboTextures[idx].IsValid()) {
        m_Texture = s_ComboTextures[idx];
    }
    // m_bUseComboSound = 1 (v1.6.1 MakeCombo @0x0019e630, written BEFORE arcade override)
    m_bUseComboSound    = 1;
    m_bComboActive = 1;
    m_ComboCount   = comboCount;  // caller's value stored first
    // Arcade-mode override: after storing caller's comboCount and picking texture,
    // binary overrides m_ComboCount with (int)(WaveManager::GetSpeed(0) + 0.65f).
    // v1.6.1 MakeCombo @0x0019e630 (AFTER m_ComboCount = comboCount, AFTER texture lookup)
    Game* g = Game::GetInstance();
    if (g && game_work.gameMode == GAME_MODE_ARCADE) {
        WaveManager* wm = WaveManager::GetInstance();
        if (wm) m_ComboCount = (int)(wm->GetSpeed(0) + 0.65f);
    }
    m_DrawColour.a = 0xff;
    m_LifeTimer    = MISS_FADE_INIT;  // DAT_00151740 = 1.81f
    m_AnimState    = 3;
    m_bFlashing     = 1;
    this->pos      = pos;
    m_FlashTimer  = 0;
    // Size: (w+1, h+1, 0), halved for clamp, then restored
    if (s_ComboTextures[idx].IsValid()) {
        uint32_t w = (uint32_t)s_ComboTextures[idx]->GetWidth();
        uint32_t h = (uint32_t)s_ComboTextures[idx]->GetHeight();
        size.x = (float)(w + 1);
        size.y = (float)(h + 1);
        size.z = 0.0f;  // DAT_00151744 = 0.0
        size.x *= 0.5f;
        size.y *= 0.5f;
        // Screen clamps use HALF size (binary @ 0x00151748..0x00151754)
        // DIFFERS: opt-in widescreen -- see MakeCritical's clamp note.
        const float clampXHi =  MissClampHalfX();
        const float clampXLo = -MissClampHalfX();
        if (size.x + this->pos.x >  clampXHi) this->pos.x =  clampXHi - size.x;
        if (size.y + this->pos.y >  CLAMP_Y_HI) this->pos.y =  CLAMP_Y_HI - size.y;
        if (this->pos.x - size.x <  clampXLo) this->pos.x =  clampXLo + size.x;
        if (this->pos.y - size.y <  CLAMP_Y_LO) this->pos.y =  CLAMP_Y_LO + size.y;
        // Restore full size
        size.x += size.x;
        size.y += size.y;
    }
    SetPlayer(entityType);
}

// ASM-spec v1.6.1 MissControl::MakeDisappear @0x0019f338: calls virtual Init() (vtbl slot2 @0x0019e07c) first; two-path form based on whether SmartPtr is valid.
// Common prefix after Init(): m_DrawColour.a=0xff, then pos.
void MissControl::MakeDisappear(_Vector3<float> inPos, int sizeMult,
                                Mortar::SmartPtr<Mortar::Texture> tex) {
    LOG_INFO("MissControl", "MakeDisappear pos=(%.1f,%.1f,%.1f) sizeMult=%d tex=%d this=%p",
             inPos.x, inPos.y, inPos.z, sizeMult,
             tex.IsValid() ? 1 : 0, static_cast<void*>(this));
    Init();   // sets m_Texture = s_TexCross; path 2 relies on this (no texture arg supplied)
    m_DrawColour.a = 0xff;  // field_0x5f = 0xff (common prefix)
    pos = inPos;
    if (tex.IsValid()) {
        // Path 1: zen-bomb X overlay (valid SmartPtr supplied).
        m_bPlaySound     = 0;   // +0x8c = 0 (sound-gate cleared)
        m_Texture      = tex;
        m_bFlashing     = 1;
        m_AnimState    = 3;
        m_LifeTimer    = MISS_FADE_INIT;  // DAT_00151f40 = 1.81f
        m_FlashTimer  = 0;
        m_bComboActive = 1;
        uint32_t w = (uint32_t)tex->GetWidth();
        uint32_t h = (uint32_t)tex->GetHeight();
        size = _Vector3<float>((float)(w + 1), (float)(h + 1), 0.0f);  // DAT_00151f44 = 0.0
        // ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00151e40 (re-analyst v2)
        // Binary: `mov r0,r4; mov r1,r7; blx 0x000f6c30` -- r7 was saved from
        // r2 = param_3 = sizeMult at function entry @ 0x00151db6.
        SetPlayer(sizeMult);
    } else {
        // Path 2: fruit miss-penalty / arcade-bomb cross overlay.
        // v1.6.1 MakeDisappear @0x0019f338 else branch -- does NOT touch m_Texture.
        // The red X comes from Init()'s default `m_Texture = s_TexCross`
        // (v1.6.1 MissControl::Init @0x0019e07c). Path 2 just updates pose /
        // size / alpha / anim-state; the texture binding from Init carries through.
        m_FlashTimer  = (sizeMult >= 1) ? 0x1e : 0;
        m_LifeTimer    = SOUND_THRESH;   // DAT_00151f48 = 1.66f
        m_AnimState    = 3;
        m_bFlashing     = 1;
        // size = (*pHudScale) * 62.0f, where pHudScale is the global Vec3 loaded
        // from GOT+0x77CC (DAT_00151f5c = 0x77cc GOT offset). That global is
        // _Vector3<float>::One @ BSS 0x001f4334, constructed to (1,1,1) by the
        // _GLOBAL__I_MissControl.cpp static-init (binary @ 0x00152378) and never
        // mutated, so the product is (62,62,62). Binary @ 0x00151e74 (s16 = 62.0f
        // from DAT_00151f4c = 0x42780000), 0x00151e7e (Vec3 * scalar -> size).
        // Binary @ 0x00151f5c (GOT offset), 0x00151e64 (load pHudScale).
        {
            const _Vector3<float>& pHudScale = _Vector3<float>::One();  // GOT+0x77CC == _Vector3<float>::One
            size = pHudScale * MISS_DISAPPEAR_SIZE;
        }
        // ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00151e94 (re-analyst v2)
        // Same pattern as path 1: r1 = r7 = sizeMult.
        SetPlayer(sizeMult);
        // Path 2 size is set again after SetPlayer (binary @ 0x00151e9a sets size
        // a second time from the same Vec3*scalar -- identical value).
        {
            const _Vector3<float>& pHudScale = _Vector3<float>::One();
            size = pHudScale * MISS_DISAPPEAR_SIZE;
        }
        // Pos clamp using HALF size (binary @ 0x00151f50..0x00151f54 clamps)
        // DIFFERS: opt-in widescreen -- see MakeCritical's clamp note.
        float halfX = size.x * 0.5f;
        const float clampHalfX = MissClampHalfX();
        if (pos.x >  clampHalfX - halfX) pos.x =  clampHalfX - halfX;
        if (pos.x < -clampHalfX + halfX) pos.x = -clampHalfX + halfX;
        float halfY = size.y * 0.5f;
        if (pos.y >  MISS_CLAMP_HALF_Y - halfY) pos.y =  MISS_CLAMP_HALF_Y - halfY;
        if (pos.y < -MISS_CLAMP_HALF_Y + halfY) pos.y = -MISS_CLAMP_HALF_Y + halfY;
        // Binary path 2 does NOT assign m_Active=1 (Init already set it).
        // Binary path 2 does NOT re-assign m_Texture (keeps Init's s_TexCross default).
    }
}

// --- Update ----------------------------------------------------------------

// ASM-verified: 2026-05-24 v1.6.1 MissControl::Update @ 0x0019e15c (re-analyst)
// v1.6.1 MissControl::Update @0x0019e15c
void MissControl::Update(float dt) {
    // Passive miss-counter path: 3 GameInit-spawned widgets at top of HUD.
    // Their m_AnimState is 0/1/2 (slot index); m_Active stays 0.
    // Toggle m_bFlashing based on game_work.missCount vs m_AnimState -- when the
    // player has missed at least (m_AnimState + 1) fruits, the X marker
    // turns red. v1.6.1 MissControl::Update @0x0019e15c lines 1-10.
    Game* game = Game::GetInstance();
    uint8_t missCount = (game ? game_work.missCount : 0);
    if (!m_bFlashing && m_AnimState < missCount) {
        m_FlashTimer  = 0x1e;
        m_DrawColour.a = 0xff;
        m_bFlashing     = 1;
    }

    // Combo separation force: if m_bComboActive, repel busy neighbours within 70px.
    // v1.6.1 MissControl::Update @0x0019e15c combo block (~50 instructions)
    if (m_bComboActive) {
        // s_NumCriticals++ happens AT THE TOP of the combo block (before iteration).
        // binary @ 0x00151ac6 -- incremented before the pool loop.
        ++s_NumCriticals;

        // ASM-spec v1.6.1 MissControl::Update @0x0019e15c:
        //   repulsion accumulator seeds from _Vector2<float>::Zero (BSS 0x002d92a0) = (0,0)
        //   (0x0019e1f0/f4: vldr s14,[r3]; vldr s15,[r3,#4], r3 = &Vector2::Zero).
        //   acc -= dir * (radius - dist) * dt * 15.0 per overlapping active neighbor;
        //   pos += acc  =>  a lone combo popup is STATIC for its 1.81s life.
        float accX = 0.0f;
        float accY = 0.0f;
        // ASM-spec v1.6.1 MissControl::Update @0x0019e1e4: separation radius
        // r = 70 * m_Zoom * m_Zoom (70*zoom^2, NOT (70*zoom)^2); gate is
        // distSq < r*r; force term uses (r - dist).
        const FruitCamera* cam = game_work.m_FruitCamera;
        const float zoom = cam ? cam->m_Zoom : 1.0f;
        const float sepRadius = SEP_BASE * zoom * zoom;
        const float sepRadiusSq = sepRadius * sepRadius;
        for (int k = 0; k < s_PoolCount; ++k) {
            MissControl* other = &s_pPool[k];
            if (other == this || !other->m_Active) continue;
            float dx = other->pos.x - pos.x;
            float dy = other->pos.y - pos.y;
            float distSq = dx*dx + dy*dy;
            if (distSq >= sepRadiusSq) continue;
            float dist = 1.0f;
            if (distSq <= 0.0f) {
                // random direction when coincident.
                // ASM-spec v1.6.1 MissControl::Update @0x0019e15c (outlined helper
                // T.892 @0x0019df0c): Math::g_random.Rand32(0xff3a) x1, only when distSq <= 0.
                uint16_t a = (uint16_t)Math::g_Random.Rand32(0xff3a);
                dx = SinIdx(a);
                dy = CosIdx(a);
            } else {
                dist = sqrtf(distSq);
            }
            dx /= dist;
            dy /= dist;
            // Binary chains three Vec2 *= scalar calls then a -= :
            //   a = dir * (r - dist);  a *= dt;  a *= 15.0;  accel -= a
            // (repel: self moves AWAY from other), r = 70*zoom^2 from above.
            float force = (sepRadius - dist) * dt * 15.0f;
            accX -= dx * force;
            accY -= dy * force;
        }
        pos.x += accX;
        pos.y += accY;
        // Scale dt by combo modifier
        dt = dt * s_DtMod * m_DragScale;
    }

    if (m_LifeTimer <= 0.0f) {
        // Passive deactivation: if missCount went DOWN below this slot
        // (e.g. between rounds when the counter resets), turn the X off.
        // binary @ 0x00151c08..0x00151d28
        if (!m_bFlashing) return;
        if (m_AnimState < missCount) return;
        m_FlashTimer  = 0x1e;
        m_DrawColour.a = 0xff;
        m_bFlashing     = 0;
        // Binary @ 0x0019e53c: spawn particle emitter on fade-expire (0x281ecb hash, count 9)
        {
            PSPParticleManager& ppm = PSPParticleManager::GetInstance();
            ppm.AddEmitter(0x281ecb, nullptr, false);
        }
        return;
    }

    // Pause guard: binary reads game_work.bM_Mode (+0x2), NOT bM_bPaused (+0x5).
    // binary @ 0x00151c18..0x00151c20
    if (game && game_work.bM_Mode) return;

    pos.z = 0.0f;

    bool wasAboveThresh = (m_LifeTimer >= SOUND_THRESH);
    m_LifeTimer -= dt;

    // Sound trigger on 1.66 crossing. v1.6.1 MissControl::Update @0x0019e15c sound block
    // ASM-verified: 2026-05-18 v1.6.1 v1.6.1 MissControl::Update @0x0019e15c (re-analyst)
    if (wasAboveThresh && m_LifeTimer < SOUND_THRESH && m_bComboActive && m_bPlaySound) {
        char buf[0x40];
        bool altPlayed = false;
        if (m_bUseComboSound != 0) {
            ItemManager* im = ItemManager::GetInstance();
            altPlayed = im ? im->PlayAlternateComboSound(m_ComboCount - 3) : false;
        }

        if (!altPlayed) {
            if (m_bUseComboSound == 0) {
                std::strcpy(buf, "New-best-score");  // literal 0x001b96ba (DAT_00151d88)
            } else {
                int n;
                if      (m_ComboCount < 4)   n = 1;
                else if (m_ComboCount < 10)  n = m_ComboCount - 2;
                else                         n = 8;
                // NOTE: binary uses "%i" not "%d" (rodata @ 0x001bbdc3, DAT_00151d84)
                std::snprintf(buf, sizeof(buf), "combo-%i", n);
            }
            if (game && game_work.mGameSound) {
                // binary SFXPlay args: vol=0.25f (s0=0x3e800000), pitch=1.0f (s1=0x3f800000)
                // binary @ 0x00151cd4/cda: vmov.f32 s0,0x3e800000; vmov.f32 s1,0x3f800000
                game_work.mGameSound->SFXPlay(buf, /*vol*/0.25f, /*pitch*/1.0f);
            }
        }
    }

    // Slot release when fully faded. binary @ 0x00151d0a..0x00151d28.
    if (m_LifeTimer <= 0.0f) {
        // Binary fires m_RemoveCallback BEFORE writing m_Active = 0.
        // Binary does NOT clear m_LifeTimer or m_bComboActive here.
        // ASM-verified: 2026-05-20 v1.6.1 binary @ 0x00151d0a (re-analyst)
        m_RemoveCallback(this);
        m_Active = 0;
    }
}

// --- Draw ------------------------------------------------------------------

// ASM-verified: 2026-05-24 v1.6.1 MissControl::Draw @ 0x0019f54c (re-analyst)
// TODO: v1.6.1 0x0019f54c (MissControl::Draw) — the interior offsets quoted below
// (0x00151f94/0x00151fe0/0x00151fe4/0x00152186/0x00152294/0x001522a0) are stale v1.5.x; the
// function itself is @0x0019f54c. Re-map the sub-block addresses on the next pass.
// Quad-origin formula:
//
//   origin = drawPos + this->pos + Vec3(480, 320, 0) * m_HudScale  (Vec3*Vec3)
//
//   drawPos derivation:
//     init from _Vector3<float>::Zero @0x002d9288 (.bss, static-ctor filled) = Vec3(0,0,0)
//     if (m_FlashTimer > 0): drawPos REPLACED with Vec3(RandUint(8)-4, RandUint(8)-4, 0); m_FlashTimer--
//
//     if (m_LifeTimer <= 0.0f):               // passive miss-marker path only
//         return unless FailureEnabled() && !IsMultiplayer()
//                       && fabs(m_PauseAmount) < 1.0     // Zen / MP / settled pause: no X-marks
//         drawPos.y -= 3.0f * pos.y * fabs(m_PauseAmount)   // no else-branch
void MissControl::Draw(float* hudScaleRaw) {
    const _Vector3<float>& hudScale = *reinterpret_cast<const _Vector3<float>*>(hudScaleRaw);
    // ASM-verified: 2026-05-11 v1.6.1 MissControl::Draw @ 0x0019f54c first ~20 instructions
    // (re-analyst). Binary's Draw has NO entry-gate on m_bComboActive or
    // m_bFlashing -- those are UV-pickers later in the function, not gates.
    // The disappear mechanism for finished combo popups is the m_Active=0
    // write in Update's slot-release tail (binary @ MissControl::Update);
    // HUD::Draw filters on m_Active (src/hud/HUD.cpp:88) so this Draw
    // doesn't even get called for released slots.

    // _Vector3<float>::Zero global @0x002d9288 -- lives in .bss and is FILLED BY A STATIC
    // CTOR pre-OspMain, so it is not a zero-in-the-image constant; the value happens to be
    // (0,0,0). Binary loads Zero.{x,y,z} into stack-local drawPos.
    // ASM-verified: 2026-07-28T00:00Z v1.6.1 MissControl::Draw @ 0x0019f54c (re-analyst)
    _Vector3<float> drawPos(0.0f, 0.0f, 0.0f);

    // Jitter: binary REPLACES drawPos with jitter Vec3 (not an offset).
    // binary @ 0x00151f94..0x00151fe0: drawPos = Vec3(rx-4, ry-4, 0); --m_FlashTimer
    if (m_FlashTimer > 0) {
        // ASM-spec v1.6.1 MissControl::Draw @0x0019f54c (outlined helper
        // T.892 @0x0019df0c): Math::g_random.Rand32(8) x2 -> (rx-4, ry-4, 0)
        int rx = (int)(uint8_t)Math::g_Random.Rand32(8);
        int ry = (int)(uint8_t)Math::g_Random.Rand32(8);
        drawPos.x = (float)(rx - 4);
        drawPos.y = (float)(ry - 4);
        drawPos.z = 0.0f;  // DAT_00152294 = 0.0
        m_FlashTimer--;
    }

    // m_LifeTimer branch ladder (v1.6.1 MissControl::Draw @0x0019f54c):
    //   > 1.66f (SOUND_THRESH)  -> early return (popup invisible during the
    //                              0.15s spawn-grace from MakeCritical's 1.81 init)
    //   > 0                      -> pulse-scale animation: scale = |SinIdx(phase)|
    //                              with a clamp ladder; quad size = m_Size * scale
    //   <= 0                     -> y-position jiggle for failure-feedback animation;
    //                              draw still proceeds (visual = passive miss markers)
    float pulseScale = 1.0f;
    if (m_LifeTimer > 0.0f) {
        if (m_LifeTimer > SOUND_THRESH) return;
        // Pulse-scale: phase factor is exactly 182.0f (DAT_001522a0 = 0x43360000).
        // binary @ 0x00151fe4: phase = (m_LifeTimer / 1.66) * 360.0 * 6.0 * 182.0
        const float phase_f =
            (m_LifeTimer / SOUND_THRESH) * 360.0f * 6.0f * 182.0f;
        const uint16_t pidx = (phase_f > 0.0f) ? (uint16_t)(int)phase_f : 0;
        pulseScale = std::fabs(SinIdx(pidx));
        // Clamp ladder (binary @ 0x00152034..0x00152088):
        //   if phase_f > 16380 && phase_f < 376740:
        //     if phase_f >= 32760 && phase_f <= 360360: pulseScale = 0.65 (forced)
        //     else                                    : pulseScale = max(pulseScale, 0.65)
        // ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00152034 (re-analyst)
        // Outer band: strict > / < (bhi/blo). Inner band: inclusive >= / <= (bmi/ble).
        if (phase_f > MISS_PULSE_PHASE_LO && phase_f < MISS_PULSE_PHASE_HI) {
            if (phase_f >= MISS_PULSE_NARROW_LO && phase_f <= MISS_PULSE_NARROW_HI) {
                pulseScale = MISS_PULSE_FLOOR;
            } else if (pulseScale < MISS_PULSE_FLOOR) {
                pulseScale = MISS_PULSE_FLOOR;
            }
        }
    } else {
        // m_LifeTimer <= 0 -- passive miss-marker path.
        // Binary has NO Game::GetInstance gate here -- reads game_work directly.
        // ASM-spec v1.6.1 MissControl::Draw @0x0019f6a4: lifeTimer<=0 path returns unless FailureEnabled()&&!IsMultiplayer()&&fabs(m_PauseAmount)<1.0; then drawPos.y -= 3*pos.y*fabs(m_PauseAmount). No else-branch.
        const float pause = fabsf(game_work.m_PauseAmount);
        if (!Mortar::FailureEnabled(game_work.gameMode)) return;
        if (IsMultiplayer()) return;
        if (pause >= 1.0f) return;
        drawPos.y -= 3.0f * pos.y * pause;
    }

    // Texture validity check (binary @ 0x001520c6 -- after pulse/pos path)
    if (!m_Texture.IsValid()) return;

    m_Texture->Set();

    // Scale: size * pulseScale; Z = size.z * pulseScale (= 0 * pulseScale = 0.0f).
    // binary @ 0x001520c6..0x001520ec: Vec3 scaledSize = size * pulseScale; Scale44(scaledSize)
    // PORT DIVERGE was 1.0f for Z -- corrected to size.z * pulseScale per spec.
    Matrix44 mat = Matrix44::MakeScale(size.x * pulseScale,
                                       size.y * pulseScale,
                                       size.z * pulseScale);  // size.z=0 so this is 0.0f
    if (m_Timer != 0.0f) {
        uint16_t a = (uint16_t)(int)(m_Timer * 182.0f);  // DAT_001522a0 = 182.0f exact
        mat.RotZ44(SinIdx(a), CosIdx(a));
    }
    // Anchor offset (binary @ 0x00152140..0x00152186):
    //   final = drawPos + this->pos + Vec3(480, 320, 0) * m_HudScale  (Vec3*Vec3 componentwise)
    // ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00152140 (re-analyst)
    (void)hudScale;  // per-frame hudScale arg is unused for MissControl
    _Vector3<float> t1 = drawPos + pos;
    _Vector3<float> anchor(480.0f, 320.0f, 0.0f);
    _Vector3<float> anchorScaled = anchor * m_HudScale;  // Vec3*Vec3 componentwise -- binary blx 0x001060ec
    _Vector3<float> final_t = t1 + anchorScaled;
    mat.GlobalTranslate44(final_t.x, final_t.y, final_t.z);

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    // Binary @ 0x001521ac: scale alpha by game_work.mHud->m_DrawAlpha (HUD + 0x20).
    // Binary has NO Game::GetInstance gate here -- reads game_work.mHud directly.
    // Skips multiplier when ts >= 1.0 (bpl branch @ 0x001521aa -> 0x001521d8).
    // ASM-verified: 2026-05-24 v1.6.1 binary @ 0x001520ec (re-analyst)
    Colour tint = m_DrawColour;
    {
        float ts = game_work.mHud->m_DrawAlpha;  // HUD + 0x20 (per-frame draw-alpha; v1.6.1 MissControl::Draw @0x0019f7e0)
        if (ts < 1.0f) {
            int aScaled = (int)((float)tint.a * ts);
            if (aScaled < 1)          tint.a = 0;
            else if (aScaled >= 255)  tint.a = 255;
            else                      tint.a = (uint8_t)aScaled;
        }
    }
    // UV crop based on m_bComboActive / m_bFlashing.
    // ASM-verified: 2026-05-10 v1.6.1 MissControl::Draw @ 0x0019f54c (re-analyst)
    //   combo:    u0=0.0  u1=1.0  v0=0.0   v1=1.0   (full quad)
    //   inactive: u0=0.0  u1=0.5  v0=0.25  v1=0.75  (left half, vertical centre)
    //   active:   u0=0.5  u1=1.0  v0=0.25  v1=0.75  (right half, vertical centre)
    float u0, v0, du, dv;
    if (m_bComboActive) {
        u0 = 0.0f; v0 = 0.0f;  du = 1.0f; dv = 1.0f;
    } else if (!m_bFlashing) {
        u0 = 0.0f; v0 = 0.25f; du = 0.5f; dv = 0.5f;
    } else {
        u0 = 0.5f; v0 = 0.25f; du = 0.5f; dv = 0.5f;
    }

    // v1.6.1 MissControl::Draw @0x0019f54c: DrawQuadUnCached(colour,uMin,uMax,vMin,vMax,fx).
    const float u1 = u0 + du;
    const float v1 = v0 + dv;
    Mortar::Mesh::DrawQuadUnCached(tint, u0, u1, v0, v1, 0);

    m_Texture->UnSet();
}

// ASM-spec v1.6.1 GetCurrentMissCount @0x11a10c
// Returns the global miss count from game_work; player arg ignored (single global counter).
unsigned char GetCurrentMissCount(int /*player*/) {
    return game_work.missCount;
}
