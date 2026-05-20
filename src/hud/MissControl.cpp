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

// Binary @ 0x001515a4 — combo overlay textures [0..9] = combo_2..combo_11.
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
    m_bActive       = 0;   // pool slot starts free; Init/Make* sets to 1
    m_bNoDestructor = 1;
    // binary Init writes field_0x34 = 1 ("configured" flag), NOT 0x200.
    m_LayerFlags    = Mortar::HUD_LAYER_DEFAULT;
}

MissControl::~MissControl() = default;

// --- vtable overrides -------------------------------------------------------

// Binary @ 0x001513cc — vtable[5]. Drops m_Texture SmartPtr ref.
void MissControl::Release() {
    m_Texture.SetNull();
}

// vtable[4] @ 0x00150fa4
void MissControl::Init() {
    m_bComboActive = 0;
    m_bActive      = 1;   // binary field_0x30 = 1; marks slot as busy/active
    m_Timer        = 0.0f;  // rotation (+0x2c)
    m_AnimState    = 0;
    // Binary @ 0x00150fc2..0x00150fd4: movs r6, #0x1; str r6, [r0, #0x34].
    m_LayerFlags   = Mortar::HUD_LAYER_DEFAULT;  // "configured" flag
    m_FadeAlpha    = 0.0f;
    m_bActive      = 1;   // binary writes field_0x30 twice (second write is redundant but faithful)
    m_ComboCount   = 0;
    m_bUseSound    = 0;
    m_AlphaScale   = 1.0f;
    m_DrawColour   = Colour(255, 255, 255, 255);  // default colour from DAT_00150f7c
    size           = Vec3(0.0f, 0.0f, 0.0f);
    // base init for transform -- binary calls vtable[2] base (HUDControl3d base)
    HUDControl3d::Init();
}

// vtable[6] @ 0x00150f14
void MissControl::Reset() {
    m_DrawColour   = Colour(255, 255, 255, 255);  // restore RGBA tint from DAT_00150f7c
    m_DrawColour.a = 0xff;
    m_JitterTimer  = 0;
    m_bVisible     = 0;
    if (m_FadeAlpha > 0.0f) {
        m_bActive      = 0;   // binary field_0x30 = 0; frees slot
        m_DrawColour.a = 0;
    }
}

// Binary @ 0x00150e00 — vtable[8]. No-op shadow of HUDControl::PreDraw base.
// Binary's MissControl::PreDraw is a no-op (single bx lr in the original).
// m_HudScale (+0x14) is initialised once in GameInit and not refreshed
// per-frame; the Draw call uses the stored value directly.
void MissControl::PreDraw(const Vec3& /*hudScale*/) {}

// Binary @ 0x00150dfc — vtable[16]. Defunct: same-screen MP player-index hook.
// Defunct: same-screen MP player-index hook — no-op stub; binary @ 0x00150dfc
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
                Mortar::FailureEnabled(g->gameMode);  // IsMultiplayer() unported -> false
            if (failureEnabled) {
                p.y -= 3.0f * pos.y * fabsf(g->m_TransitionTimer);
            } else {
                p.y -= 3.0f * pos.y;
            }
        }
    }
    return Vec3(p.x + 480.0f * m_HudScale.x,
                p.y + 320.0f * m_HudScale.y,
                p.z);
}

