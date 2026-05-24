// Analysed: 2026-05-03T00:00
#include "MissControl.h"
#include "HUD.h"
#include "HUDLayer.h"
#include "asset/TextureManager.h"
#include "Game.h"
#include "game/GameMode.h"
#include "engine/audio/GameSound.h"
#include "game/ItemManager.h"
#include "game/WaveManager.h"
#include "math/Matrix44.h"
#include "math/MathUtil.h"
#include "render/MatrixManager.h"
#include "render/Renderer.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/gl_funcs.h"
#include "debug/Logger.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include "game/GameWork.h"

// Pool size: binary CreatePool(0xC, hud) = 12 slots. DIFFERS: was 9.
// binary @ 0x001512d8
static constexpr int MISS_POOL_SIZE = 12;

// Round-robin cursor. Binary: leaves cursor at the FOUND slot index.
static int s_NextSlot = 0;

static MissControl* s_Pool[MISS_POOL_SIZE] = { nullptr };
static bool s_PoolAllocated = false;

static Mortar::SmartPtr<Mortar::Texture> s_TexCritical;
static Mortar::SmartPtr<Mortar::Texture> s_TexRare;
static Mortar::SmartPtr<Mortar::Texture> s_TexCross;
static bool s_TexturesLoaded = false;

// MakeCritical / MakeRare fade init. DAT_001518b8 = 1.81f.
// binary @ 0x00151764 (MakeCritical), 0x001518d8 (MakeRare)
static constexpr float MISS_FADE_INIT = 1.81f;

// Screen clamp rectangle (centred ortho). binary @ 0x001518c0..0x001518cc
static constexpr float CLAMP_X_HI =  240.0f;
static constexpr float CLAMP_X_LO = -240.0f;
static constexpr float CLAMP_Y_HI =  160.0f;
static constexpr float CLAMP_Y_LO = -160.0f;

// Combo separation distance (sqrt = 70 px). DAT_00151d58 = 4900.0
static constexpr float SEP_DIST_SQR = 4900.0f;
static constexpr float SEP_TARGET   = 70.0f;   // DAT_00151d5c

// Sound threshold crossing. DAT_00151d64 = 1.66f
static constexpr float SOUND_THRESH = 1.66f;

// Screen-clamp half-extents (centred ortho). DAT_00151f50, DAT_00151f54
static constexpr float MISS_CLAMP_HALF_X = 240.0f;
static constexpr float MISS_CLAMP_HALF_Y = 160.0f;

// Pulse banding thresholds. DAT_001522a4..DAT_001522b4. ASM-verified
// 2026-05-10 (asm-inspector byte-level IEEE-754 decode).
static constexpr float MISS_PULSE_FLOOR       = 0.65f;    // DAT_001522b4
static constexpr float MISS_PULSE_PHASE_LO    = 16376.0f; // DAT_001522a4
static constexpr float MISS_PULSE_PHASE_HI    = 376992.0f; // DAT_001522a8
static constexpr float MISS_PULSE_NARROW_LO   = 32752.0f; // DAT_001522ac
static constexpr float MISS_PULSE_NARROW_HI   = 360104.0f; // DAT_001522b0

// --- Static members -------------------------------------------------------

int   MissControl::s_NumCriticals = 0;
float MissControl::s_DtMod        = 0.5f;  // (float)0 + 0.5 initial

// Binary @ 0x001515a4 -- combo overlay textures [0..9] = combo_2..combo_11.
Mortar::SmartPtr<Mortar::Texture> MissControl::s_ComboTextures[10];

// --- ctor / dtor -----------------------------------------------------------

MissControl::MissControl()
    : m_AnimState(0)
    , m_bVisible(0)
    , m_JitterTimer(0)
    , m_FadeAlpha(0.0f)
    , m_bComboActive(0)
    , m_bUseSound(0)
    , m_ComboCount(0)
    , m_AlphaScale(1.0f)
{
    m_Active        = 0;   // pool slot starts free; Init/Make* sets to 1
    m_bNoDestructor = 1;
    // binary Init writes field_0x34 = 1 ("configured" flag), NOT 0x200.
    m_LayerFlags    = Mortar::HUD_LAYER_DEFAULT;
}

MissControl::~MissControl() = default;

// --- vtable overrides -------------------------------------------------------

// Binary @ 0x001513cc -- vtable[5]. Drops m_Texture SmartPtr ref.
void MissControl::Release() {
    m_Texture.SetNull();
}

