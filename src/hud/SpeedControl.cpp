// Analysed: 2026-05-03T00:00

#include "SpeedControl.h"
#include "hud/HUDLayer.h"
#include "Game.h"
#include "asset/TextureManager.h"
#include "asset/Mesh.h"
#include "engine/audio/GameSound.h"
#include "engine/audio/MortarSound.h"
#include "particle/PSPParticleManager.h"
#include "game/WaveManager.h"
#include "game/GameMode.h"
#include "game/GameWork.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/Layout.h"

// ctor @ 0x001b892c
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
    // ASM-spec v1.6.1 SpeedControl::SpeedControl @0x001b892c: loads "loading.tex"
    // verbatim -- string literal at binary 0x001bb184. The filename is misleading:
    // "loading.tex" IS the 8-frame vertical speed-gauge atlas, shared with (or
    // repurposed from) the loading-screen spinner.
    // TextureManager::LoadLocalisedTexture v1.6.1 @ 0x0011a768 DOES remap textures
    // by language: checks textures/<lang>/<name> first (File::Exists gate), then
    // falls back to textures/<name>. See TextureManager.cpp for the locale switch.
    // DIFFERS from v1.5.x @0x0010a758 (base path only -- no locale switch).
    // TODO: v1.6.1 0x001b892c (SpeedControl::SpeedControl) -- re-verify exact
    // PC-relative instruction offsets for the string load / texH*0.125f slicing
    // math (previously mis-cited against a stale 0x0016133c address).
    int texW = 0, texH = 0;
    Mortar::SmartPtr<Mortar::Texture> tex = Mortar::TextureManager::LoadLocalisedTexture("loading.tex");
    if (tex.IsValid()) {
        // Binary ctor @ 0x001b892c writes +0x74 (m_Texture). HUDControl3d::Draw base reads +0x74.
        m_Texture = tex;
        texW = tex->GetWidth();
        texH = tex->GetHeight();
    }

    // 8-frame vertical atlas: use 1/8 height per frame.
    _Vector3<float> sz((float)texW, (float)texH * 0.125f, 0.0f);
    size      = sz;
    m_BaseSize = sz;

    // Layer mask 0x40 (binary ctor strb).
    m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
}

// ASM-spec v1.6.1 SpeedControl::~SpeedControl @0x001b8ba4 (base/D2), @0x001b8c4c
// (complete/D1), @0x001b8cf4 (deleting/D0): releases the active combo-blitz
// backing-stream m_pSound via GameSound::Release (name indexed by m_SoundIdx),
// nulls m_pSound, and restores game_work.mGameSound->m_MasterVolume to 1.0
// before falling through to the base ~HUDControl3d chain. The m_Texture
// SmartPtr release the binary also performs at this scope is redundant with
// HUDControl3d's own SmartPtr<Texture> member dtor -- no port action needed.
SpeedControl::~SpeedControl() {
    if (m_pSound) {
        static const char* const kSfxNames[2] = {
            "Combo-Blitz-Backing-Light",
            "Combo-Blitz-Backing"
        };
        if (game_work.mGameSound) {
            game_work.mGameSound->Release(static_cast<Mortar::MortarSound*>(m_pSound), kSfxNames[m_SoundIdx]);
            m_pSound = nullptr;
            game_work.mGameSound->m_MasterVolume = 1.0f;
        }
    }
}

