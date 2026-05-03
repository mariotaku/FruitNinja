#ifndef FN_GAME_SCREEN_EFFECT_H
#define FN_GAME_SCREEN_EFFECT_H

// Analysed: 2026-05-03T00:00
//
// ScreenEffect — per-powerup full-screen visual overlay (0x50 bytes in binary).
// Stubs all methods; full port is Tier-2.
//
// Binary addresses (from re-analyst PowerUp lifecycle pass):
//   Parse         (binary @ 0x? -- re-analyst pass needed)
//   Activate      (binary @ 0x? -- re-analyst pass needed)
//   Update        (binary @ 0x? -- re-analyst pass needed)
//   Deactivate    (binary @ 0x? -- re-analyst pass needed)
//   LoadTextures  (binary @ 0x? -- re-analyst pass needed)

#include <cstdint>
#include <tinyxml2.h>

class PowerUp;

class ScreenEffect {
public:
    // Back-pointer to owning PowerUp (m_pOwner in binary).
    PowerUp* m_pOwner;

    // Unique name hash for pool lookup.
    uint32_t m_NameHash;

    // Remaining lifetime — decremented by Update; expiry when <= 0.
    // TODO: binary uses a different expiry mechanism (re-analyst pass needed)
    float m_Lifetime;

    ScreenEffect() : m_pOwner(nullptr), m_NameHash(0), m_Lifetime(0.0f) {}

    // TODO: implement (binary @ 0x? -- re-analyst pass needed)
    void Parse(tinyxml2::XMLElement* /*xml*/) {}

    // TODO: implement (binary @ 0x? -- re-analyst pass needed)
    void Activate() {}

    // TODO: implement (binary @ 0x? -- re-analyst pass needed)
    void Update(float dt, float /*longest*/, float /*total*/) { m_Lifetime -= dt; }

    // TODO: implement (binary @ 0x? -- re-analyst pass needed)
    void Deactivate() {}

    // TODO: implement (binary @ 0x? -- re-analyst pass needed)
    void LoadTextures() {}
};

#endif // FN_GAME_SCREEN_EFFECT_H
