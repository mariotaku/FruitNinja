// Analysed: 2026-05-03T00:00

#include "SpeedControl.h"
#include "Game.h"
#include "asset/TextureManager.h"

// ctor @ 0x0016133c
SpeedControl::SpeedControl()
    : m_RawSpeed(0),
      m_DisplayedSpeed(0.0f),
      m_PulseScale(1.0f),
      m_BaseSize(0.0f, 0.0f, 0.0f),
      m_Speed(1.0f),
      m_SmoothedAlpha(0.0f),
      m_pSound(nullptr),
      m_pEmitter(nullptr),
      m_SoundIdx(0),
      m_SoundVolume(0.0f)
{
    _pad7e[0] = 0; _pad7e[1] = 0;

    // HUDControl base: SpeedControl opts out of HUD tint modulation.
    m_bUseHUDScales = 0;

    // Load localised speed gauge texture.
    // TODO: resolve DAT_00161440 string-table key — using "speed_control" as placeholder.
    int texW = 0, texH = 0;
    SmartPtr<Mortar::Texture> tex = Mortar::TextureManager::LoadLocalisedTexture("speed_control.tex");
    if (tex.IsValid()) {
        m_Texture = tex->m_TexId;
        texW = tex->m_Width;
        texH = tex->m_Height;
    }

    // 8-frame vertical atlas: use 1/8 height per frame.
    Vec3 sz((float)texW, (float)texH * 0.125f, 0.0f);
    size      = sz;
    m_BaseSize = sz;

    // Layer mask 0x40 (binary ctor strb).
    m_LayerFlags = 0x40;
}

// dtor @ 0x00161558 / 0x001615d4 / 0x00161650
SpeedControl::~SpeedControl() {}

// Update @ 0x00160dc4
void SpeedControl::Update(float dt) {
    Game* g = Game::GetInstance();
    if (!g) return;

    if (g->pauseFlag != 0) return;

    if (m_DisplayedSpeed == 0.0f) {
        m_SoundVolume = 0.0f;
        // TODO: 0x00160e08 Mortar::PSPParticleManager::ClearEmitter(m_pEmitter); m_pEmitter = nullptr;
        m_Speed = 0.0f;
    } else {
        // Clamp displayed speed to [3, 20] and accumulate raw speed counter.
        float clamped = m_DisplayedSpeed;
        if (clamped < 3.0f)  clamped = 3.0f;
        if (clamped > 20.0f) clamped = 20.0f;
        // Binary: signed-short wrapping accumulator (uint16_t wraps naturally).
        m_RawSpeed = (uint16_t)((float)m_RawSpeed + dt * 32760.0f * clamped * 0.25f);
    }

    // Smooth alpha toward clamp(m_Speed * 1.333, 0, 1).
    float target = m_Speed * 1.333f;
    if (target < 0.0f) target = 0.0f;
    if (target > 1.0f) target = 1.0f;
    m_SmoothedAlpha += (target - m_SmoothedAlpha) * 0.1f;

    // Map smoothed alpha to draw colour alpha channel (binary: * 16, clamped [0,255]).
    float alphaF = m_SmoothedAlpha * 16.0f;
    if (alphaF < 0.0f)   alphaF = 0.0f;
    if (alphaF > 255.0f) alphaF = 255.0f;
    m_DrawColour = Colour(0xF6, 0xD4, 0xC1, (uint8_t)alphaF);

    // TODO: 0x00160e80 pulse-scale + game[+0x188] global-uniform write.
    // TODO: 0x00160ea0 sound play/stop via GameSound::SFXPlay/Release (m_pSound, m_SoundIdx).
    // TODO: 0x00160f00 PSPParticleEmitter speed-stream wiring.
}
