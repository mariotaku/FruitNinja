// SuperFruitGlow — glow halo entity for the super-fruit (pomegranate).
// Binary: ctor @ 0x001c06bc, Update @ 0x001c0024, FruitWasKilled @ 0x001bee48,
//         DrawOrder @ 0x001bfb18, Release @ 0x001c01c8.
//
// VFX bodies (Update, DrawOrder, Draw) are stubbed with // TODO markers pending
// full RE of the glow animation curve and draw primitives.

#include "SuperFruitGlow.h"
#include "Fruit.h"
#include <cstring>

// Binary @ 0x001c06bc
SuperFruitGlow::SuperFruitGlow()
    : m_bFruitKilled(0)
    , m_pHostFruit(nullptr)
    , m_pText(nullptr)
    , m_GlowAlpha(0.0f)
{
    entityType = 6;  // super-fruit type in binary
    memset(_pad_own, 0, sizeof(_pad_own));
    memset(_pad_7d, 0, sizeof(_pad_7d));
}

SuperFruitGlow::~SuperFruitGlow()
{
    m_pHostFruit = nullptr;
}

// Binary @ 0x001c01c8
void SuperFruitGlow::Release()
{
    m_pHostFruit = nullptr;
    Mortar::Entity::Release();
}

// Binary @ 0x001c0024.
// Animates glow alpha/scale toward host fruit position each frame.
// TODO: 0x001c0024 -- full glow animation (alpha lerp, scale pulse) not yet ported
void SuperFruitGlow::Update(float /*dt*/)
{
    if (m_bFruitKilled) {
        // Self-release when host is gone
        flags |= ENT_KILLED;
    }
}

// TODO: 0x001bfb18 -- SuperFruitGlow::Draw not yet ported
void SuperFruitGlow::Draw(Renderer& /*r*/)
{
}

// TODO: 0x001bfb18 -- SuperFruitGlow::PostUpdate not yet ported
void SuperFruitGlow::PostUpdate(float /*dt*/)
{
}

// Binary @ 0x001bfb18. Returns z-depth draw order value.
// TODO: 0x001bfb18 -- DrawOrder value not yet RE'd; returning host z as fallback
float SuperFruitGlow::DrawOrder() const
{
    if (m_pHostFruit) {
        return m_pHostFruit->m_ZPosition;
    }
    return 0.0f;
}

// Binary @ 0x001bee48.
// If fruit matches m_pHostFruit: clear m_pHostFruit, set m_bFruitKilled=true.
void SuperFruitGlow::FruitWasKilled(Fruit* fruit)
{
    if (m_pHostFruit == fruit) {
        m_pHostFruit = nullptr;
        m_bFruitKilled = 1;
    }
}
