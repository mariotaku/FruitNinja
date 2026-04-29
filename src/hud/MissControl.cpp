#include "MissControl.h"
#include "HUD.h"
#include "asset/TextureManager.h"
#include "Game.h"
#include "math/Matrix44.h"
#include "render/MatrixManager.h"
#include "render/Renderer.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/gl_funcs.h"
#include <cstdio>
#include <cstring>

// --- Shared pool + texture state -----------------------------------------
//
// Mirrors the binary's static ref-counted singleton state accessed by all
// MissControl instances. The pool is 9 slots laid out as a flat array;
// GetFree round-robins over it.

static constexpr int MISS_POOL_SIZE = 9;

// Round-robin index used by GetFree. Wraps at MISS_POOL_SIZE.
static int s_NextSlot = 0;

// Pool instances. Static storage so destruction order doesn't matter —
// matches the binary's global-cache pattern. Access through GetFree.
static MissControl* s_Pool[MISS_POOL_SIZE] = { nullptr };
static bool s_PoolAllocated = false;

// Shared textures (load-once across all pool instances). SmartPtr keeps
// them alive for the lifetime of the pool.
static SmartPtr<Mortar::Texture> s_TexCritical;     // critical.tex
static SmartPtr<Mortar::Texture> s_TexRare;         // ultra_rare_plus_50.tex
static SmartPtr<Mortar::Texture> s_TexCross;        // hud_cross.tex
static bool s_TexturesLoaded = false;

// MakeCritical / MakeRare fade magnitude. Binary DAT_001518b8 = 0.808.
static constexpr float MISS_FADE_INIT = 0.808f;

// Screen clamp rectangle for label position (centred ortho).
static constexpr float CLAMP_X_HI =  240.0f;   // DAT_001518c0
static constexpr float CLAMP_X_LO = -240.0f;   // DAT_001518c8
static constexpr float CLAMP_Y_HI =  160.0f;   // DAT_001518c4
static constexpr float CLAMP_Y_LO = -160.0f;   // DAT_001518cc

// Fade rate — binary runs an anim-state machine (m_AnimState == 3) with
// its own curve. Port approximates via a simple per-frame exponential
// decay. Tuned to give a ~1.2s fade at 60fps (0.808 * 0.95^72 ~= 0.02).
static constexpr float FADE_DECAY = 0.95f;
static constexpr float FADE_CLEAR = 0.02f;

// --- ctor / dtor ---------------------------------------------------------

MissControl::MissControl()
    : m_bBusy(0), m_bComboActive(0),
      m_FadeAlpha(0.0f), m_AnimState(0), m_AlphaScale(1.0f) {
    m_bActive = 1;         // HUD-level active flag
    m_bNoDestructor = 1;   // pool owns us; HUD shouldn't free on removal
    m_LayerFlags = 0x200;  // draw alongside bomb-hit overlay layer
}

MissControl::~MissControl() = default;

// --- Shared texture load -------------------------------------------------

void MissControl::LoadContent() {
    if (s_TexturesLoaded) return;
    s_TexCritical = Mortar::TextureManager::LoadLocalisedTexture("critical.tex");
    s_TexRare     = Mortar::TextureManager::LoadLocalisedTexture("ultra_rare_plus_50.tex");
    s_TexCross    = Mortar::TextureManager::LoadLocalisedTexture("hud_cross.tex");
    s_TexturesLoaded = true;
    printf("[MissControl] LoadContent: critical=%d rare=%d cross=%d\n",
           s_TexCritical.IsValid(), s_TexRare.IsValid(), s_TexCross.IsValid());
    // Combo textures (combo_%d.tex for indices 3..10) are not loaded
    // yet — combo indicator overlay is deferred.
}

// --- Pool allocation -----------------------------------------------------

void MissControl::AllocatePool() {
    if (s_PoolAllocated) return;
    Game* game = Game::GetInstance();
    if (!game || !game->hud) {
        printf("[MissControl] AllocatePool: HUD not ready; skipping\n");
        return;
    }
    for (int i = 0; i < MISS_POOL_SIZE; ++i) {
        s_Pool[i] = new MissControl();
        game->hud->AddControl(s_Pool[i]);
    }
    s_PoolAllocated = true;
    printf("[MissControl] AllocatePool: %d slots registered with HUD\n",
           MISS_POOL_SIZE);
}

// --- GetFree -------------------------------------------------------------

// Matches MissControl::GetFree (0x00150da4). Round-robin search from
// s_NextSlot; return the first non-busy slot. If all slots are busy,
// the loop bails after MISS_POOL_SIZE iterations and returns the
// current s_NextSlot entry (caller will overwrite — binary does the
// same: the oldest label gets pre-empted).
MissControl* MissControl::GetFree() {
    if (!s_PoolAllocated) return nullptr;
    int idx = s_NextSlot;
    for (int tries = 0; tries < MISS_POOL_SIZE; ++tries) {
        if (s_Pool[idx] && s_Pool[idx]->m_bBusy == 0) break;
        idx = (idx + 1) % MISS_POOL_SIZE;
    }
    s_NextSlot = (idx + 1) % MISS_POOL_SIZE;
    return s_Pool[idx];
}

// --- Make* ---------------------------------------------------------------