// ASM-spec v1.6.1 SpeedControl::Update @0x001b8290
//   (downgraded from ASM-verified 2026-05-17: the stamp covered a body that
//    carried port-added `Game::GetInstance()` + `mGameSound` null guards.)
// Binary computes ducking + per-frame lerps for the Combo-Blitz speed
// effect: master-volume duck (via GameSound::m_MasterVolume), looping
// stream SFX gated on combo progression, fuse-style trail emitter
// released on idle. HUDState one-shot flags (pulseTrigger/volSnapFlag,
// GOT+0x7c44) are ported below as function-local one-shot statics
// (fire exactly once per process lifetime, matching the binary's
// GOT-backed flag semantics -- the HUDState subsystem itself isn't
// ported, so the flag storage is inlined here rather than in a struct).
// The "Combo-Blitz-Backing-Light" looping sound is the constant SFX
// name (.rodata @ 0x001bc258).
void SpeedControl::Update(float dt) {
    // Binary reads game_work straight from the GOT and derefs
    // game_work.pM_pGameSound (+0x18c) with no null test (@0x001b8618,
    // @0x001b851c). No Game::GetInstance call in the body.
    GameSound* gs = game_work.mGameSound;

    // ASM-spec v1.6.1 SpeedControl::Update @0x001b8290: one-shot "pulseTrigger"
    // correction. Fires exactly once (process lifetime): if the control's
    // very first Update lands while already paused (bM_Mode set) with a
    // nonzero displayed speed (e.g. re-entering a saved paused session),
    // snap the pulse/alpha/colour state directly instead of leaving the
    // ctor defaults to persist through the early-return below.
    static bool s_PulseTriggerPending = true;
    if (s_PulseTriggerPending) {
        if (game_work.bM_Mode && m_DisplayedSpeed > 0.0f) {
            m_PulseScale = 1.0f;
            float t0 = m_Speed * 1.333f;
            if (t0 < 0.0f) t0 = 0.0f; else if (t0 > 1.0f) t0 = 1.0f;
            m_SmoothedAlpha = t0;
            uint8_t a0 = (uint8_t)(m_SmoothedAlpha * 16.0f);
            m_DrawColour = Colour(0xF6, 0xD4, 0x00, a0);
        }
        s_PulseTriggerPending = false;
    }

    // ASM-verified: 2026-05-20 v1.6.1 SpeedControl::Update @0x001b8290 (re-analyst)
    // Gate reads game_work+0x02 = bM_Mode, NOT +0x05 = bM_bPaused.
    // Wrong gate caused Arcade-entry white-line flash: bM_bPaused=1
    // at Arcade start, port returned early, default opaque-white m_DrawColour
    // (from HUDControl base ctor) persisted, HUDControl3d::Draw rendered the
    // quad for a frame or two until LTF cleared.
    if (game_work.bM_Mode) return;

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
        // ASM-spec v1.6.1 SpeedControl::Update @0x001b8380: gate also requires
        // game_work.m_PauseAmount == 0.0f (settled/active) -- paused-arcade
        // (mid pause-fade or fully paused) falls through to the else branch below.
        if (game_work.gameMode == GAME_MODE_ARCADE && game_work.m_PauseAmount == 0.0f) {
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
    if (game_work.gameMode == GAME_MODE_ARCADE) {
        float master = gs->m_MasterVolume;
        if      (master > deltaTarget) { master -= dt; if (master < deltaTarget) master = deltaTarget; }
        else if (master < deltaTarget) { master += dt; if (master > deltaTarget) master = deltaTarget; }
        gs->m_MasterVolume = master;

        // ASM-spec v1.6.1 SpeedControl::Update @0x001b8290: one-shot "volSnapFlag".
        // Fires exactly once (process lifetime): the first time the arcade
        // ducking branch runs, m_SoundVolume snaps straight to volTarget
        // instead of the usual +-dt*0.5 lerp, avoiding a slow initial ramp-in.
        static bool s_VolSnapPending = true;
        float v = m_SoundVolume;
        if (s_VolSnapPending) {
            v = volTarget;
            s_VolSnapPending = false;
        } else if (v > volTarget) { v -= dt * 0.5f; if (v < volTarget) v = volTarget; }
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
            // ASM-verified: 2026-05-18 v1.6.1 SpeedControl::Update @0x001b8290 (re-analyst)
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
    // DIFFERS fix: binary b-channel is 0x00, not 0xC1 (earlier summary was wrong).
    m_DrawColour = Colour(0xF6, 0xD4, 0x00, a);
}

// ASM-spec v1.6.1 SpeedControl::Draw @0x001b8788
// Renders the 4-panel scrolling combo-blitz chevron speed-line strip.
// Overrides HUDControl3d::Draw entirely (does not chain to the base quad
// draw) -- gate is m_SmoothedAlpha alone; no texture-validity/alpha==0
// gate like the base, no colour save/restore, no blend/texenv state
// (colour rides in the per-vertex QUADCUSTOMVERTEX::colour, texenv is
// left at whatever HUDControl3d::Draw or the previous draw call left it).
// hudScale is read by the binary's vtable signature for parity but unused
// in the body (the base's HUD-tint modulation doesn't apply here).
void SpeedControl::Draw(float* /*hudScale*/) {
    if (!(m_SmoothedAlpha > 0.001f)) return;

    // 6-vertex tri-strip chevron, z=0, normal=(0,0,1), colour=m_DrawColour,
    // u=v=0.5 (constant -- texture is a solid-fill / dithered atlas frame,
    // not sampled by shape). (x,y) per binary: x = vertical (320-unit) axis,
    // y = horizontal (480-unit) axis -- see docs/engine/coordinate-system.md.
    // DIFFERS: opt-in widescreen -- the +-240 entries are the full-width overlay
    // edges (not anchored positions), so scale by HalfWidth()/240 to keep the
    // chevron stripe spanning the whole (possibly widened) field. Identity
    // (240.0f) when disabled/__bada__.
#ifdef __bada__
    const float edgeX = 240.0f;
#else
    const float edgeX = Layout::HalfWidth();
#endif
    const float kX[6] = { -edgeX, -edgeX,    0.0f,    0.0f,  edgeX,  edgeX };
    static const float kY[6] = { -240.0f, -320.0f,    0.0f,  -80.0f, -240.0f, -320.0f };

    const uint32_t packed = m_DrawColour.PlatformColour();
    QUADCUSTOMVERTEX v[6];
    for (int i = 0; i < 6; ++i) {
        v[i].x = kX[i]; v[i].y = kY[i]; v[i].z = 0.0f;
        v[i].nx = 0.0f; v[i].ny = 0.0f; v[i].nz = 1.0f;
        v[i].colour = packed;
        v[i].u = 0.5f; v[i].v = 0.5f;
    }

    m_Texture->Set();

    MatrixManager& mm = MatrixManager::GetInstance();
    // Raw speed accumulator [0..65535] maps to a horizontal scroll offset
    // in [-160, 0); four 160-wide panels advance in lockstep to cover the
    // full 480-wide screen with continuous wraparound scroll.
    float scroll = (float)m_RawSpeed * (1.0f / 65536.0f) * 160.0f - 160.0f;
    for (int i = 0; i < 4; ++i) {
        mm.GetWorldStack().Reset();
        mm.GetWorldStack().Translate(_Vector3<float>(0.0f, scroll, -5550.0f));
        mm.UploadModelViewOnly();
        Mortar::Mesh::DrawTriStrip(v, 6, false, NULL);
        scroll += 160.0f;
    }

    m_Texture->UnSet(true);
}

// Binary @ 0x001b8154 -- no-op (single bx lr). Slot exists for vtable
// parity; Update() handles all per-frame state, including lazy emitter
// alloc, on the first non-idle frame.
void SpeedControl::Init() {}

// Binary @ 0x001b815c -- empty pass-through.
void SpeedControl::PreDraw(float* /*hudScale*/) {}

// Binary @ 0x001b8150 -- no-op (single bx lr). State is driven entirely
// by m_DisplayedSpeed in Update; no per-Reset wipe.
void SpeedControl::Reset() {}

// Binary @ 0x001b8158 -- no-op (single bx lr).
void SpeedControl::Skip() {}

// Binary @ 0x001b8160 (re-analyst 2026-05-18) -- looping-stream restart
// callback handed to GameSound::SFXPlay. When the previous stream
// instance finishes, re-issue SFXPlay with the appropriate name (light
// or heavy variant by wave count). Returns false (binary uint32 0).
bool SpeedControl::SoundNeedsLooping(Mortar::MortarSound* finished) {
    if (m_pSound != finished) return false;   // not our loop -- ignore

    // ASM-spec v1.6.1 SpeedControl::SoundNeedsLooping @ 0x001b8160
    //   (downgraded from ASM-verified 2026-05-18: the stamp covered a body that
    //    carried a port-added `WaveManager::GetInstance()` null guard.)
    // After 6+ blitz tiers (WaveManager m_BlitzLevel +0x60), swap to the heavier stream variant.
    if (WaveManager::GetInstance()->m_BlitzLevel > 5) {
        m_SoundIdx = 1;
    }

    // Second SFX name confirmed from .rodata GOT[0x00160dac][1].
    static const char* const kSfxNames[2] = {
        "Combo-Blitz-Backing-Light",
        "Combo-Blitz-Backing"
    };
    const char* const kStreamSfx = kSfxNames[m_SoundIdx];
    // Binary @0x001b8160 calls GameSound::SFXPlay unguarded -- no
    // Game::GetInstance, no pM_pGameSound null test.
    Mortar::Delegate1<bool, Mortar::MortarSound*> loopCb =
        Mortar::Delegate1<bool, Mortar::MortarSound*>::Make(
            this, &SpeedControl::SoundNeedsLooping);
    m_pSound = game_work.mGameSound->SFXPlay(kStreamSfx, 0.0f, 0.0f, loopCb);
    if (m_pSound) {
        static_cast<Mortar::MortarSound*>(m_pSound)->SetVolume(0.0f);
    }
    return false;
}