// ASM-verified: 2026-05-24 binary @ 0x00150fa4 (re-analyst)
// vtable[4] @ 0x00150fa4
void MissControl::Init() {
    m_bComboActive = 0;
    m_Active       = 1;   // binary field_0x30 = 1; marks slot as busy/active
    m_Timer        = 0.0f;  // rotation (+0x2c)
    field_0x8c     = 1;   // +0x8c = 1 (sound-enable gate)
    // Binary @ 0x00150fc2..0x00150fd4: movs r6, #0x1; str r6, [r0, #0x34].
    m_LayerFlags   = Mortar::HUD_LAYER_DEFAULT;  // "configured" flag
    m_AnimState    = 0;
    m_Texture      = s_TexCritical;
    m_FadeAlpha    = 0.0f;
    m_Active       = 1;   // binary writes field_0x30 twice (second write is redundant but faithful)
    m_ComboCount   = 0;
    m_bPendingRemoval = 0;  // +0x33 = 0 (binary Init @ 0x00150ff6)
    m_bUseSound    = 0;
    m_AlphaScale   = 1.0f;
    // DIFFERS: binary calls GetWidth() twice (not GetHeight) -- visually incorrect but binary-faithful.
    // Port uses (W+1, H+1, 0) for visual correctness.
    // DIFFERS: original = (GetWidth()>>1)+1, (GetWidth()>>1)+1 from Init @ 0x00150fa4;
    //          port uses (W+1, H+1) -- see spec note 1 for Init.
    if (s_TexCritical.IsValid()) {
        size = Vec3((float)(s_TexCritical->m_Width >> 1) + 1,
                    (float)(s_TexCritical->m_Width >> 1) + 1,
                    0.0f);
    } else {
        size = Vec3(0.0f, 0.0f, 0.0f);
    }
    // base init for transform -- binary calls vtable[2] base (HUDControl3d base)
    HUDControl3d::Init();
}

// ASM-verified: 2026-05-24 binary @ 0x00150f14 (re-analyst)
// vtable[6] @ 0x00150f14
void MissControl::Reset() {
    m_DrawColour   = Colour(255, 255, 255, 255);  // restore RGBA tint from DAT_00150f7c
    m_DrawColour.a = 0xff;
    m_JitterTimer  = 0;
    m_bVisible     = 0;
    if (m_FadeAlpha > 0.0f) {
        m_Active       = 0;   // binary field_0x30 = 0; frees slot
        m_DrawColour.a = 0;
    }
}

// Binary @ 0x00150e00 -- vtable[8]. No-op shadow of HUDControl::PreDraw base.
// Binary's MissControl::PreDraw is a no-op (single bx lr in the original).
// m_HudScale (+0x14) is initialised once in GameInit and not refreshed
// per-frame; the Draw call uses the stored value directly.
void MissControl::PreDraw(const Vec3& /*hudScale*/) {}

// Binary @ 0x00150dfc -- vtable[16]. Defunct: same-screen MP player-index hook.
// Defunct: same-screen MP player-index hook -- no-op stub; binary @ 0x00150dfc
int MissControl::SetPlayer(int player) {
    return player;
}

// Port specific: mirrors the quad-origin formula from MissControl::Draw
// (binary @ 0x00151f60..0x00152186) so the F1 boundary tracks the rendered quad.
// Jitter term is omitted (non-deterministic RandUint; short-lived and cosmetic).
Vec3 MissControl::GetDrawPos() const {
    Vec3 p = pos;
    if (m_FadeAlpha <= 0.0f) {
        Game* g = Game::GetInstance();
        if (g) {
            const bool failureEnabled =
                Mortar::FailureEnabled(game_work.gameMode);  // IsMultiplayer() unported -> false
            if (failureEnabled) {
                p.y -= 3.0f * pos.y * fabsf(game_work.m_GameDt);
            } else {
                p.y -= 3.0f * pos.y;
            }
        }
    }
    return Vec3(p.x + 480.0f * m_HudScale.x,
                p.y + 320.0f * m_HudScale.y,
                p.z);
}

