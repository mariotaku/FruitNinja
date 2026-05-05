// Analysed: 2026-05-03T00:00
#include "MissControl.h"
#include "HUD.h"
#include "asset/TextureManager.h"
#include "Game.h"
#include "math/Matrix44.h"
#include "math/MathUtil.h"
#include "render/MatrixManager.h"
#include "render/Renderer.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/gl_funcs.h"
#include <cstdio>
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

// Pulse banding thresholds. DAT_001522a4..DAT_001522b4
static constexpr float MISS_PULSE_FLOOR       = 0.65f;    // DAT_001522b4 (OK)
static constexpr float MISS_PULSE_PHASE_LO    = 16380.0f; // DAT_001522a4 (OK)
static constexpr float MISS_PULSE_PHASE_HI    = 376740.0f; // DAT_001522a8. DIFFERS: was 376354.0
static constexpr float MISS_PULSE_NARROW_LO   = 32760.0f;  // DAT_001522ac. DIFFERS: was 32764.0
static constexpr float MISS_PULSE_NARROW_HI   = 360360.0f; // DAT_001522b0. DIFFERS: was 360104.0

// --- Static members -------------------------------------------------------

int   MissControl::s_NumCriticals = 0;
float MissControl::s_DtMod        = 0.5f;  // (float)0 + 0.5 initial

// Binary @ 0x001515a4 — combo overlay textures [0..9] = combo_2..combo_11.
Mortar::SmartPtr<Mortar::Texture> MissControl::s_ComboTextures[10];

// --- ctor / dtor -----------------------------------------------------------

MissControl::MissControl()
    : m_bBusy(0)
    , m_AnimState(0)
    , m_bVisible(0)
    , m_JitterTimer(0)
    , m_FadeAlpha(0.0f)
    , m_bComboActive(0)
    , m_bUseSound(0)
    , m_ComboCount(0)
    , m_AlphaScale(1.0f)
{
    m_bActive       = 1;
    m_bNoDestructor = 1;
    // binary Init writes field_0x34 = 1 ("configured" flag), NOT 0x200.
    m_LayerFlags    = 1;
}

MissControl::~MissControl() = default;

// --- vtable overrides -------------------------------------------------------

// Binary @ 0x001513cc — vtable[5]. Drops m_Texture SmartPtr ref.
// Port stores raw GLuint; ownership lives in TextureManager, so clearing to 0 is sufficient.
void MissControl::Release() {
    m_Texture = 0;
}

// vtable[4] @ 0x00150fa4
void MissControl::Init() {
    m_bComboActive = 0;
    m_bBusy        = 1;
    m_Timer        = 0.0f;  // rotation (+0x2c)
    m_AnimState    = 0;
    m_LayerFlags   = 1;     // "configured" flag; DIFFERS: was 0x200
    m_FadeAlpha    = 0.0f;
    m_bBusy        = 1;     // binary writes twice (first overwritten by ctor)
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
        m_bBusy        = 0;
        m_DrawColour.a = 0;
    }
}

// Binary @ 0x00150e00 — vtable[8]. No-op shadow of HUDControl::PreDraw base.
void MissControl::PreDraw(const Vec3& /*hudScale*/) {}

