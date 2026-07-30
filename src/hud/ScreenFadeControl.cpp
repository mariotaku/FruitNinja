// ScreenFadeControl — full-screen alpha fade overlay.
// Binary: ctor C1 @0x001aefd4 / C2 @0x001af0c0 (identical bodies), sizeof 0xB8.

#include "ScreenFadeControl.h"
#include "hud/HUDLayer.h"
#include "Game.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "math/Matrix44.h"
#include "render/gl_funcs.h"

// Binary: ctor C1 @0x001aefd4 / C2 @0x001af0c0
ScreenFadeControl::ScreenFadeControl()
    : m_bVisible(0),
      m_bAnimating(0),
      m_bStaysVisibleAfter(1),
      _pad7F(0),
      m_Timer(0.0f),
      m_Duration(0.0f),
      m_Colour(),
      m_StartAlpha(0),
      m_FromAlpha(0),
      m_TargetAlpha(0),
      _pad8F(0)
{
    m_LayerFlags = Mortar::HUD_LAYER_FADE_MODAL;
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0015AA1C (re-analyst)
    // Binary loads literal "loading.tex" (same GOT offset as SpeedControl).
    // Runtime TextureManager::LoadLocalisedTexture may remap to the actual fade asset.
    m_FadeTexture = Mortar::TextureManager::LoadLocalisedTexture("loading.tex");
    size = _Vector3<float>(0.0f, 0.0f, 1.0f);
}

ScreenFadeControl::~ScreenFadeControl() {}

// Binary @ 0x1aebf4
void ScreenFadeControl::Init()
{
    Reset();
}

// Binary @ 0x1aec0c
void ScreenFadeControl::Reset()
{
    m_bAnimating = 0;
    m_Duration   = 0.0f;
    m_bVisible   = 0;
    m_Timer      = 0.0f;
}

// Binary @ 0x1aec80
void ScreenFadeControl::Update(float dt)
{
    if (!m_bAnimating) return;

    m_Timer += dt;
    if (m_Timer > m_Duration) {
        OnFadeComplete();
        return;
    }

    float t = m_Timer / m_Duration;
    float a = (float)m_FromAlpha + ((int)m_TargetAlpha - (int)m_FromAlpha) * t;
    m_Colour.a = (a > 0.0f) ? (uint8_t)(int)a : 0;
}

// Binary @ 0x1aed74
void ScreenFadeControl::Draw(float* hudScaleRaw)
{
    (void)hudScaleRaw;

    if (!m_bVisible) return;

    // DIFFERS: original = unguarded `(*m_FadeTexture->vtbl[0xc])()` (v1.6.1
    // ScreenFadeControl::Draw @0x001aed74 has no SmartPtr validity test), using an
    // early-out because the port can reach Draw before the fade texture loads.
    Mortar::Texture* tex = m_FadeTexture.Get();
    if (!tex) return;

    tex->Set();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();

    Matrix44 mat = Matrix44::MakeScale(size.x, size.y, size.z);
    mat.GlobalTranslate44(pos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    // Binary: Mortar::Mesh::DrawQuadUnCached(m_Colour, ...)
    Game::GetInstance()->renderer.DrawQuad(m_Colour, 0.0f, 1.0f, 0.0f, 1.0f);

    tex->UnSet();
}

// Binary @ 0x1aec30
bool ScreenFadeControl::SetToMultiplayerState()
{
    Reset();
    return false;
}

// Binary @ 0x001aece0
// DIFFERS: binary's Colour const& param is a dead-store -- only m_Colour.a (m_StartAlpha)
// is ever touched; m_Colour stays at ctor-default (opaque BLACK, Colour::Colour() @0x0011afa8),
// so the fade is alpha-only over a black quad, not the caller-supplied tint.
void ScreenFadeControl::StartFade(bool inOrOut, float duration, const Colour& color,
                                   Mortar::Delegate0<void> onComplete)
{
    (void)color;  // dead-store per binary — colour parameter is not applied to m_Colour

    m_OnComplete = onComplete;
    m_Timer      = 0.0f;
    m_Duration   = duration;

    if (inOrOut) {
        m_FromAlpha   = 0;
        m_StartAlpha  = 0;
        m_TargetAlpha = 0xFF;
    } else {
        m_FromAlpha   = 0xFF;
        m_StartAlpha  = 0xFF;
        m_TargetAlpha = 0;
    }

    m_bStaysVisibleAfter = inOrOut ? 1 : 0;
    m_bVisible           = 1;
    m_Colour.a           = m_StartAlpha;
    m_bAnimating         = 1;
}

// Binary @ 0x1aec4c
void ScreenFadeControl::CancelFade()
{
    m_bVisible   = 0;
    m_bAnimating = 0;
}

// Binary: private helper called from Update @0x1aec80; no separate v1.6.1 address confirmed
void ScreenFadeControl::OnFadeComplete()
{
    m_Colour.a = m_TargetAlpha;
    if (!m_bStaysVisibleAfter) m_bVisible = 0;
    m_bAnimating = 0;
    m_OnComplete();
}

// ASM-spec v1.6.1 DefaultScreenFadeCompleteCallback @0x1aec48: empty no-op.
void DefaultScreenFadeCompleteCallback() {}