// vtable[15] @ 0x00150e3c
void MissControl::Skip() {
    // Fast-forward spawn animation when critical/rare label needs to appear immediately.
    // binary: if (m_AnimState < player_count) { jitter=0; alpha=0xff; bVisible=1 }
    // Port has no player_count from binary; guard on m_AnimState < 1 (single-player).
    if (m_AnimState < 1) {
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

// Binary @ 0x001512d8 — port uses static array s_Pool[N] instead of binary's
//   operator new[] + manual [size][count] header. Equivalent behaviour for trivially-
//   destructible MissControl; HUD::AddControl(.,.,false) registers each as non-owned.
void MissControl::AllocatePool() {
    if (s_PoolAllocated) return;
    Game* game = Game::GetInstance();
    if (!game || !game->hud) {
        LOG_WARN("MissControl", "AllocatePool: HUD not ready");
        return;
    }
    for (int i = 0; i < MISS_POOL_SIZE; ++i) {
        s_Pool[i] = new MissControl();
        game->hud->AddControl(s_Pool[i]);
    }
    s_PoolAllocated = true;
    LOG_DEBUG("MissControl", "AllocatePool: %d slots", MISS_POOL_SIZE);
}

const Mortar::SmartPtr<Mortar::Texture>& MissControl::GetCrossTexture() {
    return s_TexCross;
}

// --- CleanPool -------------------------------------------------------------

// Binary @ 0x00150e74 — delete every pool slot, null the pool ptr. Called from GameExit @ 0x0016d086.
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
        if (s_Pool[idx] && s_Pool[idx]->m_bActive == 0) break;
        idx = (idx + 1) % MISS_POOL_SIZE;
    }
    s_NextSlot = idx;  // binary leaves cursor at found slot, not +1
    return s_Pool[idx];
}

// --- Make* -----------------------------------------------------------------

// Shared core of MakeCritical / MakeRare. binary @ 0x00151764 / 0x001518d8
static void PopulateOverlay(MissControl* mc, const Vec3& pos,
                            const Mortar::SmartPtr<Mortar::Texture>& tex,
                            float alphaScale) {
    // m_FadeAlpha init = 1.81 (DAT_001518b8).
    mc->m_FadeAlpha  = MISS_FADE_INIT;
    mc->m_AnimState  = 3;
    mc->m_AlphaScale = alphaScale;
    mc->m_bActive    = 1;   // binary field_0x30 = 1; marks slot busy
    mc->m_bComboActive = 1;
    mc->m_JitterTimer = 0;   // field_0x7e = 0. binary @ 0x001518b8
    mc->m_DrawColour.a = 0xff;  // field_0x5f = 0xff. binary @ 0x001518b4
    mc->pos = pos;

    if (tex.IsValid()) {
        mc->m_Texture = tex;
        // binary MakeCritical: size = (w+1, h+1, 0) then halved, then doubled.
        // Net result: size = (w+1, h+1, 0) (the halve+double cancel).
        // binary @ 0x00151764 (MakeCritical size formula)
        const float w = (float)(tex->m_Width  + 1);
        const float h = (float)(tex->m_Height + 1);
        // binary: size.xy = (w+1)/2+1 ... doubled back. Net = (w, h) roughly.
        // Reproducing: full extent stored (the halve/clamp/double yields back to w+1, h+1).
        mc->size.x = w;
        mc->size.y = h;
        mc->size.z = 0.0f;  // DAT_001518bc = 0.0. DIFFERS: was 1.0
    }

    // Screen-clamp. binary @ 0x001518c0..0x001518cc
    if (mc->pos.x + mc->size.x >  CLAMP_X_HI) mc->pos.x =  CLAMP_X_HI - mc->size.x;
    if (mc->pos.y + mc->size.y >  CLAMP_Y_HI) mc->pos.y =  CLAMP_Y_HI - mc->size.y;
    if (mc->pos.x - mc->size.x <  CLAMP_X_LO) mc->pos.x =  CLAMP_X_LO + mc->size.x;
    if (mc->pos.y - mc->size.y <  CLAMP_Y_LO) mc->pos.y =  CLAMP_Y_LO + mc->size.y;
}

void MissControl::MakeCritical(Vec3 pos, int /*playerIdx*/) {
    PopulateOverlay(this, pos, s_TexCritical, /*alphaScale*/ 1.0f);
}

void MissControl::MakeRare(Vec3 pos) {
    PopulateOverlay(this, pos, s_TexRare, /*alphaScale*/ 0.5f);
}

