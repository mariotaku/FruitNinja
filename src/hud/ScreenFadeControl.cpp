// Analysed: 2026-05-04T00:00
// ScreenFadeControl — full-screen alpha fade overlay.
// Binary: ctor @ 0x0015AA1C, sizeof 0xB8.

#include "ScreenFadeControl.h"
#include "Game.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "math/Matrix44.h"
#include "render/gl_funcs.h"

// Binary @ 0x0015AA1C
ScreenFadeControl::ScreenFadeControl()
    : m_bVisible(0),
      m_bAnimating(0),
      m_bStaysVisibleAfter(1),
      _pad7F(0),
      m_Timer(0.0f),
      m_Duration(0.0f),
      m_Colour(255, 255, 255, 255),
      m_StartAlpha(0),
      m_FromAlpha(0),
      m_TargetAlpha(0),
      _pad8F(0)
{
    m_LayerFlags = 0x400;
    // TODO: 0x0015AA1C -- resolve fade-texture name from GOT[0x15aacc]
    m_FadeTexture = Mortar::TextureManager::LoadLocalisedTexture("fade_overlay.tex");
    size = Vec3(0.0f, 0.0f, 1.0f);
}

ScreenFadeControl::~ScreenFadeControl() {}

// Binary @ 0x0015A724
void ScreenFadeControl::Init()
{
    Reset();
}

// Binary @ 0x0015A734
void ScreenFadeControl::Reset()
{
    m_bAnimating = 0;
    m_Duration   = 0.0f;
    m_bVisible   = 0;
    m_Timer      = 0.0f;
}

// Binary @ 0x0015A798
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

// Binary @ 0x0015A868
void ScreenFadeControl::Draw(const Vec3& hudScale, int layerMask)
{
    (void)hudScale;
    (void)layerMask;

    if (!m_bVisible) return;

    Mortar::Texture* tex = m_FadeTexture.Get();
    if (!tex) return;

    Game* game = Game::GetInstance();
    if (!game) return;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex->m_TexId);

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();

    Matrix44 mat = Matrix44::MakeScale(size.x, size.y, size.z);
    mat.GlobalTranslate44(pos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    // Binary: Mortar::Mesh::DrawQuadUnCached(m_Colour, ...)
    game->renderer.DrawQuad(m_Colour, 0.0f, 0.0f, 1.0f, 1.0f);

    glBindTexture(GL_TEXTURE_2D, 0);
}

// Binary @ 0x0015A754
bool ScreenFadeControl::SetToMultiplayerState()
{
    Reset();
    return false;
}

// Binary @ 0x0015A7F0
// DIFFERS: binary's Colour const& param is a dead-store -- m_Colour stays at ctor-default (white); fade is alpha-only
void ScreenFadeControl::StartFade(bool inOrOut, float duration, const Colour& color,
                                   Mortar::Delegate<void()> onComplete)
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

// Binary @ 0x0015A764
void ScreenFadeControl::CancelFade()
{
    m_bVisible   = 0;
    m_bAnimating = 0;
}

// Binary @ 0x0015A770
void ScreenFadeControl::OnFadeComplete()
{
    m_Colour.a = m_TargetAlpha;
    if (!m_bStaysVisibleAfter) m_bVisible = 0;
    m_bAnimating = 0;
    m_OnComplete();
}
