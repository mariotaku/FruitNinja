#ifndef FN_GAME_MODIFIER_H
#define FN_GAME_MODIFIER_H

//
// GameModifier : abstract base for ScoreModifier / TimeModifier /
// SlashModifier / WaveModifier. Binary size 0x3c bytes.
//
// Binary addresses:
//   ctor            0x0011a160
//   dtor            0x0011a19c
//   RemoveModifier  0x0011a1b8  (no-op in base, subclasses may override)
//   ApplyModifier   0x00118178  (base — writes m_RemainingTime)
//   vtable slot 0xc = UpdateSpecific (float)
//   vtable slot 0x14 = RemoveModifier
//   vtable slot 0x18 = ParseSpecific(TiXmlElement*)
//
// Port status: STUB — none of the PowerUpManager/PowerUp/GameModifier
// lifecycle is wired yet. Declared so SlashModifier has a base to inherit.
// Without PowerUpManager calling UpdateSpecific each frame, no modifier
// has runtime effect. See docs/functions/power-ups.md.
//

#include <cstdint>

class PowerUp;

class GameModifier {
public:
    // +0x00: vtable

    // +0x04: XML-declared duration of this modifier (seconds). Set from
    // <...modifier duration="..."/>.
    float m_BaseDuration;

    uint32_t field_0x08;     // +0x08: padding/unknown, zeroed in ctor

    // +0x0c: seconds remaining on this modifier. Decremented each tick by
    // PowerUp::Update. When <= 0 the PowerUp deactivates.
    float m_RemainingTime;

    // +0x10: set when the modifier was allocated via `new` (vs. stack or
    // static). Destructor guards heap-free on this.
    bool  m_AllocatedByNew;

    uint8_t field_0x11;
    uint8_t field_0x12;
    uint8_t field_0x13;

    // +0x14: cooldown / start marker. Initialised to -1.0f.
    float m_field14;

    // +0x18: active flag (unclear separation from m_RemainingTime > 0).
    bool  m_fieldActive;
    uint8_t field_0x19;
    uint8_t field_0x1a;
    uint8_t field_0x1b;

    // +0x1c: back-pointer to the PowerUp that owns this modifier.
    PowerUp* m_pOwner;

    // +0x20..+0x3b: subclass fields (28 bytes). Each subclass uses its own.
    uint8_t subclass_fields[0x1c];

    GameModifier()
        : m_BaseDuration(0.0f), field_0x08(0), m_RemainingTime(0.0f),
          m_AllocatedByNew(false), field_0x11(0), field_0x12(0), field_0x13(0),
          m_field14(-1.0f), m_fieldActive(false),
          field_0x19(0), field_0x1a(0), field_0x1b(0),
          m_pOwner(nullptr) {
        for (auto& b : subclass_fields) b = 0;
    }

    virtual ~GameModifier() {}

    // Empty in base. Subclasses override for per-frame dispatch (the
    // mechanism PowerUpManager drives modifiers by).
    virtual void UpdateSpecific(float /*dt*/) {}

    // Called by PowerUpManager::ApplyModifier once when the modifier
    // activates. Base writes m_RemainingTime = m_BaseDuration.
    virtual void ApplyModifier() { m_RemainingTime = m_BaseDuration; }

    // Called when the parent PowerUp deactivates. Base is a no-op.
    virtual void RemoveModifier() {}

    // Called by PowerUp::Parse from XML. Subclasses override to read
    // their type-specific attributes.
    virtual void ParseSpecific(void* /*xml*/) {}
};

#endif