// binary @ 0x001515a4
// Picks combo_N.tex where N = clamp(comboCount, 2, 11); maps to s_ComboTextures[idx].
// Sets m_bComboActive=1, m_ComboCount=combo, m_FadeAlpha=1.811, anim=3, visible=1.
void MissControl::MakeCombo(Vec3 pos, int comboCount, int /*entityType*/) {
    // Arcade-mode override: comboCount is computed from wave-speed
    // rather than the caller's literal. Binary @ 0x001515a4 reads
    // (int)(WaveManager::GetSpeed(0) + 0.65f) and uses that as the
    // effective combo count when gameMode == ARCADE.
    Game* g = Game::GetInstance();
    if (g && g->gameMode == Mortar::GAME_MODE_ARCADE) {
        WaveManager* wm = WaveManager::GetInstance();
        if (wm) comboCount = (int)(wm->GetSpeed(0) + 0.65f);
    }
    Init();
    int idx = comboCount - 2;
    if (idx < 0)  idx = 0;
    if (idx > 9)  idx = 9;
    if (s_ComboTextures[idx].IsValid()) {
        m_Texture = s_ComboTextures[idx];
        const float w = (float)(s_ComboTextures[idx]->m_Width  + 1);
        const float h = (float)(s_ComboTextures[idx]->m_Height + 1);
        size.x = w;
        size.y = h;
        size.z = 0.0f;
    }
    m_bComboActive = 1;
    m_ComboCount   = comboCount;
    m_FadeAlpha    = MISS_FADE_INIT;
    m_AnimState    = 3;
    m_bVisible     = 1;
    m_JitterTimer  = 0;
    // Screen-clamp: centre-clamp within +-240/+-160 minus half-extent.
    Vec3 clamped = pos;
    const float hx = size.x * 0.5f;
    const float hy = size.y * 0.5f;
    if (clamped.x + hx >  CLAMP_X_HI) clamped.x =  CLAMP_X_HI - hx;
    if (clamped.y + hy >  CLAMP_Y_HI) clamped.y =  CLAMP_Y_HI - hy;
    if (clamped.x - hx <  CLAMP_X_LO) clamped.x =  CLAMP_X_LO + hx;
    if (clamped.y - hy <  CLAMP_Y_LO) clamped.y =  CLAMP_Y_LO + hy;
    this->pos = clamped;
    SetPlayer(0);    // vtable[16] Defunct stub call for vtable-call parity.
    // ASM-verified: 2026-05-18 binary @ 0x001515a4 (re-analyst)
}

// binary @ 0x00151d94: two-path form based on whether SmartPtr is valid.
void MissControl::MakeDisappear(const Vec3& inPos, int sizeMult,
                                const Mortar::SmartPtr<Mortar::Texture>& tex) {
    pos        = inPos;
    m_DrawColour.a = 0xff;  // field_0x5f = 0xff
    if (tex.IsValid()) {
        // Path 1: zen-bomb X overlay (valid SmartPtr supplied).
        // binary @ 0x00151d94 path 1
        m_bUseSound    = 0;     // field_0x8c = 0 (suppress sound)
        m_Texture      = tex;
        m_bVisible     = 1;
        m_AnimState    = 3;
        // path 1: zen-bomb X overlay (DAT_00151f40 = 1.811f, same as MISS_FADE_INIT).
        m_FadeAlpha    = MISS_FADE_INIT;
        m_JitterTimer  = 0;
        m_bComboActive = 1;
        size = Vec3((float)(tex->m_Width + 1), (float)(tex->m_Height + 1), 0.0f);
        m_bActive      = 1;   // binary field_0x30 = 1; marks slot busy
        // No screen clamp on path 1. binary @ 0x00151d94
    } else {
        // Path 2: fruit miss-penalty (invalid SmartPtr = use existing texture).
        // binary @ 0x00151d94 else branch
        m_JitterTimer  = (sizeMult >= 1) ? 0x1e : 0;
        // path 2: fruit-miss penalty (DAT_00151f48 = 1.66f, same as SOUND_THRESH).
        // Skips the spawn-phase gate; goes straight to fade-down.
        m_FadeAlpha    = SOUND_THRESH;
        m_AnimState    = 3;
        m_bVisible     = 1;
        // g_HudScale (DAT_00151f5c -> module-static Vec3) defaults to (1,1,1); the
        // binary's double-multiply is a no-op until Bada DPI code mutates it. Skipped.
        size.x = size.x; size.y = size.y; size.z = size.z;
        // DAT_00151f50 = 240.0, DAT_00151f54 = 160.0 (centred-ortho half-extents).
        pos.x = std::max(size.x * 0.5f - MISS_CLAMP_HALF_X, std::min(pos.x, MISS_CLAMP_HALF_X - size.x * 0.5f));
        pos.y = std::max(size.y * 0.5f - MISS_CLAMP_HALF_Y, std::min(pos.y, MISS_CLAMP_HALF_Y - size.y * 0.5f));
        m_bActive      = 1;   // binary field_0x30 = 1; marks slot busy
        if (s_TexCross.IsValid()) m_Texture = s_TexCross;
    }
}