// ASM-verified: 2026-05-24 binary @ 0x00150e3c (re-analyst)
// vtable[15] @ 0x00150e3c
void MissControl::Skip() {
    // Binary reads GameWork.missCount (+0x14) as the cap, not hardcoded 1.
    // binary @ 0x00150e3c: ldrb r3,[r2,#0x14]; cmp r3,r4 (r4 = m_AnimState)
    uint8_t cap = game_work.missCount;
    if (m_AnimState < cap) {
        m_JitterTimer  = 0;
        m_DrawColour.a = 0xff;
        m_bVisible     = 1;
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
    // ASM-verified: 2026-05-18 binary @ 0x001515a4 (re-analyst)
    // Binary loop: iVar3=1..10; loads combo_%d.tex only for iVar3>=3.
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

// Binary @ 0x001512d8 -- port uses static array s_Pool[N] instead of binary's
//   operator new[] + manual [size][count] header. Equivalent behaviour for trivially-
//   destructible MissControl; HUD::AddControl(.,.,false) registers each as non-owned.
void MissControl::AllocatePool() {
    if (s_PoolAllocated) return;
    Game* game = Game::GetInstance();
    if (!game || !game_work.mHud) {
        LOG_WARN("MissControl", "AllocatePool: HUD not ready");
        return;
    }
    for (int i = 0; i < MISS_POOL_SIZE; ++i) {
        s_Pool[i] = new MissControl();
        game_work.mHud->AddControl(s_Pool[i]);
    }
    s_PoolAllocated = true;
    LOG_DEBUG("MissControl", "AllocatePool: %d slots", MISS_POOL_SIZE);
}

const Mortar::SmartPtr<Mortar::Texture>& MissControl::GetCrossTexture() {
    return s_TexCross;
}

// --- CleanPool -------------------------------------------------------------

// Binary @ 0x00150e74 -- delete every pool slot, null the pool ptr. Called from GameExit @ 0x0016d086.
void MissControl::CleanPool() {
    if (!s_PoolAllocated) return;
    for (int i = 0; i < MISS_POOL_SIZE; i++) {
        delete s_Pool[i];
        s_Pool[i] = nullptr;
    }
    s_PoolAllocated = false;
}

// --- GetFree ---------------------------------------------------------------

// binary @ 0x00150da4: cursor left at FOUND slot index, not idx+1.
MissControl* MissControl::GetFree() {
    if (!s_PoolAllocated) return nullptr;
    int idx = s_NextSlot;
    for (int tries = 0; tries < MISS_POOL_SIZE; ++tries) {
        if (s_Pool[idx] && s_Pool[idx]->m_Active == 0) break;
        idx = (idx + 1) % MISS_POOL_SIZE;
    }
    s_NextSlot = idx;  // binary leaves cursor at found slot, not +1
    return s_Pool[idx];
}

// --- Make* -----------------------------------------------------------------

// ASM-verified: 2026-05-24 binary @ 0x00151764 (re-analyst)
// binary @ 0x00151764: Init() first, then tex + flags, then half/clamp/restore size.
void MissControl::MakeCritical(Vec3 pos, int playerIdx) {
    Init();
    m_Texture      = s_TexCritical;
    m_FadeAlpha    = MISS_FADE_INIT;
    m_bVisible     = 1;
    m_DrawColour.a = 0xff;
    m_AnimState    = 3;
    this->pos      = pos;
    m_bComboActive = 1;
    m_JitterTimer  = 0;
    if (s_TexCritical.IsValid()) {
        uint32_t w = s_TexCritical->m_Width;
        uint32_t h = s_TexCritical->m_Height;
        // size = (w+1, h+1, 0), halved for clamp, then restored
        size.x = (float)(w + 1);
        size.y = (float)(h + 1);
        size.z = 0.0f;  // DAT_001518bc = 0.0
        size.x *= 0.5f;
        size.y *= 0.5f;
        // Screen clamps use HALF size (binary @ 0x001518c0..0x001518cc)
        if (size.x + this->pos.x >  CLAMP_X_HI) this->pos.x =  CLAMP_X_HI - size.x;
        if (size.y + this->pos.y >  CLAMP_Y_HI) this->pos.y =  CLAMP_Y_HI - size.y;
        if (this->pos.x - size.x <  CLAMP_X_LO) this->pos.x =  CLAMP_X_LO + size.x;
        if (this->pos.y - size.y <  CLAMP_Y_LO) this->pos.y =  CLAMP_Y_LO + size.y;
        // Restore full size
        size.x += size.x;
        size.y += size.y;
    }
    SetPlayer(playerIdx);
}

// ASM-verified: 2026-05-24 binary @ 0x001518d8 (re-analyst)
// binary @ 0x001518d8: same as MakeCritical but uses s_TexRare, sets m_AlphaScale=0.5,
// and does NOT call SetPlayer.
void MissControl::MakeRare(Vec3 pos) {
    Init();
    m_Texture      = s_TexRare;
    m_FadeAlpha    = MISS_FADE_INIT;
    m_bVisible     = 1;
    m_DrawColour.a = 0xff;
    m_AnimState    = 3;
    this->pos      = pos;
    m_bComboActive = 1;
    m_JitterTimer  = 0;
    if (s_TexRare.IsValid()) {
        uint32_t w = s_TexRare->m_Width;
        uint32_t h = s_TexRare->m_Height;
        size.x = (float)(w + 1);
        size.y = (float)(h + 1);
        size.z = 0.0f;  // DAT_00151a30 = 0.0
        m_AlphaScale = 0.5f;  // written between size init and HalfScale (binary @ 0x001518d8)
        size.x *= 0.5f;
        size.y *= 0.5f;
        // Screen clamps use HALF size (binary @ 0x00151a2c..0x00151a40)
        if (size.x + this->pos.x >  CLAMP_X_HI) this->pos.x =  CLAMP_X_HI - size.x;
        if (size.y + this->pos.y >  CLAMP_Y_HI) this->pos.y =  CLAMP_Y_HI - size.y;
        if (this->pos.x - size.x <  CLAMP_X_LO) this->pos.x =  CLAMP_X_LO + size.x;
        if (this->pos.y - size.y <  CLAMP_Y_LO) this->pos.y =  CLAMP_Y_LO + size.y;
        // Restore full size
        size.x += size.x;
        size.y += size.y;
    }
    // MakeRare does NOT call SetPlayer (unlike MakeCritical)
}

// ASM-verified: 2026-05-24 binary @ 0x001515a4 (re-analyst)
// binary @ 0x001515a4
// Picks combo_N.tex where N = clamp(comboCount, 2, 11); maps to s_ComboTextures[idx].
// Sets m_bComboActive=1, m_bUseSound=1, m_ComboCount=combo, m_FadeAlpha=1.811, anim=3, visible=1.
void MissControl::MakeCombo(Vec3 pos, int comboCount, int entityType) {
    Init();
    // Texture pick uses CALLER's comboCount (before arcade override).
    // binary @ 0x001515a4: idx computed before arcade m_ComboCount override.
    int idx;
    if (comboCount < 2)       idx = 0;
    else if (comboCount < 10) idx = comboCount - 1;
    else                      idx = 9;
    if (s_ComboTextures[idx].IsValid()) {
        m_Texture = s_ComboTextures[idx];
    }
    // m_bUseSound = 1 (binary @ 0x001515a4, written BEFORE arcade override)
    m_bUseSound    = 1;
    m_bComboActive = 1;
    m_ComboCount   = comboCount;  // caller's value stored first
    // Arcade-mode override: after storing caller's comboCount and picking texture,
    // binary overrides m_ComboCount with (int)(WaveManager::GetSpeed(0) + 0.65f).
    // binary @ 0x001515a4 (AFTER m_ComboCount = comboCount, AFTER texture lookup)
    Game* g = Game::GetInstance();
    if (g && game_work.gameMode == Mortar::GAME_MODE_ARCADE) {
        WaveManager* wm = WaveManager::GetInstance();
        if (wm) m_ComboCount = (int)(wm->GetSpeed(0) + 0.65f);
    }
    m_DrawColour.a = 0xff;
    m_FadeAlpha    = MISS_FADE_INIT;  // DAT_00151740 = 1.81f
    m_AnimState    = 3;
    m_bVisible     = 1;
    this->pos      = pos;
    m_JitterTimer  = 0;
    // Size: (w+1, h+1, 0), halved for clamp, then restored
    if (s_ComboTextures[idx].IsValid()) {
        uint32_t w = s_ComboTextures[idx]->m_Width;
        uint32_t h = s_ComboTextures[idx]->m_Height;
        size.x = (float)(w + 1);
        size.y = (float)(h + 1);
        size.z = 0.0f;  // DAT_00151744 = 0.0
        size.x *= 0.5f;
        size.y *= 0.5f;
        // Screen clamps use HALF size (binary @ 0x00151748..0x00151754)
        if (size.x + this->pos.x >  CLAMP_X_HI) this->pos.x =  CLAMP_X_HI - size.x;
        if (size.y + this->pos.y >  CLAMP_Y_HI) this->pos.y =  CLAMP_Y_HI - size.y;
        if (this->pos.x - size.x <  CLAMP_X_LO) this->pos.x =  CLAMP_X_LO + size.x;
        if (this->pos.y - size.y <  CLAMP_Y_LO) this->pos.y =  CLAMP_Y_LO + size.y;
        // Restore full size
        size.x += size.x;
        size.y += size.y;
    }
    SetPlayer(entityType);
    // ASM-verified: 2026-05-18 binary @ 0x001515a4 (re-analyst)
}

// ASM-verified: 2026-05-24 binary @ 0x00151d94 (re-analyst)
// binary @ 0x00151d94: two-path form based on whether SmartPtr is valid.
// Common prefix: Init() first, then m_DrawColour.a=0xff, then pos.
void MissControl::MakeDisappear(const Vec3& inPos, int sizeMult,
                                const Mortar::SmartPtr<Mortar::Texture>& tex) {
    Init();
    m_DrawColour.a = 0xff;  // field_0x5f = 0xff (common prefix, binary @ 0x00151d94)
    pos = inPos;
    if (tex.IsValid()) {
        // Path 1: zen-bomb X overlay (valid SmartPtr supplied).
        // binary @ 0x00151d94 path 1
        field_0x8c     = 0;   // +0x8c = 0 (sound-gate cleared)
        m_Texture      = tex;
        m_bVisible     = 1;
        m_AnimState    = 3;
        m_FadeAlpha    = MISS_FADE_INIT;  // DAT_00151f40 = 1.81f
        m_JitterTimer  = 0;
        m_bComboActive = 1;
        uint32_t w = tex->m_Width;
        uint32_t h = tex->m_Height;
        size = Vec3((float)(w + 1), (float)(h + 1), 0.0f);  // DAT_00151f44 = 0.0
        // ASM-verified: 2026-05-24 binary @ 0x00151e40 (re-analyst v2)
        // Binary: `mov r0,r4; mov r1,r7; blx 0x000f6c30` -- r7 was saved from
        // r2 = param_3 = sizeMult at function entry @ 0x00151db6.
        SetPlayer(sizeMult);
    } else {
        // Path 2: fruit miss-penalty (invalid SmartPtr = use existing texture from Init).
        // binary @ 0x00151d94 else branch
        m_JitterTimer  = (sizeMult >= 1) ? 0x1e : 0;
        m_FadeAlpha    = SOUND_THRESH;   // DAT_00151f48 = 1.66f
        m_AnimState    = 3;
        m_bVisible     = 1;
        // size = g_HudScale * 62.0f; g_HudScale defaults to (1,1,1).
        // TODO: 0x00151f5c -- resolve DAT_00151f5c (g_HudScale module-static Vec3 ptr)
        // and wire up properly. For now use default (1,1,1) * 62.0f.
        size.x = 62.0f;  // g_HudScale.x * DAT_00151f4c (62.0f)
        size.y = 62.0f;  // g_HudScale.y * DAT_00151f4c
        size.z = 62.0f;  // g_HudScale.z * DAT_00151f4c
        // ASM-verified: 2026-05-24 binary @ 0x00151e94 (re-analyst v2)
        // Same pattern as path 1: r1 = r7 = sizeMult.
        SetPlayer(sizeMult);
        // Path 2 size is set again after SetPlayer (binary sets size twice -- same value).
        size.x = 62.0f;
        size.y = 62.0f;
        size.z = 62.0f;
        // Pos clamp using HALF size (binary @ 0x00151f50..0x00151f54 clamps)
        float halfX = size.x * 0.5f;
        if (pos.x >  MISS_CLAMP_HALF_X - halfX) pos.x =  MISS_CLAMP_HALF_X - halfX;
        if (pos.x < -MISS_CLAMP_HALF_X + halfX) pos.x = -MISS_CLAMP_HALF_X + halfX;
        float halfY = size.y * 0.5f;
        if (pos.y >  MISS_CLAMP_HALF_Y - halfY) pos.y =  MISS_CLAMP_HALF_Y - halfY;
        if (pos.y < -MISS_CLAMP_HALF_Y + halfY) pos.y = -MISS_CLAMP_HALF_Y + halfY;
        // Binary path 2 does NOT assign m_Active=1 (Init already set it).
        // Binary path 2 does NOT assign m_Texture=s_TexCross (keeps Init's s_TexCritical).
    }
}

// --- Update ----------------------------------------------------------------

// ASM-verified: 2026-05-24 binary @ 0x00151a60 (re-analyst)
// binary @ 0x00151a60
void MissControl::Update(float dt) {
    // Passive miss-counter path: 3 GameInit-spawned widgets at top of HUD.
    // Their m_AnimState is 0/1/2 (slot index); m_Active stays 0.
    // Toggle m_bVisible based on game_work.missCount vs m_AnimState -- when the
    // player has missed at least (m_AnimState + 1) fruits, the X marker
    // turns red. binary @ 0x00151a60 lines 1-10.
    Game* game = Game::GetInstance();
    uint8_t missCount = (game ? game_work.missCount : 0);
    if (!m_bVisible && m_AnimState < missCount) {
        m_JitterTimer  = 0x1e;
        m_DrawColour.a = 0xff;
        m_bVisible     = 1;
    }

    // Combo separation force: if m_bComboActive, repel busy neighbours within 70px.
    // binary @ 0x00151a60 combo block (~50 instructions)
    if (m_bComboActive) {
        // s_NumCriticals++ happens AT THE TOP of the combo block (before iteration).
        // binary @ 0x00151ac6 -- incremented before the pool loop.
        ++s_NumCriticals;

        // TODO: 0x00151d78 -- accel seed: asm reads floats from *(float**)(base+DAT_00151d78)[0..1].
        // Likely {0.0, 0.0} constants; port initialises accX/Y to 0.0f (matches expected default).
        // Verify the resolved rodata bytes to confirm.
        float accX = 0.0f, accY = 0.0f;
        for (int k = 0; k < MISS_POOL_SIZE; ++k) {
            MissControl* other = s_Pool[k];
            if (!other || other == this || !other->m_Active) continue;
            float dx = other->pos.x - pos.x;
            float dy = other->pos.y - pos.y;
            float distSq = dx*dx + dy*dy;
            if (distSq >= SEP_DIST_SQR) continue;
            float dist = 1.0f;
            if (distSq <= 0.0f) {
                // random direction when coincident (binary uses RandUint(0xff3a) -> SinIdx/CosIdx)
                uint16_t a = (uint16_t)(rand() % 0xff3a);
                dx = SinIdx(a);
                dy = CosIdx(a);
            } else {
                dist = sqrtf(distSq);
            }
            dx /= dist;
            dy /= dist;
            // ASM-verified: 2026-05-24 binary @ 0x00151b80..0x00151bbc (re-analyst v2)
            // Binary chains three Vec2 *= scalar calls then a -= :
            //   a_Stack_60 = dir * (70 - dist)
            //   a_Stack_68 = a_Stack_60 * dt
            //   _Stack_70  = a_Stack_68 * 15.0
            //   accel -= _Stack_70                     // repel: self moves AWAY from other
            // i.e. accel -= dir * (70 - dist) * dt * 15.0
            float force = (SEP_TARGET - dist) * dt * 15.0f;
            accX -= dx * force;
            accY -= dy * force;
        }
        pos.x += accX;
        pos.y += accY;
        // Scale dt by combo modifier
        dt = dt * s_DtMod * m_AlphaScale;
    }

    if (m_FadeAlpha <= 0.0f) {
        // Passive deactivation: if missCount went DOWN below this slot
        // (e.g. between rounds when the counter resets), turn the X off.
        // binary @ 0x00151c08..0x00151d28
        if (!m_bVisible) return;
        if (m_AnimState < missCount) return;
        m_JitterTimer  = 0x1e;
        m_DrawColour.a = 0xff;
        m_bVisible     = 0;
        return;
    }

    // Pause guard: binary reads game_work.m_Paused (+0x2), NOT m_LevelTransitionFlag (+0x5).
    // binary @ 0x00151c18..0x00151c20
    if (game && game_work.m_Paused) return;

    pos.z = 0.0f;

    bool wasAboveThresh = (m_FadeAlpha >= SOUND_THRESH);
    m_FadeAlpha -= dt;

    // Sound trigger on 1.66 crossing. binary @ 0x00151a60 sound block
    // ASM-verified: 2026-05-18 binary @ 0x00151a60 (re-analyst)
    if (wasAboveThresh && m_FadeAlpha < SOUND_THRESH && m_bComboActive && field_0x8c) {
        char buf[0x40];
        bool altPlayed = false;
        if (m_bUseSound != 0) {
            ItemManager* im = ItemManager::GetInstance();
            altPlayed = im ? im->PlayAlternateComboSound(m_ComboCount - 3) : false;
        }

        if (!altPlayed) {
            if (m_bUseSound == 0) {
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
    if (m_FadeAlpha <= 0.0f) {
        // Binary fires m_RemoveCallback BEFORE writing m_Active = 0.
        // Binary does NOT clear m_FadeAlpha or m_bComboActive here.
        // ASM-verified: 2026-05-20 binary @ 0x00151d0a (re-analyst)
        m_RemoveCallback(this);
        m_Active = 0;
    }
}

// --- Draw ------------------------------------------------------------------

// ASM-verified: 2026-05-20T00:00Z binary @ 0x00151f60 (re-analyst)
// ASM-verified: 2026-05-24 binary @ 0x00151f60 (re-analyst)
// Quad-origin formula (binary @ 0x00151f60..0x00152186):
//
//   origin = drawPos + this->pos + Vec3(480 * m_HudScale.x, 320 * m_HudScale.y, 0)
//
//   drawPos derivation:
//     init from global Vec3 *pfVar4 (DAT_001522c4 -- see TODO below)
//     if (m_JitterTimer > 0): drawPos REPLACED with Vec3(RandUint(8)-4, RandUint(8)-4, 0); m_JitterTimer--
//
//     if (m_FadeAlpha <= 0.0f):               // passive miss-marker path only
//         if (FailureEnabled() && !IsMultiplayer()):
//             drawPos.y -= 3.0f * pos.y * fabsf(game_work.m_GameDt)
//         else:
//             drawPos.y -= 3.0f * pos.y    // Zen / MP: park off-screen
//
// binary @ 0x00151f60
void MissControl::Draw(const Vec3& hudScale, int /*layerMask*/) {
    // ASM-verified: 2026-05-11 binary @ 0x00151f60 first ~20 instructions
    // (re-analyst). Binary's Draw has NO entry-gate on m_bComboActive or
    // m_bVisible -- those are UV-pickers later in the function, not gates.
    // The disappear mechanism for finished combo popups is the m_Active=0
    // write in Update's slot-release tail (binary @ MissControl::Update);
    // HUD::Draw filters on m_Active (src/hud/HUD.cpp:88) so this Draw
    // doesn't even get called for released slots.

    // TODO: 0x001522c4 -- Draw phase 0: drawPos is initialised from *pfVar4 (a global Vec3
    // ptr resolved from DAT_001522c4), NOT from this->pos. Binary then translates with
    // drawPos + this->pos + anchor. Needs DAT_001522c4 GOT resolution to identify the Vec3.
    // For now, port inits drawPos to (0,0,0) matching the expected zero-init of that global.
    Vec3 drawPos(0.0f, 0.0f, 0.0f);

    // Jitter: binary REPLACES drawPos with jitter Vec3 (not an offset).
    // binary @ 0x00151f94..0x00151fe0: drawPos = Vec3(rx-4, ry-4, 0); --m_JitterTimer
    if (m_JitterTimer > 0) {
        int rx = (int)(uint8_t)(rand() % 8);
        int ry = (int)(uint8_t)(rand() % 8);
        drawPos.x = (float)(rx - 4);
        drawPos.y = (float)(ry - 4);
        drawPos.z = 0.0f;  // DAT_00152294 = 0.0
        m_JitterTimer--;
    }

    // m_FadeAlpha branch ladder (binary @ 0x00151f60):
    //   > 1.66f (SOUND_THRESH)  -> early return (popup invisible during the
    //                              0.15s spawn-grace from MakeCritical's 1.81 init)
    //   > 0                      -> pulse-scale animation: scale = |SinIdx(phase)|
    //                              with a clamp ladder; quad size = m_Size * scale
    //   <= 0                     -> y-position jiggle for failure-feedback animation;
    //                              draw still proceeds (visual = passive miss markers)
    float pulseScale = 1.0f;
    if (m_FadeAlpha > 0.0f) {
        if (m_FadeAlpha > SOUND_THRESH) return;
        // Pulse-scale: phase factor is exactly 182.0f (DAT_001522a0 = 0x43360000).
        // binary @ 0x00151fe4: phase = (m_FadeAlpha / 1.66) * 360.0 * 6.0 * 182.0
        const float phase_f =
            (m_FadeAlpha / SOUND_THRESH) * 360.0f * 6.0f * 182.0f;
        const uint16_t pidx = (phase_f > 0.0f) ? (uint16_t)(int)phase_f : 0;
        pulseScale = std::fabs(SinIdx(pidx));
        // Clamp ladder (binary @ 0x00152034..0x00152088):
        //   if 16376 < phase_f < 376992:
        //     if 32752 < phase_f < 360104: pulseScale = 0.65 (forced)
        //     else                       : pulseScale = max(pulseScale, 0.65)
        if (phase_f > MISS_PULSE_PHASE_LO && phase_f < MISS_PULSE_PHASE_HI) {
            if (phase_f > MISS_PULSE_NARROW_LO && phase_f < MISS_PULSE_NARROW_HI) {
                pulseScale = MISS_PULSE_FLOOR;
            } else if (pulseScale < MISS_PULSE_FLOOR) {
                pulseScale = MISS_PULSE_FLOOR;
            }
        }
    } else {
        // m_FadeAlpha <= 0 -- passive miss-marker path: y-shift
        Game* g = Game::GetInstance();
        if (g) {
            const bool failureEnabled =
                Mortar::FailureEnabled(game_work.gameMode);  // IsMultiplayer() unported -> false
            if (failureEnabled) {
                drawPos.y -= 3.0f * pos.y * fabsf(game_work.m_GameDt);
            } else {
                drawPos.y -= 3.0f * pos.y;
            }
        }
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
    //   final = drawPos + this->pos + Vec3(480, 320, 0) * m_HudScale
    (void)hudScale;  // per-frame hudScale arg is unused for MissControl
    Vec3 anchor(
        480.0f * m_HudScale.x,
        320.0f * m_HudScale.y,
        0.0f);
    Vec3 final_pos = drawPos + pos + anchor;
    mat.GlobalTranslate44(final_pos);

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    // ASM-verified: 2026-05-18 binary @ 0x00151f60 (re-analyst)
    // Binary @ 0x001521ac: scale alpha by Game->hud->m_globalTimeScale (slow-mo factor)
    // only when < 1.0 (normal gameplay = 1.0, branch skipped).
    float fade = 1.0f;
    {
        Game* gFade = Game::GetInstance();
        if (gFade && game_work.mHud) {
            float ts = game_work.mHud->m_globalTimeScale;
            if (ts < 1.0f) fade = ts;
        }
    }

    // TODO: 0x001520ec -- HUD-layer fade alpha (float at MatrixManager.field_0x3c+0x20)
    // not yet wired. Binary reads it each frame and multiplies into m_DrawColour.a.
    // Setter binary site is unknown (likely a ScreenTint / fade-in-out transition).
    // Until that's RE'd, port skips the multiplier (m_DrawColour.a passes through unchanged).
    const uint8_t a = (uint8_t)(fade * (float)m_DrawColour.a);
    const uint32_t col = (uint32_t)a << 24 | (uint32_t)m_DrawColour.b << 16
                        | (uint32_t)m_DrawColour.g << 8 | (uint32_t)m_DrawColour.r;

    // UV crop based on m_bComboActive / m_bVisible.
    // ASM-verified: 2026-05-10 binary @ 0x00151f60..0x00152258 (re-analyst)
    //   combo:    u0=0.0  u1=1.0  v0=0.0   v1=1.0   (full quad)
    //   inactive: u0=0.0  u1=0.5  v0=0.25  v1=0.75  (left half, vertical centre)
    //   active:   u0=0.5  u1=1.0  v0=0.25  v1=0.75  (right half, vertical centre)
    float u0, v0, du, dv;
    if (m_bComboActive) {
        u0 = 0.0f; v0 = 0.0f;  du = 1.0f; dv = 1.0f;
    } else if (!m_bVisible) {
        u0 = 0.0f; v0 = 0.25f; du = 0.5f; dv = 0.5f;
    } else {
        u0 = 0.5f; v0 = 0.25f; du = 0.5f; dv = 0.5f;
    }

    QUADCUSTOMVERTEX v[6];
    std::memset(v, 0, sizeof(v));
    // Centred quad in [-0.5..+0.5] -- matches Renderer::DrawQuad and the
    // binary's Mortar::Mesh::DrawQuadUnCached. Matrix applies size scale
    // (full quad span = size) + translate.
    const float u1 = u0 + du;
    const float v1 = v0 + dv;
    v[0].x = -0.5f; v[0].y = -0.5f; v[0].u = u0; v[0].v = v1; v[0].colour = col;
    v[1].x =  0.5f; v[1].y = -0.5f; v[1].u = u1; v[1].v = v1; v[1].colour = col;
    v[2].x = -0.5f; v[2].y =  0.5f; v[2].u = u0; v[2].v = v0; v[2].colour = col;
    v[3].x =  0.5f; v[3].y = -0.5f; v[3].u = u1; v[3].v = v1; v[3].colour = col;
    v[4].x =  0.5f; v[4].y =  0.5f; v[4].u = u1; v[4].v = v0; v[4].colour = col;
    v[5].x = -0.5f; v[5].y =  0.5f; v[5].u = u0; v[5].v = v0; v[5].colour = col;

    if (Renderer* r = Renderer::GetInstance()) {
        r->DrawTriList(v, 6);
    }

    m_Texture->UnSet();
}
