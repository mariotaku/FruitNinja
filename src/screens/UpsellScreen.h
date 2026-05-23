#ifndef FN_SCREENS_UPSELL_SCREEN_H
#define FN_SCREENS_UPSELL_SCREEN_H

// Defunct: UpsellScreen — purchase prompt UI (irrelevant without IAP).
// Binary ctor @ 0x00164814; sizeof 0x1EC.
// Port specific: immediately invokes onDone so callers' state machine advances.

#include "hud/HUDControl3d.h"
#include "engine/util/Delegate.h"
#include "engine/math/Vec3.h"
#include <cstdint>

class UpsellScreen : public HUDControl3d {
public:
    Mortar::Delegate0<void> m_OnDone;

    UpsellScreen(Mortar::Delegate0<void> onDone, int /*mode*/)
        : m_OnDone(onDone) {}

    ~UpsellScreen() override {}

private:
    // Pad to binary sizeof 0x1EC; HUDControl3d is 0x7C, Delegate<void()> is variable per platform.
    // Binary layout preserved via padding so any subclass offset math stays correct.
    static const int kBinarySize = 0x1EC;
    static const int kUsedSize   = sizeof(HUDControl3d) + sizeof(Mortar::Delegate0<void>);
    uint8_t pad[kBinarySize - kUsedSize];
};

// Defunct: UpsellScreenElement — sub-element of UpsellScreen.
// Binary ctor variants @ 0x00104c58, 0x001063bc, 0x00165b0c; sizeof opaque (~0x200).
class UpsellScreenElement {
public:
    UpsellScreenElement() {}
    UpsellScreenElement(const UpsellScreenElement&) {}
    ~UpsellScreenElement() {}

    void SetTexture(float, float, float, float, float, Vec3* /*pos*/, void* /*tex*/) {}
    void SetAngle(unsigned short /*angleIdx*/, float /*duration*/) {}
    // TODO: 0x00166ff2 / 0x0016700c (R1.2 popup-N gap) -- AddSound is empty;
    // binary's UpsellScreenElement::AddSound queues {name, startT, endT} into
    // m_Sounds vector. UpsellScreenElement::Update fires GameSound::SFXPlay
    // when elapsed >= startT. Round 2: add m_Sounds<USESound> vector + AddSound
    // body + Update SFX-fire loop. Loop in MakeMainUpsellScreen iterates 4x
    // queuing "popup-%i" + "popup-1" finale.
    void AddSound(const char* /*path*/, float /*t0*/, float /*t1*/) {}
    void ClearSounds() {}

private:
    uint8_t pad[0x200];
};

#endif // FN_SCREENS_UPSELL_SCREEN_H
