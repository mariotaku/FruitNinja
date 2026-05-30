#ifndef FN_SCREENS_UPSELL_SCREEN_H
#define FN_SCREENS_UPSELL_SCREEN_H

// Defunct: UpsellScreen -- purchase prompt UI (irrelevant without IAP).
// Polymorphic: derives from HUDControl3d (vtable from base).
// Binary ctor @ 0x00164814; sizeof = 0x1EC (492 bytes) on ARM32/Bada.
//
// Binary layout (ARM32 offsets):
//   +0x00..+0x7B: HUDControl3d base (124 = 0x7C bytes)
//   +0x7C:        field1_0x7c (HUDControlFns* / void*, set to 0)
//   +0x80:        field2_0x80 (uint32_t, set to 0)
//   +0x84:        m_OnDismiss_delegate (Delegate0<void>, 36 bytes, ends +0xA7)
//   +0xA8:        field4_0xa8 (float, set to 0.0f)
//   +0xAC..+0x1AB: opaque pad (256 bytes, not RE'd)
//   +0x1AC:       m_elements (std::list<UpsellScreenElement>, 8 bytes)
//   +0x1B4:       field269_0x1b4 (Texture*)
//   +0x1B8:       field270_0x1b8 (uint32_t)
//   +0x1BC:       field271_0x1bc_mode (int, ctor param_3)
//   +0x1C0:       field272_0x1c0 (float, 0.9f)
//   +0x1C4:       field273_0x1c4 (float, 2.4f)
//   +0x1C8:       field274_0x1c8 (float, 1.0f)
//   +0x1CC:       field275_0x1cc (float, 180.0f)
//   +0x1D0:       field276_0x1d0 (float/Colour, 11.0f)
//   +0x1D4:       field277_0x1d4 (float, 0.0f)
//   +0x1D8:       field278_0x1d8 (float, -1.0f)
//   +0x1DC:       field279_0x1dc (float, -1.0f)
//   +0x1E0:       field280_0x1e0 (Texture*, null)
//   +0x1E4:       field281_0x1e4 (Texture*, null)
//   +0x1E8:       field282_0x1e8 (Texture*, null)
//   Total: 0x1EC = 492 bytes

#include "hud/HUDControl3d.h"
#include "engine/util/Delegate.h"
#include "engine/math/Vec3.h"
#include <cstdint>
#include <list>

// Forward-declare UpsellScreenElement before UpsellScreen (used in m_elements list).
class UpsellScreenElement;

class UpsellScreen : public HUDControl3d {
public:
    // +0x7C (ARM32): void* placeholder for HUDControlFns* (set to 0 in ctor)
    void*                      field1_0x7c;

    // +0x80 (ARM32): opaque field, set to 0 in ctor
    uint32_t                   field2_0x80;

    // +0x84 (ARM32): on-dismiss delegate
    Mortar::Delegate0<void>    m_OnDismiss_delegate;

    // +0xA8 (ARM32): float field, set to 0.0f
    float                      field4_0xa8;

    // +0xAC..+0x1AB: opaque region (256 bytes) not yet RE'd
    uint8_t                    m_pad_ac[256];

    // +0x1AC (ARM32): list of sub-elements (8-byte Sourcery pre-C++11 list)
    std::list<UpsellScreenElement> m_elements;

    // +0x1B4..+0x1EB: individual fields (see header comment for offsets)
    void*    field269_0x1b4;   // Texture*
    uint32_t field270_0x1b8;
    int      field271_0x1bc_mode;
    float    field272_0x1c0;
    float    field273_0x1c4;
    float    field274_0x1c8;
    float    field275_0x1cc;
    float    field276_0x1d0;   // Ghidra typed as Colour; used as float (11.0f)
    float    field277_0x1d4;
    float    field278_0x1d8;
    float    field279_0x1dc;
    void*    field280_0x1e0;   // Texture*
    void*    field281_0x1e4;   // Texture*
    void*    field282_0x1e8;   // Texture*

    UpsellScreen(Mortar::Delegate0<void> onDone, int mode);
    ~UpsellScreen() override {}

    // Defunct: UpsellScreen monetization -- no-op stub; binary @ 0x00166d20
    static UpsellScreen* MakeMainUpsellScreen(Mortar::Delegate0<void> onDone);