// Shared core of MakeCritical / MakeRare / MakeDisappear. Sets position,
// size (from the supplied texture's dimensions), fade alpha + anim state,
// and screen-clamps pos.
static void PopulateOverlay(MissControl* mc, const Vec3& pos,
                            const SmartPtr<Mortar::Texture>& tex,
                            float alphaScale) {
    mc->m_FadeAlpha  = MISS_FADE_INIT;
    mc->m_AnimState  = 3;
    mc->m_AlphaScale = alphaScale;
    mc->m_bActive    = 1;
    mc->m_bComboActive = 1;
    mc->m_bBusy = 1;
    mc->pos = pos;

    if (tex.IsValid()) {
        mc->m_Texture = tex->m_TexId;
        // Size from texture dimensions — MakeCritical reads width/height
        // via Texture vtable+0x14 / +0x18 (GetWidth / GetHeight); port
        // uses the same convention via m_Width / m_Height on Texture.
        const float w = (float)(tex->m_Width  + 1);
        const float h = (float)(tex->m_Height + 1);
        mc->size.x = w * 0.5f;
        mc->size.y = h * 0.5f;
        mc->size.z = 1.0f;
    }

    // Screen-clamp so the label fully fits on-screen. Matches binary's
    // four-way clamp on the centred ortho ±240 / ±160 bounds.
    if (mc->pos.x + mc->size.x >  CLAMP_X_HI) mc->pos.x =  CLAMP_X_HI - mc->size.x;
    if (mc->pos.y + mc->size.y >  CLAMP_Y_HI) mc->pos.y =  CLAMP_Y_HI - mc->size.y;
    if (mc->pos.x - mc->size.x <  CLAMP_X_LO) mc->pos.x =  CLAMP_X_LO + mc->size.x;
    if (mc->pos.y - mc->size.y <  CLAMP_Y_LO) mc->pos.y =  CLAMP_Y_LO + mc->size.y;
}

void MissControl::MakeCritical(const Vec3& pos, int /*playerIdx*/) {
    PopulateOverlay(this, pos, s_TexCritical, /*alphaScale*/ 1.0f);
}

void MissControl::MakeRare(const Vec3& pos) {
    PopulateOverlay(this, pos, s_TexRare, /*alphaScale*/ 0.5f);
}

void MissControl::MakeDisappear(const Vec3& pos, int /*sizeMult*/,
                                const SmartPtr<Mortar::Texture>& tex) {
    // Bomb zen-hit passes an invalid SmartPtr to mean "use the default
    // hud_cross overlay"; fruit miss-penalty passes the fruit's own
    // texture so the label shows which fruit was missed.
    const SmartPtr<Mortar::Texture>& pick =
        tex.IsValid() ? tex : s_TexCross;
    PopulateOverlay(this, pos, pick, /*alphaScale*/ 1.0f);
    // TODO: sizeMult (binary field_0x34 = 0x200 from Bomb::OnSliced)
    // scales the final quad width — currently baseline size only.
}

// --- Update --------------------------------------------------------------

void MissControl::Update(float /*dt*/) {
    if (m_bBusy == 0) return;
    m_FadeAlpha *= FADE_DECAY;
    if (m_FadeAlpha < FADE_CLEAR) {
        m_FadeAlpha = 0.0f;
        m_bBusy = 0;
        m_AnimState = 0;
        // Leave m_bActive at 1 so HUD keeps iterating; Draw no-ops on
        // !m_bBusy so the slot is effectively invisible until reused.
    }
}

// --- Draw ----------------------------------------------------------------

void MissControl::Draw(const Vec3& /*hudScale*/, int /*layerMask*/) {
    if (m_bBusy == 0 || m_Texture == 0) return;

    // Pick up effective alpha from fade × scale × control tint.
    float fade = m_FadeAlpha * m_AlphaScale;
    if (fade <= 0.0f) return;
    if (fade > 1.0f) fade = 1.0f;

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();

    Matrix44 mat;
    mat.ApplyScale(size.x, size.y, 1.0f);
    mat.GlobalTranslate44(pos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    glBindTexture(GL_TEXTURE_2D, m_Texture);

    const uint8_t a = (uint8_t)(fade * 255.0f);
    const uint32_t col = (uint32_t)a << 24 | 0x00FFFFFF;

    QUADCUSTOMVERTEX v[6];
    std::memset(v, 0, sizeof(v));
    // Centred unit quad — matrix applies pos + size scale.
    const float x0 = -1.0f, x1 = 1.0f, y0 = -1.0f, y1 = 1.0f;
    v[0].x = x0; v[0].y = y0; v[0].u = 0.0f; v[0].v = 1.0f; v[0].colour = col;
    v[1].x = x1; v[1].y = y0; v[1].u = 1.0f; v[1].v = 1.0f; v[1].colour = col;
    v[2].x = x0; v[2].y = y1; v[2].u = 0.0f; v[2].v = 0.0f; v[2].colour = col;
    v[3].x = x1; v[3].y = y0; v[3].u = 1.0f; v[3].v = 1.0f; v[3].colour = col;
    v[4].x = x1; v[4].y = y1; v[4].u = 1.0f; v[4].v = 0.0f; v[4].colour = col;
    v[5].x = x0; v[5].y = y1; v[5].u = 0.0f; v[5].v = 0.0f; v[5].colour = col;

    if (Renderer* r = Renderer::GetInstance()) {
        r->DrawTriList(v, 6);
    }
}