// Binary @ 0x00150dfc — vtable[16]. Defunct: same-screen MP player-index hook.
// Defunct: same-screen MP player-index hook — no-op stub; binary @ 0x00150dfc
int MissControl::SetPlayer(int player) {
    return player;
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
    // Binary @ 0x001515a4 — combo textures. Binary ctor loop iVar3=1..10, loads
    // combo_%d.tex for iVar3>=3 -> names combo_3..combo_12 (10 entries). The MakeCombo
    // index mapping uses (comboCount-1) clamped [1..9] -> array indices [0..9].
    // TODO: 0x001515a4 -- runtime confirms combo_11.tex absent; verify if scheme is
    //   combo_2..combo_10 or combo_3..combo_11. asset-side investigation pending.
    for (int i = 0; i < 10; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "combo_%d.tex", i + 2);
        s_ComboTextures[i] = Mortar::TextureManager::LoadLocalisedTexture(name);
    }
    s_TexturesLoaded = true;
    printf("[MissControl] LoadContent: critical=%d rare=%d cross=%d\n",
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
        printf("[MissControl] AllocatePool: HUD not ready\n");
        return;
    }
    for (int i = 0; i < MISS_POOL_SIZE; ++i) {
        s_Pool[i] = new MissControl();
        game->hud->AddControl(s_Pool[i]);
    }
    s_PoolAllocated = true;
    printf("[MissControl] AllocatePool: %d slots\n", MISS_POOL_SIZE);
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
        if (s_Pool[idx] && s_Pool[idx]->m_bBusy == 0) break;
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
    mc->m_bActive    = 1;
    mc->m_bComboActive = 1;
    mc->m_bBusy      = 1;
    mc->m_JitterTimer = 0;   // field_0x7e = 0. binary @ 0x001518b8
    mc->m_DrawColour.a = 0xff;  // field_0x5f = 0xff. binary @ 0x001518b4
    mc->pos = pos;

    if (tex.IsValid()) {
        mc->m_Texture = tex->m_TexId;
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
// TODO: 0x001515a4 — gameMode==2 override: m_ComboCount = (int)(WaveManager::GetSpeed(0)+0.65f)
//   requires GameTaskState gameMode to be plumbed.
void MissControl::MakeCombo(Vec3 pos, int comboCount, int /*entityType*/) {
    Init();
    int idx = comboCount - 2;
    if (idx < 0)  idx = 0;
    if (idx > 9)  idx = 9;
    if (s_ComboTextures[idx].IsValid()) {
        m_Texture = s_ComboTextures[idx]->m_TexId;
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
        m_Texture      = tex->m_TexId;
        m_bVisible     = 1;
        m_AnimState    = 3;
        // path 1: zen-bomb X overlay (DAT_00151f40 = 1.811f, same as MISS_FADE_INIT).
        m_FadeAlpha    = MISS_FADE_INIT;
        m_JitterTimer  = 0;
        m_bComboActive = 1;
        size = Vec3((float)(tex->m_Width + 1), (float)(tex->m_Height + 1), 0.0f);
        m_bBusy        = 1;
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
        m_bBusy        = 1;
        if (s_TexCross.IsValid()) m_Texture = s_TexCross->m_TexId;
    }
}

// --- Update ----------------------------------------------------------------

// binary @ 0x00151a60
void MissControl::Update(float dt) {
    if (!m_bBusy) return;

    // Visibility lazy-on: if not yet visible and anim starting (animState < 1 = single-player)
    // binary @ 0x00151a60 lines 1-10
    if (!m_bVisible && m_AnimState < 1) {
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
            if (!other || other == this || !other->m_bBusy) continue;
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

    if (m_FadeAlpha <= 0.0f) return;

    // Pause guard: if game paused, skip fade. binary @ 0x00151a60 pause guard
    Game* game = Game::GetInstance();
    if (game && game->pauseFlag) return;

    pos.z = 0.0f;

    bool wasAboveThresh = (m_FadeAlpha >= SOUND_THRESH);
    m_FadeAlpha -= dt;

    // Sound trigger on 1.66 crossing. binary @ 0x00151a60 sound block
    if (wasAboveThresh && m_FadeAlpha < SOUND_THRESH && m_bComboActive) {
        // TODO: full SFX logic (field_0x85, ItemManager::PlayAlternateComboSound, etc.)
        // binary @ 0x00151a60 SFX path; requires GameSound::SFXPlay wiring.
    }

    // Slot release when fully faded. binary @ 0x00151a60 release block
    if (m_FadeAlpha <= 0.0f) {
        m_FadeAlpha = 0.0f;
        if (m_RemoveCallback) m_RemoveCallback(this);
        m_bBusy = 0;
    }
}

// --- Draw ------------------------------------------------------------------

// ASM-verified-partial: 2026-05-03 binary @ 0x00151f60..0x00152190 (pulse formula + fall-off only; transform field_0x14/0x20 pre-mult still a gap)
// binary @ 0x00151f60
void MissControl::Draw(const Vec3& /*hudScale*/, int /*layerMask*/) {
    if (m_Texture == 0) return;

    // Jitter: add random offset if jitter counter > 0. binary @ 0x00151f60 jitter block
    Vec3 drawPos = pos;
    if (m_JitterTimer > 0) {
        drawPos.x += (float)(rand() % 8 - 4);
        drawPos.y += (float)(rand() % 8 - 4);
        m_JitterTimer--;
    }

    // Two paths based on m_FadeAlpha. binary @ 0x00151f60 alpha gate
    float drawAlpha;
    if (m_FadeAlpha <= 0.0f) {
        // binary @ 0x00152272..0x00152286: fall-off uses this->pos.y (unjittered entity field).
        // IsMultiplayer: stub false (same-screen MP not yet ported). binary @ WaveManager.
        // FailureEnabled: stub false (game-mode flag not yet ported). binary @ Game.
        if (false /* FailureEnabled() */ && !false /* IsMultiplayer() */) {
            // binary @ 0x001520a2: drawPos.y -= 3.0 * pos.y * abs(game->field_0xc)
            // TODO: Game::field_0xc semantics undocumented; stub to 1.0 until ported
            drawPos.y -= 3.0f * pos.y * fabsf(/*game->field_0xc*/ 1.0f);
        } else {
            // binary @ 0x00152272: single VMLA — no +1.0 constant; uses pos.y not drawPos.y
            drawPos.y += -3.0f * pos.y;
        }
        drawAlpha = 0.0f;
    } else {
        if (m_FadeAlpha > SOUND_THRESH) return;  // early-return: spawning phase, no draw yet
        // binary @ 0x00151ff2..0x0015201a: four-op VFP chain preserving float32 rounding.
        //   vdiv s14, fade, 1.66    s14 = fade / 1.66
        //   vmul s14, s14, 360       s14 *= 360
        //   vmul s14, s14, 6         s14 *= 6
        //   vmul s14, s14, 182       s14 *= 182
        // Net: phase = (fade / 1.66) * 360 * 6 * 182  ~= fade * 236819.28
        // pulse = |SinIdx((uint16_t)(uint32_t)max(phase,0))| with banded 0.65 floor.
        float phase = (m_FadeAlpha / 1.66f) * 360.0f * 6.0f * 182.0f;
        uint16_t idx = (uint16_t)(uint32_t)std::max(phase, 0.0f);
        float pulse = fabsf(SinIdx(idx));
        if (phase > MISS_PULSE_PHASE_LO && phase < MISS_PULSE_PHASE_HI) {
            if (phase < MISS_PULSE_NARROW_LO || phase > MISS_PULSE_NARROW_HI) {
                if (pulse < MISS_PULSE_FLOOR) pulse = MISS_PULSE_FLOOR;
            } else {
                pulse = MISS_PULSE_FLOOR;
            }
        }
        drawAlpha = m_FadeAlpha * pulse;
    }

    // Effective alpha: fade * AlphaScale, then clamp [0..1].
    float fade = drawAlpha * m_AlphaScale;
    if (fade <= 0.0f) return;
    if (fade > 1.0f) fade = 1.0f;

    // UV crop based on m_bComboActive / m_bVisible. binary @ 0x00151f60 UV block
    float u0, v0, du, dv;
    if (m_bComboActive) {
        // Full UV quad. binary combo-active path.
        u0 = 0.0f; v0 = 0.0f; du = 1.0f; dv = 1.0f;
    } else if (!m_bVisible) {
        // 25%-wide vertical crop left. binary: (u0=0, v0=0.5, du=0.25, dv=0.75)
        u0 = 0.0f; v0 = 0.5f; du = 0.25f; dv = 0.75f;
    } else {
        // 25%-wide vertical crop right. binary: (u0=0.5, v0=0, du=0.25, dv=0.75)
        u0 = 0.5f; v0 = 0.0f; du = 0.25f; dv = 0.75f;
    }

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();

    // binary @ 0x001520ec / 0x000fc720 / 0x000f7a4c. Order: Scale -> RotZ -> Translate.
    // TODO: HUDControl3d field_0x14 / field_0x20 pre-multiplications (anchor + local-scale)
    // are missing here. Requires RE confirmation of base-class field port-side names.
    Matrix44 mat = Matrix44::MakeScale(size.x, size.y, 1.0f);
    if (m_Timer != 0.0f) {
        uint16_t a = (uint16_t)(int)(m_Timer * 182.0f);
        mat.RotZ44(SinIdx(a), CosIdx(a));
    }
    // Port ortho already centers on (0,0); no +480/+320 offset needed here.
    mat.GlobalTranslate44(drawPos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    glBindTexture(GL_TEXTURE_2D, m_Texture);

    // Tint: m_DrawColour multiplied by per-frame HUD tint (MatrixManager.field_0x3c.field_0x20).
    // TODO: fetch HUD tint multiplier from MatrixManager for exact binary match.
    const uint8_t a = (uint8_t)(fade * (float)m_DrawColour.a);
    const uint32_t col = (uint32_t)a << 24 | (uint32_t)m_DrawColour.b << 16
                        | (uint32_t)m_DrawColour.g << 8 | (uint32_t)m_DrawColour.r;

    QUADCUSTOMVERTEX v[6];
    std::memset(v, 0, sizeof(v));
    // Centred quad in [-1..+1]. Matrix applies size scale + translate.
    const float u1 = u0 + du;
    const float v1 = v0 + dv;
    v[0].x = -1.0f; v[0].y = -1.0f; v[0].u = u0; v[0].v = v1; v[0].colour = col;
    v[1].x =  1.0f; v[1].y = -1.0f; v[1].u = u1; v[1].v = v1; v[1].colour = col;
    v[2].x = -1.0f; v[2].y =  1.0f; v[2].u = u0; v[2].v = v0; v[2].colour = col;
    v[3].x =  1.0f; v[3].y = -1.0f; v[3].u = u1; v[3].v = v1; v[3].colour = col;
    v[4].x =  1.0f; v[4].y =  1.0f; v[4].u = u1; v[4].v = v0; v[4].colour = col;
    v[5].x = -1.0f; v[5].y =  1.0f; v[5].u = u0; v[5].v = v0; v[5].colour = col;

    if (Renderer* r = Renderer::GetInstance()) {
        r->DrawTriList(v, 6);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}
