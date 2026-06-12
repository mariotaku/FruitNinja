#ifndef FN_SUPER_FRUIT_GLOW_H
#define FN_SUPER_FRUIT_GLOW_H

// SuperFruitGlow — glow halo entity attached to the host Fruit during super-fruit
// state. Entity-type 6 subclass.
//
// Binary sizes: ctor @ 0x001c06bc, Update @ 0x001c0024,
//               FruitWasKilled @ 0x001bee48, DrawOrder @ 0x001bfb18,
//               Release @ 0x001c01c8.
//
// Binary sizeof(SuperFruitGlow) confirmed via operator new() = 0x8c bytes.
// Field layout (RE spec):
//   +0x00..+0x3b: Mortar::Entity base (0x3c bytes ARM32)
//   +0x3c..+0x7b: Mortar::Entity fields used by subclass (gap)
//   +0x7c: bool m_bFruitKilled  (fruit has been killed -- glow should self-release)
//   +0x80: Fruit* m_pHostFruit  (host fruit pointer; cleared by FruitWasKilled)
//   +0x84: Mortar::FancyBakedString* m_pText  (combo/score text overlay)
//   +0x88: float m_GlowAlpha    (current glow alpha; animated toward target)

#include "Entity.h"

class Fruit;
struct Renderer;
namespace Mortar { class FancyBakedString; }

class SuperFruitGlow : public Mortar::Entity {
public:
    // +0x3c..+0x7b: base + padding to match binary 0x8c layout
    // (0x8c - 0x3c = 0x50 = 80 bytes of own fields; fields enumerated below)
    uint8_t _pad_own[64];    // +0x3c..+0x7b (load-bearing layout padding; binary fields unresolved)

    // +0x7c: set to true by FruitWasKilled; glow enters self-release
    uint8_t m_bFruitKilled;  // +0x7c

    uint8_t _pad_7d[3];

    // +0x80: back-pointer to host fruit; cleared to nullptr by FruitWasKilled
    Fruit* m_pHostFruit;     // +0x80

    // +0x84: combo/score text overlay (FancyBakedString)
    Mortar::FancyBakedString* m_pText;    // +0x84

    // +0x88: glow alpha [0..1]; animated toward target each Update
    float m_GlowAlpha;                    // +0x88

    // Binary @ 0x001c06bc
    SuperFruitGlow();
    ~SuperFruitGlow();

    // Entity vtable overrides
    void Update(float dt) override;       // 0x001c0024
    void Draw(Renderer& r) override;      // TODO: not yet RE'd
    void PostUpdate(float dt) override;   // TODO: not yet RE'd
    void Release() override;              // 0x001c01c8

    // Binary @ 0x001bfb18. Returns draw-order Z value for this glow.
    // Called from ActorManager draw-sort path.
    float DrawOrder() const;

    // Binary @ 0x001bee48. Called when the host fruit is killed (via Fruit kill delegate).
    // Clears m_pHostFruit if it matches fruit, sets m_bFruitKilled=true.
    void FruitWasKilled(Fruit* fruit);
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(SuperFruitGlow) == 0x8c, "SuperFruitGlow binary sizeof must be 0x8c");
static_assert(offsetof(SuperFruitGlow, m_bFruitKilled) == 0x7c, "SuperFruitGlow::m_bFruitKilled offset");
static_assert(offsetof(SuperFruitGlow, m_pHostFruit)   == 0x80, "SuperFruitGlow::m_pHostFruit offset");
static_assert(offsetof(SuperFruitGlow, m_pText)        == 0x84, "SuperFruitGlow::m_pText offset");
static_assert(offsetof(SuperFruitGlow, m_GlowAlpha)    == 0x88, "SuperFruitGlow::m_GlowAlpha offset");
#endif

#endif // FN_SUPER_FRUIT_GLOW_H
