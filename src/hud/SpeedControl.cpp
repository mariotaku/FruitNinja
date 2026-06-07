// Analysed: 2026-05-03T00:00

#include "SpeedControl.h"
#include "hud/HUDLayer.h"
#include "Game.h"
#include "asset/TextureManager.h"
#include "engine/audio/GameSound.h"
#include "engine/audio/MortarSound.h"
#include "particle/PSPParticleManager.h"
#include "game/WaveManager.h"
#include "game/GameMode.h"
#include "game/GameWork.h"

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

    // Load speed-gauge texture.
    // ASM-verified: 2026-05-22 binary @ 0x0016133c (re-analyst).
    // SpeedControl loads "loading.tex" verbatim -- string literal at binary
    // 0x001bb184, PC-relative load at 0x00161352..0x0016135a. The filename
    // is misleading: "loading.tex" IS the 8-frame vertical speed-gauge atlas,
    // shared with (or repurposed from) the loading-screen spinner.
    // TextureManager::LoadLocalisedTexture (binary @ 0x0010a758) does NOT
    // remap or localise -- it just does snprintf("textures/%s", name) plus
    // a File::Exists gate. The "Localised" in the function name is a
    // misnomer carried over from an earlier engine version.
    // Slicing math (texH * 0.125f) matches binary's vmov.f32 s15,0x3e000000;
    // vmul.f32 s1,s14,s15 at 0x001613c0.
    int texW = 0, texH = 0;
    Mortar::SmartPtr<Mortar::Texture> tex = Mortar::TextureManager::LoadLocalisedTexture("loading.tex");
    if (tex.IsValid()) {
        // Binary ctor @ 0x0016133c writes +0x74 (m_Texture). HUDControl3d::Draw base reads +0x74.
        m_Texture = tex;
        texW = tex->m_Width;
        texH = tex->m_Height;
    }

    // 8-frame vertical atlas: use 1/8 height per frame.
    Vec3 sz((float)texW, (float)texH * 0.125f, 0.0f);
    size      = sz;
    m_BaseSize = sz;

    // Layer mask 0x40 (binary ctor strb).
    m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
}

// dtor @ 0x00161558 / 0x001615d4 / 0x00161650
SpeedControl::~SpeedControl() {}

// ASM-verified: 2026-05-17 binary @ 0x00160dc4 (re-analyst)
// Binary computes ducking + per-frame lerps for the Combo-Blitz speed
// effect: master-volume duck (via GameSound::m_MasterVolume), looping
// stream SFX gated on combo progression, fuse-style trail emitter
// released on idle. HUDState one-shot flags (pulseTrigger/volSnapFlag)
// at GOT+0x7c44 not ported -- both treated as 0 (skip mini-blocks).
// The "Combo-Blitz-Backing-Light" looping sound is the constant SFX
// name (.rodata @ 0x001bc258).
void SpeedControl::Update(float dt) {
    Game* g = Game::GetInstance();
    if (!g || !game_work.mGameSound) return;
    GameSound* gs = game_work.mGameSound;

    // ASM-verified: 2026-05-20 binary @ 0x00160de4 ldrb r3,[r3,#0x2] (re-analyst)
    // Gate reads game_work+0x02 = m_Paused, NOT +0x05 = m_LevelTransitionFlag.
    // Wrong gate caused Arcade-entry white-line flash: m_LevelTransitionFlag=1
    // at Arcade start, port returned early, default opaque-white m_DrawColour
    // (from HUDControl base ctor) persisted, HUDControl3d::Draw rendered the
    // quad for a frame or two until LTF cleared.
    if (game_work.m_Paused) return;

    float deltaTarget, volTarget;
    if (m_DisplayedSpeed == 0.0f) {
        // Idle: release the speed-stream emitter and stream sound.
        m_SoundVolume = 0.0f;
        if (m_pEmitter) {
            PSPParticleManager::GetInstance().ClearEmitter(
                static_cast<PSPParticleEmitter*>(m_pEmitter));
            m_pEmitter = nullptr;
        }
        m_Speed = 0.0f;
        deltaTarget = 1.0f;
        volTarget   = 0.0f;
    } else {
        // Active: combo-blitz drives the ducking. progression is the
        // wave-manager's bonus accumulator [0..1]; map [0.25..1.0]->[0..1].
        if (game_work.gameMode == Mortar::GAME_MODE_ARCADE) {
            float p   = WaveManager::GetInstance()->GetComboBonusProgression(0);
            float c01 = (p - 0.25f) / 0.75f;
            if (c01 < 0.0f) c01 = 0.0f; else if (c01 > 1.0f) c01 = 1.0f;
            float ha  = 2.0f * m_SmoothedAlpha;
            if (ha < 0.0f) ha = 0.0f; else if (ha > 1.0f) ha = 1.0f;
            deltaTarget = 1.0f + c01 * -0.2f;   // 1.0 -> 0.8 (master-vol duck)
            volTarget   = c01 * ha;             // stream SFX volume
        } else {
            deltaTarget = 1.0f;
            m_DisplayedSpeed = 0.0f;
            volTarget   = 0.0f;
        }
        // RawSpeed wraps as uint16_t per binary (signed-short accumulator).
        float clamped = m_DisplayedSpeed;
        if (clamped < 3.0f)  clamped = 3.0f;
        if (clamped > 20.0f) clamped = 20.0f;
        m_RawSpeed = (uint16_t)((float)m_RawSpeed + dt * 32760.0f * clamped * 0.25f);
    }

    // Smooth alpha toward clamp(m_Speed * 1.333, 0, 1) by 0.1 per frame.
    float t = m_Speed * 1.333f;
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    m_SmoothedAlpha += (t - m_SmoothedAlpha) * 0.1f;

    // Master-volume duck + SoundVolume lerp gated on Arcade mode.
    // Binary writes *(float*)gs == gs->m_MasterVolume directly.
    if (game_work.gameMode == Mortar::GAME_MODE_ARCADE) {
        float master = gs->m_MasterVolume;
        if      (master > deltaTarget) { master -= dt; if (master < deltaTarget) master = deltaTarget; }
        else if (master < deltaTarget) { master += dt; if (master > deltaTarget) master = deltaTarget; }
        gs->m_MasterVolume = master;

        float v = m_SoundVolume;
        if      (v > volTarget) { v -= dt * 0.5f; if (v < volTarget) v = volTarget; }
        else if (v < volTarget) { v += dt * 0.5f; if (v > volTarget) v = volTarget; }
        m_SoundVolume = v;
    } else {
        m_SoundVolume = 0.0f;
        gs->m_MasterVolume = 1.0f;
    }

    // Looping SFX start/stop on m_SoundVolume.
    static const char* const kStreamSfx = "Combo-Blitz-Backing-Light";
    if (m_SoundVolume <= 0.0f) {
        if (m_pSound) {
            gs->Release(static_cast<Mortar::MortarSound*>(m_pSound), kStreamSfx);
            m_pSound = nullptr;
        }
    } else {
        if (!m_pSound) {
            m_SoundIdx = 0;
            Mortar::Delegate1<bool, Mortar::MortarSound*> loopCb =
                Mortar::Delegate1<bool, Mortar::MortarSound*>::Make(
                    this, &SpeedControl::SoundNeedsLooping);
            // ASM-verified: 2026-05-18 binary @ 0x00160dc4 (re-analyst)
            m_pSound = gs->SFXPlay(kStreamSfx, m_SoundVolume, 0.0f, loopCb);
        }
        if (m_pSound) {
            static_cast<Mortar::MortarSound*>(m_pSound)->SetVolume(m_SoundVolume);
        }
    }

    // Final draw-colour alpha = clamp(m_SmoothedAlpha * 16, 0, 255).
    float aF = m_SmoothedAlpha * 16.0f;
    uint8_t a;
    if      (aF <= 0.0f)   a = 0;
    else if (aF >= 255.0f) a = 0xFF;
    else                   a = (uint8_t)aF;
    m_DrawColour = Colour(0xF6, 0xD4, 0xC1, a);
}

