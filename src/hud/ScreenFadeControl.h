#ifndef FN_HUD_SCREENFADECONTROL_H
#define FN_HUD_SCREENFADECONTROL_H

// Analysed: 2026-05-04T00:00
// ScreenFadeControl — full-screen alpha fade overlay.
// Binary: sizeof 0xB8, ctor @ 0x0015AA1C, symbol ScreenFadeControl::* (no Mortar:: namespace).
//
// Layout relative to HUDControl3d base (0x7C bytes):
//   +0x7C: m_bVisible       uint8_t
//   +0x7D: m_bAnimating     uint8_t
//   +0x7E: m_bStaysVisibleAfter uint8_t
//   (pad +0x7F)
//   +0x80: m_Timer          float
//   +0x84: m_Duration       float
//   +0x88: m_Colour         Colour (BGRA, 4 bytes; alpha byte at +0x8B = m_CurAlpha)
//   +0x8C: m_StartAlpha     uint8_t
//   +0x8D: m_FromAlpha      uint8_t
//   +0x8E: m_TargetAlpha    uint8_t
//   (pad +0x8F)
//   +0x90: m_OnComplete     Mortar::Delegate0<void> (36 bytes)
//   +0xB4: m_FadeTexture    Mortar::SmartPtr<Mortar::Texture> (4 bytes)
//   Total: 0xB8

#include "HUDControl3d.h"
#include "util/Delegate.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "math/Colour.h"
#include <cstdint>

class ScreenFadeControl : public HUDControl3d {
public:
    // Binary @ 0x0015AA1C
    ScreenFadeControl();
    virtual ~ScreenFadeControl();

    // vtable +0x08: Binary @ 0x0015A724
    virtual void Init() override;
    // vtable +0x0C: Binary @ 0x0015A730 -- empty
    virtual void Release() override {}
    // vtable +0x10: Binary @ 0x0015A734
    virtual void Reset() override;
    // vtable +0x18: Binary @ 0x0015A750 -- pass-through
    virtual void PreDraw(float* hudScale) override { (void)hudScale; }
    // vtable +0x28: Binary @ 0x0015A798
    virtual void Update(float dt) override;
    // vtable +0x1C: Binary @ 0x0015A868
    virtual void Draw(float* hudScaleRaw) override;
    // vtable +0x2C: Binary @ 0x0015A754 -- Reset(); return false
    virtual bool SetToMultiplayerState() override;
    // vtable +0x30: Binary @ 0x0015AEA0
    virtual int GetType() override { return 0xC; }

    // Binary @ 0x0015A7F0 -- replace prior fade; alpha 0<->255 over duration.
    void StartFade(bool inOrOut, float duration, const Colour& color,
                   Mortar::Delegate0<void> onComplete);

    // Binary @ 0x0015A764 -- clears visible+animating; does NOT fire OnComplete
    void CancelFade();

private:
    // Binary @ 0x0015A770
    void OnFadeComplete();

    uint8_t  m_bVisible;             // +0x7C
    uint8_t  m_bAnimating;           // +0x7D
    uint8_t  m_bStaysVisibleAfter;   // +0x7E
    uint8_t  _pad7F;                 // +0x7F

    float    m_Timer;                // +0x80
    float    m_Duration;             // +0x84

    Colour   m_Colour;               // +0x88 (BGRA; alpha at +0x8B)
    uint8_t  m_StartAlpha;           // +0x8C
    uint8_t  m_FromAlpha;            // +0x8D
    uint8_t  m_TargetAlpha;          // +0x8E
    uint8_t  _pad8F;                 // +0x8F

    Mortar::Delegate0<void>   m_OnComplete;   // +0x90 (36 bytes)
    Mortar::SmartPtr<Mortar::Texture>  m_FadeTexture;  // +0xB4 (4 bytes)
};

// v1.6.1 DefaultScreenFadeCompleteCallback @0x1aec48: empty no-op used as the
// default Mortar::Delegate0<void> onComplete handler for ScreenFadeControl::StartFade.
void DefaultScreenFadeCompleteCallback();

#endif // FN_HUD_SCREENFADECONTROL_H