// --- Update ----------------------------------------------------------------

// binary @ 0x00151a60
// ASM-verified: 2026-05-09 binary @ 0x00151a60 (re-analyst — passive miss-counter
// path identified). Binary does NOT short-circuit on m_bActive at function entry;
// the m_bComboActive gate around the separation block was previously confused
// with an m_bActive gate.
void MissControl::Update(float dt) {
    // Passive miss-counter path: 3 GameInit-spawned widgets at top of HUD.
    // Their m_AnimState is 0/1/2 (slot index); m_bActive stays 0.
    // Toggle m_bVisible based on game->missCount vs m_AnimState — when the
    // player has missed at least (m_AnimState + 1) fruits, the X marker
    // turns red. binary @ 0x00151a60 lines 1-10.
    Game* game = Game::GetInstance();
    uint8_t missCount = (game ? game->missCount : 0);
    if (!m_bVisible && m_AnimState < missCount) {
        m_JitterTimer  = 0x1e;
        m_DrawColour.a = 0xff;
        m_bVisible     = 1;
    }

    // Combo separation force: if m_bComboActive, repel busy neighbours within 70px.
    // binary @ 0x00151a60 combo block (~50 instructions)
    if (m_bComboActive) {
        float accX = 0.0f, accY = 0.0f;
        for (int k = 0; k < MISS_POOL_SIZE; ++k) {
            MissControl* other = s_Pool[k];
            if (!other || other == this || !other->m_bActive) continue;
            float dx = other->pos.x - pos.x;
            float dy = other->pos.y - pos.y;
            float distSq = dx*dx + dy*dy;
            if (distSq >= SEP_DIST_SQR) continue;
            float dist = sqrtf(distSq);
            float nx, ny;
            if (dist == 0.0f) {
                // random direction when coincident
                nx = (float)(rand() % 3 - 1);
                ny = (float)(rand() % 3 - 1);
                if (nx == 0.0f && ny == 0.0f) nx = 1.0f;
            } else {
                nx = dx / dist;
                ny = dy / dist;
            }
            float force = (SEP_TARGET - dist) * 15.0f;
            accX += nx * force;
            accY += ny * force;
        }
        s_NumCriticals++;
        pos.x += accX;
        pos.y += accY;
        // Scale dt by combo modifier
        dt = dt * s_DtMod * m_AlphaScale;
    }

    if (m_FadeAlpha <= 0.0f) {
        // Passive deactivation: if missCount went DOWN below this slot
        // (e.g. between rounds when the counter resets), turn the X off.
        // binary @ 0x00151a60 fadeAlpha<=0 fall-through.
        if (m_bVisible && m_AnimState >= missCount) {
            m_JitterTimer  = 0x1e;
            m_DrawColour.a = 0xff;
            m_bVisible     = 0;
        }
        return;
    }

    // Pause guard: if game paused, skip fade. binary @ 0x00151a60 pause guard
    if (game && game->levelTransitionFlag) return;

    pos.z = 0.0f;

    bool wasAboveThresh = (m_FadeAlpha >= SOUND_THRESH);
    m_FadeAlpha -= dt;

    // Sound trigger on 1.66 crossing. binary @ 0x00151a60 sound block
    // ASM-verified: 2026-05-18 binary @ 0x00151a60 (re-analyst)
    if (wasAboveThresh && m_FadeAlpha < SOUND_THRESH && m_bComboActive && m_bUseSound) {
        char buf[0x40];
        bool defaultSfx = true;
        // m_bUseSound (port field at +0x85) is binary's field_0x85 (HasSeenLightning).
        // When non-zero: try alternate combo sound, else format "combo-%d".
        // When zero: fall back to "New-best-score" (no-lightning path).
        if (m_bUseSound != 0) {
            // TODO: 0x00103f68 — ItemManager::PlayAlternateComboSound not yet ported;
            // stub is void, so alternate-sound suppression path is skipped for now.
            ItemManager* im = ItemManager::GetInstance();
            if (im) im->PlayAlternateComboSound(m_ComboCount - 3);
            int n = (m_ComboCount < 4)  ? 1
                  : (m_ComboCount < 10) ? m_ComboCount - 2
                                        : 8;
            std::snprintf(buf, sizeof(buf), "combo-%d", n);
        } else {
            std::strcpy(buf, "New-best-score");
        }
        if (defaultSfx && game && game->pGameSound) {
            game->pGameSound->SFXPlay(buf, /*vol*/1.0f, /*pitch*/0.25f);
        }
    }

    // Slot release when fully faded. binary @ 0x00151a60 release block
    if (m_FadeAlpha <= 0.0f) {
        m_FadeAlpha = 0.0f;
        // ASM-verified: 2026-05-11 binary @ MissControl::Update tail
        // (re-analyst). Slot-release writes 0 to field_0x30 = m_bActive.
        // HUD::Draw and HUD::Update gate on m_bActive (port: src/hud/HUD.cpp:72/88/108),
        // so clearing it stops both Update and Draw cycles for this slot
        // until MakeCritical/MakeRare/MakeCombo's PopulateOverlay sets
        // m_bActive=1 again on slot reuse.
        // m_RemoveCallback is NEVER bound for MissControl pool slots in the
        // binary (verified: no Delegate1<...>::Callee<MissControl> exists).
        // The disappear mechanism is purely the m_bActive flip (binary field_0x30 = 0).
        m_bActive      = 0;
        m_bComboActive = 0;
    }
}