// Binary @ 0x00160cdc -- no-op (single bx lr). Slot exists for vtable
// parity; Update() handles all per-frame state, including lazy emitter
// alloc, on the first non-idle frame.
void SpeedControl::Init() {}

// Binary @ 0x00160ce4 -- empty pass-through.
void SpeedControl::PreDraw(const Vec3& /*hudScale*/) {}

// Binary @ 0x00160cd8 -- no-op (single bx lr). State is driven entirely
// by m_DisplayedSpeed in Update; no per-Reset wipe.
void SpeedControl::Reset() {}

// Binary @ 0x00160ce0 -- no-op (single bx lr).
void SpeedControl::Skip() {}

// Binary @ 0x00160ce8 (re-analyst 2026-05-18) -- looping-stream restart
// callback handed to GameSound::SFXPlay. When the previous stream
// instance finishes, re-issue SFXPlay with the appropriate name (light
// or heavy variant by wave count). Returns false (binary uint32 0).
bool SpeedControl::SoundNeedsLooping(Mortar::MortarSound* finished) {
    if (m_pSound != finished) return false;   // not our loop -- ignore

    // ASM-verified: 2026-05-18 binary @ 0x00160ce8 (re-analyst)
    // After 6+ waves (WaveManager+0x5C), swap to the heavier stream variant.
    WaveManager* wm = WaveManager::GetInstance();
    if (wm && wm->m_BlitzBonus[0] > 5) {
        m_SoundIdx = 1;
    }

    // Second SFX name confirmed from .rodata GOT[0x00160dac][1].
    // ASM-verified: 2026-05-18 binary @ 0x00160ce8 (re-analyst)
    static const char* const kSfxNames[2] = {
        "Combo-Blitz-Backing-Light",
        "Combo-Blitz-Backing"
    };
    const char* const kStreamSfx = kSfxNames[m_SoundIdx];
    Game* g = Game::GetInstance();
    if (!g || !game_work.mGameSound) return false;
    Mortar::Delegate1<bool, Mortar::MortarSound*> loopCb =
        Mortar::Delegate1<bool, Mortar::MortarSound*>::Make(
            this, &SpeedControl::SoundNeedsLooping);
    m_pSound = game_work.mGameSound->SFXPlay(kStreamSfx, 0.0f, 0.0f, loopCb);
    if (m_pSound) {
        static_cast<Mortar::MortarSound*>(m_pSound)->SetVolume(0.0f);
    }
    return false;
}