    // Defunct: UpsellScreen monetization -- no-op stub; binary @ 0x00166708
    static UpsellScreen* MakeModeUpsellScreen(Mortar::Delegate0<void> onDone, int mode);
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(UpsellScreen) == 0x1EC,
    "UpsellScreen must be 492 bytes on ARM32/Bada");
#endif

// ----------------------------------------------------------------------------
// UpsellScreenElement -- value-type sub-element stored in std::list inside
// UpsellScreen. Defunct: UpsellScreen monetization.
//
// Binary ctors @ 0x00104c58 (parameterised), 0x00165c74 (default), 0x001679b8 (copy).
// Non-polymorphic. Binary sizeof = 0x39C (924 bytes).
//
// Known fields (from ctor disasm; offsets ARM32):
//   +0x00: _Vector3<float> m_Position
//   +0x0C: bool m_Active
//   +0x10: _Vector3<float> m_BasePosition
//   +0x1C: TranisitionInfo m_Transition0 (24B)
//   +0x34: PulseInfo m_Pulse0 (opaque)
//   +0x5C: TranisitionInfo m_Transition1 (24B)
//   +0x74: TranisitionInfo m_Transition2 (24B)
//   +0x8C: PulseInfo m_Pulse1 (opaque)
//   +0xB4: Colour m_Colour0 (4B)
//   +0xB8: _Vector3<float> m_Scale (12B)
//   +0xC8: PulseInfo m_Pulse2 (opaque)
//   +0xD4: Colour m_Colour1 (4B)
//   +0xF0: Mortar::SmartPtr<Mortar::Texture> m_Texture (4B)
//   +0xF4: Colour m_Colour2 (4B)
//   +0xF8: float m_Alpha
//   +0xFC: Colour m_Colour3 (4B)
//   +0x100: float m_Scalar
//   +0x104: _Vector3<float> m_Vec104
//   +0x110: TranisitionInfo m_Transition3 (24B)
//   +0x128: PulseInfo m_Pulse3 (opaque)
//   +0x150: std::list<USESound> m_Sounds (8B)
//   +0x158: USEColourEntry[16] m_ColourEntries (16 * 36B = 576B)
//   +0x398: int32_t m_Field398
// ----------------------------------------------------------------------------

class UpsellScreenElement {
public:
    // Defunct: UpsellScreen monetization -- no-op stub; binary @ 0x00165c74
    UpsellScreenElement() {}
    // Defunct: UpsellScreen monetization -- no-op stub; binary @ 0x001679b8
    UpsellScreenElement(const UpsellScreenElement&) {}
    // Defunct: UpsellScreen monetization -- dtor; binary @ 0x00166324
    ~UpsellScreenElement() {}

    // Defunct: UpsellScreen monetization -- no-op stub; binary @ 0x00104c58
    void SetTexture(float, float, float, float, float, Vec3* /*pos*/, void* /*tex*/) {}

    // Defunct: UpsellScreen monetization -- no-op stub
    void SetAngle(unsigned short /*angleIdx*/, float /*duration*/) {}

    // TODO: 0x00166ff2 / 0x0016700c (R1.2 popup-N gap) -- AddSound is empty;
    // binary's UpsellScreenElement::AddSound queues {name, startT, endT} into
    // m_Sounds vector. UpsellScreenElement::Update fires GameSound::SFXPlay
    // when elapsed >= startT. Round 2: add m_Sounds<USESound> vector + AddSound
    // body + Update SFX-fire loop. Loop in MakeMainUpsellScreen iterates 4x
    // queuing "popup-%i" + "popup-1" finale.
    void AddSound(const char* /*path*/, float /*t0*/, float /*t1*/) {}

    // Defunct: UpsellScreen monetization -- no-op stub
    void ClearSounds() {}

private:
    // Opaque pad to reach binary sizeof = 0x39C (924 bytes).
    // Internal field layout documented in header comment above.
    // Not split into named members because sub-struct types (TranisitionInfo,
    // PulseInfo, USEColourEntry, USESound) are not yet ported.
    uint8_t m_pad[924];
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(UpsellScreenElement) == 0x39C,
    "UpsellScreenElement must be 924 bytes on ARM32/Bada");
#endif

#endif // FN_SCREENS_UPSELL_SCREEN_H