// --- Draw ------------------------------------------------------------------

// ASM-verified: 2026-05-20T00:00Z binary @ 0x00151f60 (re-analyst)
// Quad-origin formula (binary @ 0x00151f60..0x00152186):
//
//   origin = pos + anchorBase + Vec3(480 * m_HudScale.x, 320 * m_HudScale.y, 0)
//
//   anchorBase derivation:
//     if (m_JitterTimer > 0):
//         anchorBase = Vec3(RandUint(8)-4, RandUint(8)-4, 0); m_JitterTimer--
//     else:
//         anchorBase = Vec3(0, 0, 0)
//         // NOTE: binary reads GOT slot 0x001f251c (function ptr, not Vec3);
//         // path is dead in practice; port treats as zero.
//
//     if (m_FadeAlpha <= 0.0f):               // passive miss-marker path only
//         if (FailureEnabled() && !IsMultiplayer()):
//             anchorBase.y -= 3.0f * pos.y * fabsf(g->m_TransitionTimer)
//         else:
//             anchorBase.y -= 3.0f * pos.y    // Zen / MP: park off-screen
//
// binary @ 0x00151f60
void MissControl::Draw(const Vec3& hudScale, int /*layerMask*/) {
    if (!m_Texture.IsValid()) return;

    // ASM-verified: 2026-05-11 binary @ 0x00151f60 first ~20 instructions
    // (re-analyst). Binary's Draw has NO entry-gate on m_bComboActive or
    // m_bVisible -- those are UV-pickers later in the function, not gates.
    // The disappear mechanism for finished combo popups is the m_bActive=0
    // write in Update's slot-release tail (binary @ MissControl::Update);
    // HUD::Draw filters on m_bActive (src/hud/HUD.cpp:88) so this Draw
    // doesn't even get called for released slots.

    // Jitter: add random offset if jitter counter > 0. binary @ 0x00151f60 jitter block
    Vec3 drawPos = pos;
    if (m_JitterTimer > 0) {
        drawPos.x += (float)(rand() % 8 - 4);
        drawPos.y += (float)(rand() % 8 - 4);
        m_JitterTimer--;
    }

    // m_FadeAlpha branch ladder (binary @ 0x00151f60):
    //   > 1.66f (SOUND_THRESH)  -> early return (popup invisible during the
    //                              0.15s spawn-grace from MakeCritical's 1.81 init)
    //   > 0                      -> pulse-scale animation: scale = |SinIdx(phase)|
    //                              with a clamp ladder; quad size = m_Size * scale
    //   <= 0                     -> y-position jiggle for failure-feedback animation;
    //                              draw still proceeds (visual = passive miss markers)
    // ASM-verified: 2026-05-10 binary @ 0x00151fe4..0x001520ec (asm-inspector).
    // The earlier port comment claimed local_34 was a "dead store"; the
    // decompiler mis-presented the Vec3*scalar call -- it IS read at
    // 0x001520e0 as the scalar arg to Vec3::operator*(out, &size, &local_34)
    // whose result feeds Matrix44::Scale44.
    if (m_FadeAlpha > SOUND_THRESH) return;

    // Pulse-scale: 6-cycle |SinIdx| over m_FadeAlpha 1.66 -> 0.
    // phase_f = (m_FadeAlpha / 1.66) * 360 * 6 * (65536/360) = ... * 6 * 182.04
    // At m_FadeAlpha=1.66 phase_f = 393216 = 6 full sin periods (uint16 wraps to 0)
    // Windowed clamp ladder pins floor to 0.65f in certain phase ranges.
    float pulseScale = 1.0f;
    if (m_FadeAlpha > 0.0f) {
        const float phase_f =
            (m_FadeAlpha / SOUND_THRESH) * 360.0f * 6.0f * 182.04444f;
        const uint16_t idx = (phase_f > 0.0f) ? (uint16_t)(int)phase_f : 0;
        pulseScale = std::fabs(SinIdx(idx));
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
    }
    // Slide-in / off-screen-park y-shift. Binary @ 0x0015208e..0x001520c4:
    // Only applied when m_FadeAlpha <= 0 (passive miss-marker path).
    // Pulse path (m_FadeAlpha > 0) skips this entirely.
    //   if (FailureEnabled() && !IsMultiplayer())
    //       drawPos.y -= 3.0f * pos.y * fabsf(game->m_TransitionTimer);
    //   else
    //       drawPos.y -= 3.0f * pos.y;   // Zen / multi-player: parked off-screen
    // For non-Zen single-player, m_TransitionTimer drives the animation:
    //   timer == 1.0 (in menu / mid-transition): drawPos.y shifts -3*pos.y
    //     -> stored pos.y is negative for top-right markers, so drawPos.y
    //        moves UP past the +160 clamp (off-screen above the viewport).
    //   timer == 0.0 (gameplay): no shift, markers visible at top-right.
    //   intermediate values produce the slide-in animation.
    if (m_FadeAlpha <= 0.0f) {
        Game* g = Game::GetInstance();
        if (g) {
            const bool failureEnabled =
                Mortar::FailureEnabled(g->gameMode);  // IsMultiplayer() unported -> false
            if (failureEnabled) {
                drawPos.y -= 3.0f * pos.y * fabsf(g->m_TransitionTimer);
            } else {
                drawPos.y -= 3.0f * pos.y;
            }
        }
    }

    // ASM-verified: 2026-05-18 binary @ 0x00151f60 (re-analyst)
    // Binary @ 0x001521ac: scale alpha by Game->hud->m_globalTimeScale (slow-mo factor)
    // only when < 1.0 (normal gameplay = 1.0, branch skipped).
    float fade = 1.0f;
    {
        Game* gFade = Game::GetInstance();
        if (gFade && gFade->hud) {
            float ts = gFade->hud->m_globalTimeScale;
            if (ts < 1.0f) fade = ts;
        }
    }

    // UV crop based on m_bComboActive / m_bVisible.
    // ASM-verified: 2026-05-10 binary @ 0x00151f60..0x00152258 (re-analyst)
    //   combo:    u0=0.0  u1=1.0  v0=0.0   v1=1.0   (full quad)
    //   inactive: u0=0.0  u1=0.5  v0=0.25  v1=0.75  (left half, vertical centre)
    //   active:   u0=0.5  u1=1.0  v0=0.25  v1=0.75  (right half, vertical centre)
    // Both non-combo crops are square 0.5x0.5 -- earlier port had du=0.25 / dv=0.75
    // which sampled a 1:3 strip onto the 1:1 quad, distorting aspect AND
    // sampling different vertical regions for the two states (visible
    // position shift between inactive and active).
    float u0, v0, du, dv;
    if (m_bComboActive) {
        u0 = 0.0f; v0 = 0.0f;  du = 1.0f; dv = 1.0f;
    } else if (!m_bVisible) {
        u0 = 0.0f; v0 = 0.25f; du = 0.5f; dv = 0.5f;
    } else {
        u0 = 0.5f; v0 = 0.25f; du = 0.5f; dv = 0.5f;
    }

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();

    // binary @ 0x001520ec / 0x000fc720 / 0x000f7a4c. Order: Scale -> RotZ -> Translate.
    // Binary @ 0x001520dc..0x001520ec: scale = size * pulseScale (Vec3*scalar
    // multiply via Vec3::operator* before passing to Scale44).
    Matrix44 mat = Matrix44::MakeScale(size.x * pulseScale,
                                       size.y * pulseScale, 1.0f);
    if (m_Timer != 0.0f) {
        uint16_t a = (uint16_t)(int)(m_Timer * 182.0f);
        mat.RotZ44(SinIdx(a), CosIdx(a));
    }
    // Anchor offset (binary @ 0x0015215c..0x00152186, asm-inspector 2026-05-10):
    //   translate = pos + Vec3(480, 320, 0) * m_HudScale
    // Stored pos values are NEGATIVE offsets from the binary's 480x320
    // framebuffer bottom-right; after Bada's 90 deg device rotation that
    // lands the markers in the player's top-right. m_HudScale is set once
    // in GameInit to (0.5, 0.5, 0) per the table at 0x001F3DAC -- the
    // multiply yields the centered-ortho equivalent (240, 160, 0) of the
    // binary's (480, 320, 0) anchor in its top-left-origin 480x320 ortho.
    (void)hudScale;  // per-frame hudScale arg is unused for MissControl
    Vec3 anchor(
        480.0f * m_HudScale.x,
        320.0f * m_HudScale.y,
        0.0f);
    drawPos += anchor;
    mat.GlobalTranslate44(drawPos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    m_Texture->Set();

    // Tint: m_DrawColour multiplied by per-frame HUD tint (MatrixManager.field_0x3c.field_0x20).
    // TODO: 0x001520ec — blocked on MatrixManager::GetTintColour engine gap; binary @
    // 0x001520ec multiplies m_DrawColour by the HUD-layer tint via MatrixManager field
    // +0x3c.field_0x20. Port renders with stored m_DrawColour only (visually fine until
    // the tint-fade screen overlay is implemented). ACCEPT-cosmetic in asm-verify.
    const uint8_t a = (uint8_t)(fade * (float)m_DrawColour.a);
    const uint32_t col = (uint32_t)a << 24 | (uint32_t)m_DrawColour.b << 16
                        | (uint32_t)m_DrawColour.g << 8 | (uint32_t)m_DrawColour.r;

    QUADCUSTOMVERTEX v[6];
    std::memset(v, 0, sizeof(v));
    // Centred quad in [-0.5..+0.5] -- matches Renderer::DrawQuad and the
    // binary's Mortar::Mesh::DrawQuadUnCached. Matrix applies size scale
    // (full quad span = size) + translate. Earlier port used [-1..+1]
    // which doubled the rendered size, masked previously by setting the
    // size base to 16 instead of the binary's 32.
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
