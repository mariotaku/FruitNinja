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
#include "engine/math/_Vector3.h"
#include "engine/audio/GameSound.h"
#include "engine/audio/MortarSound.h"
#include "game/GameWork.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
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

    // Defunct: UpsellScreen monetization -- no-op stub; v1.6.1 UpsellScreen::MakeMainUpsellScreen @ 0x001c7870
    static UpsellScreen* MakeMainUpsellScreen(Mortar::Delegate0<void> onDone);

    // Defunct: UpsellScreen monetization -- no-op stub; v1.6.1 UpsellScreen::MakeModeUpsellScreen @ 0x001c7168
    static UpsellScreen* MakeModeUpsellScreen(Mortar::Delegate0<void> onDone, int mode);
};

#if defined(__bada__)
static_assert(sizeof(UpsellScreen) == 0x1EC,
    "UpsellScreen must be 492 bytes on ARM32/Bada");
#endif

// ----------------------------------------------------------------------------
// UpsellScreenElement -- value-type sub-element stored in std::list inside
// UpsellScreen. Defunct: UpsellScreen monetization.
//
// v1.6.1 UpsellScreenElement ctors: default C1 @0x001c57e8 / C2 @0x001c599c; copy @0x001c87e8.
// (The parameterised form's v1.6.1 address is still unmapped; the old 0x00104c58 was v1.5.x.)
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
    // USESound -- queued one-shot/looping SFX for an element. 12 bytes
    // (ARM32): std::string m_Name (Sourcery rep ptr, 4B) + float m_StartT (+4)
    // + float m_EndT (+8, used as a repeat period when > 0).
    // Binary ctors @ 0x0016627c (copy); pushed by AddSound @ 0x00163ee4.
    struct USESound {
        std::string m_Name;   // +0x00
        float       m_StartT; // +0x04
        float       m_EndT;   // +0x08

        USESound() : m_Name(), m_StartT(0.0f), m_EndT(0.0f) {}
        USESound(const char* name, float startT, float endT)
            : m_Name(name), m_StartT(startT), m_EndT(endT) {}

        // Binary @ 0x00163d18 -- fire the SFX if its start time was crossed
        // between prevTime (param_2) and curTime (param_1). When m_EndT > 0 the
        // sound repeats with that period (fmod wrap detection). Returns whether
        // it played this frame.
        bool CheckSound(float curTime, float prevTime) {
            if (m_StartT <= curTime && m_Name.length() != 0) {
                GameSound* gs = game_work.mGameSound;
                if (prevTime < m_StartT) {
                    // Just crossed the start time this frame -> one-shot.
                    if (gs) {
                        gs->SFXPlay(m_Name.c_str(), 1.0f, 1.0f,
                                    Mortar::Delegate1<bool, Mortar::MortarSound*>());
                    }
                    return true;
                }
                if (m_EndT > 0.0f) {
                    // Periodic repeat: play when the (time - start) modulo period
                    // wraps between prev and cur frame.
                    float curMod  = std::fmod(curTime  - m_StartT, m_EndT);
                    float prevMod = std::fmod(prevTime - m_StartT, m_EndT);
                    if (curMod < prevMod) {
                        if (gs) {
                            gs->SFXPlay(m_Name.c_str(), 1.0f, 1.0f,
                                        Mortar::Delegate1<bool, Mortar::MortarSound*>());
                        }
                        return true;
                    }
                }
            }
            return false;
        }
    };

    // Defunct: UpsellScreen monetization -- no-op stub; v1.6.1 UpsellScreenElement::UpsellScreenElement() @ 0x001c57e8 (C2 @0x001c599c)
    UpsellScreenElement() {}

    // v1.6.1 UpsellScreenElement copy ctor @0x001c87e8 -- copies every field including the m_Sounds list
    // (std::list copy ctor). MakeMainUpsellScreen relies on this: it builds a
    // temp element, queues SFX via AddSound, then push_back's it (a copy) into
    // the screen list, so the queued sounds must survive the copy. The opaque
    // sub-struct regions are copied verbatim (the SFX path never reads them).
    UpsellScreenElement(const UpsellScreenElement& o)
        : m_Sounds(o.m_Sounds) {
        memcpy(m_pad_before, o.m_pad_before, sizeof(m_pad_before));
        memcpy(m_pad_after,  o.m_pad_after,  sizeof(m_pad_after));
    }

    // Defunct: UpsellScreen monetization -- dtor; v1.6.1 UpsellScreenElement::~UpsellScreenElement @ 0x001c6c34
    ~UpsellScreenElement() {}

    // Defunct: UpsellScreen monetization -- no-op stub; v1.6.1 UpsellScreenElement::SetTexture @ 0x001c386c
    void SetTexture(float, float, float, float, float, _Vector3<float>* /*pos*/, void* /*tex*/) {}

    // Defunct: UpsellScreen monetization -- no-op stub
    void SetAngle(unsigned short /*angleIdx*/, float /*duration*/) {}

    // Binary @ 0x00163ee4 -- queue a {name, startT, endT} SFX entry into
    // m_Sounds. MakeMainUpsellScreen calls this in a 4x loop queuing "popup-%i"
    // plus a "popup-1" finale.
    void AddSound(const char* path, float startT, float endT) {
        m_Sounds.push_back(USESound(path, startT, endT));
    }

    // Binary @ 0x00163e50 -- iterate m_Sounds and fire any whose start time was
    // crossed between prevTime and curTime. Called from UpsellScreen::Update
    // with (curTime = field281_0x1e4, prevTime = field280_0x1e0).
    void CheckSounds(float curTime, float prevTime) {
        std::list<USESound>::iterator it = m_Sounds.begin();
        for (; it != m_Sounds.end(); ++it) {
            it->CheckSound(curTime, prevTime);
        }
    }

    // Binary @ 0x00167938 -- drop all queued SFX entries.
    void ClearSounds() {
        m_Sounds.clear();
    }

private:
    // Opaque pad to reach binary sizeof = 0x39C (924 bytes). The single field
    // we actually exercise -- m_Sounds at +0x150 -- is carved out of the pad so
    // its binary offset is preserved without porting the intervening sub-struct
    // types (TranisitionInfo, PulseInfo, USEColourEntry), which the Upsell SFX
    // path never touches.
    //   +0x000..+0x14F : leading opaque region (336 bytes)
    //   +0x150         : std::list<USESound> m_Sounds (8 bytes, Sourcery list)
    //   +0x158..+0x39B : trailing opaque region (580 bytes)
    uint8_t                  m_pad_before[0x150];
    std::list<USESound>      m_Sounds;
    uint8_t                  m_pad_after[924 - 0x150 - 8];
};

#if defined(__bada__)
static_assert(sizeof(UpsellScreenElement) == 0x39C,
    "UpsellScreenElement must be 924 bytes on ARM32/Bada");
#endif

#endif // FN_SCREENS_UPSELL_SCREEN_H
