// Analysed: 2026-05-03T00:00

#include "ScreenEffect.h"
#include "PowerUp.h"
#include "util/StringHash.h"
#include "math/MathUtil.h"
#include "particle/PSPParticleManager.h"
#include "hud/HUDControl3d.h"
#include "hud/HUD.h"
#include "audio/GameSound.h"
#include "Game.h"
#include <tinyxml2.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include "game/GameWork.h"

using namespace tinyxml2;
using namespace Mortar;

// Binary @ 0x0011e150 area — check particle hardware flag.
// Port: always return true (all hardware considered "fast").
static bool IsFastHardware() {
    Game* g = Game::GetInstance();
    if (g) return g->IsFastHardware();
    return true;
}

// ---- Emmiter::Parse (binary @ 0x0011e150 child "emmiter" block) ---------------

void Emmiter::Parse(XMLElement* xml) {
    if (!xml) return;
    const char* name = xml->Attribute("name");
    if (name) m_NameHash = StringHash(name);

    const char* off = xml->Attribute("offset");
    if (off) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        sscanf(off, "%f %f %f", &x, &y, &z);
        m_Offset = Vec3(x, y, z);
    }

    const char* vel = xml->Attribute("velocityScale");
    if (vel) {
        float x = 1.0f, y = 1.0f, z = 1.0f;
        sscanf(vel, "%f %f %f", &x, &y, &z);
        m_VelocityScale = Vec3(x, y, z);
    }
}

// ---- EffectImage::Parse -------------------------------------------------------

void EffectImage::Parse(XMLElement* xml) {
    if (!xml) return;

    const char* tex = xml->Attribute("texture");
    if (tex) {
        strncpy(m_TexName, tex, sizeof(m_TexName) - 1);
        m_TexName[sizeof(m_TexName) - 1] = '\0';
    }

    const char* multi = xml->Attribute("multiplyer");
    if (multi) m_bIsMultiplyerBoard = (strcmp(multi, "true") == 0 || strcmp(multi, "1") == 0);

    const char* pos = xml->Attribute("pos");
    if (pos) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        sscanf(pos, "%f %f %f", &x, &y, &z);
        m_Pos = Vec3(x, y, z);
    }

    // XML uses transitionMoveIn / transitionMoveOut for entry and exit velocity.
    // m_Vel stores the entry vector; exit vector is not yet separately tracked.
    // TODO: add m_VelOut field when exit-slide animation is ported.
    const char* moveIn = xml->Attribute("transitionMoveIn");
    if (moveIn) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        sscanf(moveIn, "%f,%f,%f", &x, &y, &z);
        m_Vel = Vec3(x, y, z);
    }
    // TODO: transitionMoveOut — not separately stored yet; needs m_VelOut field
    // const char* moveOut = xml->Attribute("transitionMoveOut");

    // transitionTime drives m_FadeRate (seconds for fade-in/out).
    // TODO: "fade" attribute not present in current powerUpList.xml; kept for
    // any older XML that may use the direct rate form.
    const char* transitionTime = xml->Attribute("transitionTime");
    if (transitionTime) m_FadeRate = (float)atof(transitionTime);
    const char* fade = xml->Attribute("fade");
    if (fade) m_FadeRate = (float)atof(fade);

    // TODO: "transition" string ("fade", "slide") — controls animation mode;
    // not yet separately stored. Needs a mode enum field when ported.
    // const char* transition = xml->Attribute("transition");

    xml->QueryUnsignedAttribute("group", &m_GroupMask);

    const char* freq = xml->Attribute("freq");
    if (freq) m_Freq = (float)atof(freq);

    const char* amp1 = xml->Attribute("amp1");
    if (amp1) m_Amp1 = (float)atof(amp1);

    const char* amp2 = xml->Attribute("amp2");
    if (amp2) m_Amp2 = (float)atof(amp2);

    const char* sizeIn = xml->Attribute("sizeIn");
    if (sizeIn) {
        float x = 1.0f, y = 1.0f, z = 1.0f;
        sscanf(sizeIn, "%f %f %f", &x, &y, &z);
        m_SizeIn = Vec3(x, y, z);
    }

    const char* sizeOut = xml->Attribute("sizeOut");
    if (sizeOut) {
        float x = 1.0f, y = 1.0f, z = 1.0f;
        sscanf(sizeOut, "%f %f %f", &x, &y, &z);
        m_SizeOut = Vec3(x, y, z);
    }

    const char* timeStart = xml->Attribute("timeStart");
    if (timeStart) m_StartT = (float)atof(timeStart);

    const char* timeEnd = xml->Attribute("timeEnd");
    if (timeEnd) m_EndT = (float)atof(timeEnd);

    const char* cs = xml->Attribute("colourScale");
    if (cs) {
        float x = 1.0f, y = 1.0f, z = 1.0f;
        sscanf(cs, "%f %f %f", &x, &y, &z);
        m_ColourScale = Vec3(x, y, z);
    }

    const char* tint = xml->Attribute("tint");
    if (tint) {
        int r = 255, g = 255, b = 255, a = 255;
        sscanf(tint, "%d %d %d %d", &r, &g, &b, &a);
        m_Tint = Colour((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a);
    }

    const char* lowEnd = xml->Attribute("lowEndOnly");
    if (lowEnd) m_bLowEndOnly = (strcmp(lowEnd, "true") == 0 || strcmp(lowEnd, "1") == 0);

    const char* flags = xml->Attribute("flags");
    if (flags) m_FlagBits = (uint32_t)atoi(flags);
}

void EffectImage::LoadTextures() {
    // TODO: load m_TexName via ReloadableTexture::Load when asset pipeline ready
    // For now: no-op stub — m_TexHandle remains 0.
}

// ---- ScreenTint::Parse -------------------------------------------------------

void ScreenTint::Parse(XMLElement* xml) {
    if (!xml) return;

    // TODO: "length" attribute not present in current powerUpList.xml; may exist
    // in older XML versions. Keep read in case binary used it.
    const char* length = xml->Attribute("length");
    if (length) m_Length = (float)atof(length);

    const char* timeStart = xml->Attribute("timeStart");
    if (timeStart) m_StartT = (float)atof(timeStart);

    const char* transitionTime = xml->Attribute("transitionTime");
    if (transitionTime) m_FadeIn = (float)atof(transitionTime);

    // TODO: "to" / "from" attributes not present in current powerUpList.xml;
    // XML uses "tint=", "backTint=", "hudTint=" instead. Keep reads for
    // compatibility with any other XML that may use the older attribute names.
    const char* to = xml->Attribute("to");
    if (to) {
        float x = 1.0f, y = 1.0f, z = 1.0f;
        sscanf(to, "%f %f %f", &x, &y, &z);
        m_ColourTo = Vec3(x, y, z);
    }

    const char* from = xml->Attribute("from");
    if (from) {
        float x = 1.0f, y = 1.0f, z = 1.0f;
        sscanf(from, "%f %f %f", &x, &y, &z);
        m_ColourFrom = Vec3(x, y, z);
    }
}

// ---- SoundEffect::Parse -------------------------------------------------------

void SoundEffect::Parse(XMLElement* xml) {
    if (!xml) return;

    const char* name = xml->Attribute("name");
    if (name) {
        strncpy(m_SoundName, name, sizeof(m_SoundName) - 1);
        m_SoundName[sizeof(m_SoundName) - 1] = '\0';
    }

    const char* timeStart = xml->Attribute("timeStart");
    if (timeStart) m_StartT = (float)atof(timeStart);

    const char* timeEnd = xml->Attribute("timeEnd");
    if (timeEnd) m_EndT = (float)atof(timeEnd);
}

// ---- ScreenEffect ctor/dtor --------------------------------------------------

// Binary @ 0x0011d568
ScreenEffect::ScreenEffect()
    : m_NameHash(0)
    , m_pOwnerPowerUp(nullptr)
    , m_RemainingTime(0.0f)
    , m_TotalDuration(0.0f)
{
    memset(m_Name, 0, sizeof(m_Name));
}

// Binary @ 0x0011bc78
ScreenEffect::ScreenEffect(const ScreenEffect& rhs)
    : m_Emmiters(rhs.m_Emmiters)
    , m_Images(rhs.m_Images)
    , m_Tints(rhs.m_Tints)
    , m_Sounds(rhs.m_Sounds)
    , m_NameHash(rhs.m_NameHash)
    , m_pOwnerPowerUp(rhs.m_pOwnerPowerUp)
    , m_RemainingTime(rhs.m_RemainingTime)
    , m_TotalDuration(rhs.m_TotalDuration)
{
    memcpy(m_Name, rhs.m_Name, sizeof(m_Name));
    // Handles in Emmiters are not copied: the clone has its own emitters
    for (size_t i = 0; i < m_Emmiters.size(); ++i)
        m_Emmiters[i].m_pHandle = nullptr;
    // HUDControl pointers are not copied: the clone adds its own controls
    for (size_t i = 0; i < m_Images.size(); ++i) {
        m_Images[i].m_pHudCtrl = nullptr;
        m_Images[i].m_bAddedToHUD = false;
    }
    // Sound handles are not copied
    for (size_t i = 0; i < m_Sounds.size(); ++i)
        m_Sounds[i].m_VoiceHandle = nullptr;
}

// Binary @ 0x0011d5a0
ScreenEffect::~ScreenEffect() {
    Deactivate();
}

ScreenEffect& ScreenEffect::operator=(const ScreenEffect& rhs) {
    if (this != &rhs) {
        Deactivate();
        m_Emmiters = rhs.m_Emmiters;
        m_Images   = rhs.m_Images;
        m_Tints    = rhs.m_Tints;
        m_Sounds   = rhs.m_Sounds;
        memcpy(m_Name, rhs.m_Name, sizeof(m_Name));
        m_NameHash        = rhs.m_NameHash;
        m_pOwnerPowerUp   = rhs.m_pOwnerPowerUp;
        m_RemainingTime   = rhs.m_RemainingTime;
        m_TotalDuration   = rhs.m_TotalDuration;
        for (size_t i = 0; i < m_Emmiters.size(); ++i)
            m_Emmiters[i].m_pHandle = nullptr;
        for (size_t i = 0; i < m_Images.size(); ++i) {
            m_Images[i].m_pHudCtrl    = nullptr;
            m_Images[i].m_bAddedToHUD = false;
        }
        for (size_t i = 0; i < m_Sounds.size(); ++i)
            m_Sounds[i].m_VoiceHandle = nullptr;
    }
    return *this;
}

// ---- ScreenEffect::Parse (binary @ 0x0011e150) --------------------------------

void ScreenEffect::Parse(XMLElement* xml) {
    if (!xml) return;

    const char* name = xml->Attribute("name");
    if (name) {
        strncpy(m_Name, name, sizeof(m_Name) - 1);
        m_Name[sizeof(m_Name) - 1] = '\0';
        m_NameHash = StringHash(m_Name);
    }

    const char* length = xml->Attribute("length");
    if (length) {
        m_TotalDuration = (float)atof(length);
        if (m_TotalDuration > 0.0f)
            m_RemainingTime = m_TotalDuration;
    }

    for (XMLElement* child = xml->FirstChildElement();
         child; child = child->NextSiblingElement()) {
        const char* tag = child->Name();
        if (!tag) continue;

        // Hardware filter — "fast" = IsFastHardware() must be true;
        // "slow" = IsFastHardware() must be false.
        const char* hw = child->Attribute("hardware");
        if (hw) {
            bool needFast = (strcmp(hw, "fast") == 0);
            bool needSlow = (strcmp(hw, "slow") == 0);
            if (needFast && !IsFastHardware()) continue;
            if (needSlow && IsFastHardware()) continue;
        }

        if (strcmp(tag, "emmiter") == 0) {
            Emmiter em;
            em.Parse(child);
            m_Emmiters.push_back(em);
        } else if (strcmp(tag, "image") == 0) {
            EffectImage img;
            img.Parse(child);
            m_Images.push_back(img);
        } else if (strcmp(tag, "tint") == 0) {
            ScreenTint tint;
            tint.Parse(child);
            m_Tints.push_back(tint);
        } else if (strcmp(tag, "sound") == 0) {
            SoundEffect sfx;
            sfx.Parse(child);
            m_Sounds.push_back(sfx);
        }
    }
}

// ---- ScreenEffect::Activate (binary @ 0x0011dbb8) ----------------------------

void ScreenEffect::Activate() {
    PSPParticleManager& pm = PSPParticleManager::GetInstance();

    // Spawn particle emitters
    for (size_t i = 0; i < m_Emmiters.size(); ++i) {
        Emmiter& em = m_Emmiters[i];
        // Only spawn if not already active (m_pHandle == nullptr = not running)
        if (!em.m_pHandle) {
            pm.AddEmitter(em.m_NameHash, &em.m_pHandle, false);
            if (em.m_pHandle) {
                em.m_pHandle->m_Pos   = em.m_Offset;
                em.m_pHandle->m_Vel   = em.m_VelocityScale;
            }
        }
    }

    // Create HUD controls for images
    Game* game = Game::GetInstance();
    HUD* hud   = game ? game_work.mHud : nullptr;

    for (size_t i = 0; i < m_Images.size(); ++i) {
        EffectImage& img = m_Images[i];
        if (img.m_bAddedToHUD) continue;

        // Binary @ 0x0011dbb8: if m_bIsMultiplyerBoard, create ScoreMultiplyerBoard;
        // otherwise create HUDControl3d.
        // ScoreMultiplyerBoard is not yet ported — stub as HUDControl3d.
        // TODO: create ScoreMultiplyerBoard when that class is ported.
        HUDControl3d* ctrl = new HUDControl3d();
        // pos offset (480, 320, 0) added internally by HUDControl3d::Draw
        ctrl->pos = img.m_Pos;
        ctrl->size = img.m_SizeIn;
        ctrl->m_DrawColour = img.m_Tint;
        // Binary @ 0x0011dd2e: `str r1, [r2, #0x34]` -- raw copy of
        // EffectImage::m_GroupMask, no `?: 1` fallback. If data has 0,
        // the binary writes 0 (HUD_LAYER_NONE -> the control is filtered
        // out of all HUD::Draw passes).
        ctrl->m_LayerFlags = (int)img.m_GroupMask;
        img.m_pHudCtrl    = ctrl;
        img.m_bAddedToHUD = true;
        img.m_CurrentVis  = 0.0f;

        if (hud) hud->AddControl(ctrl, false);
    }

    // Reset tint timers
    for (size_t i = 0; i < m_Tints.size(); ++i) {
        m_Tints[i].m_CurrentT = 0.0f;
    }
}

// ---- ScreenEffect::Update (binary @ 0x0011d664) ------------------------------

void ScreenEffect::Update(float dt, float currentLongest, float maxTotal) {
    // Standalone-mode override: when m_TotalDuration > 0, this effect manages
    // its own timeline rather than inheriting the PowerUp's.
    if (m_RemainingTime > 0.0f) {
        m_RemainingTime -= dt;
        // Override caller args with our own normalised time
        currentLongest = m_RemainingTime;
        maxTotal       = m_TotalDuration;
    }

    float tNorm = (maxTotal > 0.0f) ? (currentLongest / maxTotal) : 0.0f;

    // Per-image fade logic
    for (size_t i = 0; i < m_Images.size(); ++i) {
        EffectImage& img = m_Images[i];
        if (!img.m_pHudCtrl) continue;

        // Compute target visibility from startT/endT window
        bool inWindow = true;
        if (img.m_EndT > 0.0f && tNorm > img.m_EndT) inWindow = false;
        if (tNorm < img.m_StartT)                      inWindow = false;

        if (img.m_FadeRate > 0.0f) {
            // Fade-in / fade-out
            if (inWindow) {
                img.m_CurrentVis += img.m_FadeRate * dt;
                if (img.m_CurrentVis > 1.0f) img.m_CurrentVis = 1.0f;
            } else {
                img.m_CurrentVis -= img.m_FadeRate * dt;
                if (img.m_CurrentVis < 0.0f) img.m_CurrentVis = 0.0f;
            }
        } else {
            img.m_CurrentVis = inWindow ? 1.0f : 0.0f;
        }

        // Sine-wave oscillation on size
        img.m_SinIdx += (uint16_t)(img.m_Freq * 65536.0f * dt);
        float sinVal = SinIdx(img.m_SinIdx);

        // Lerp size between SizeIn and SizeOut using sinVal
        Vec3 sz;
        sz.x = img.m_SizeIn.x + (img.m_SizeOut.x - img.m_SizeIn.x) * (sinVal * img.m_Amp1 + img.m_Amp2);
        sz.y = img.m_SizeIn.y + (img.m_SizeOut.y - img.m_SizeIn.y) * (sinVal * img.m_Amp1 + img.m_Amp2);
        sz.z = img.m_SizeIn.z;

        img.m_pHudCtrl->size = sz;

        // Apply colour scale * tint alpha from visibility
        uint8_t alpha = (uint8_t)(img.m_CurrentVis * 255.0f);
        img.m_pHudCtrl->m_DrawColour = Colour(
            (uint8_t)(img.m_Tint.r * img.m_ColourScale.x),
            (uint8_t)(img.m_Tint.g * img.m_ColourScale.y),
            (uint8_t)(img.m_Tint.b * img.m_ColourScale.z),
            alpha
        );

        // Update position with velocity
        img.m_Pos.x += img.m_Vel.x * dt;
        img.m_Pos.y += img.m_Vel.y * dt;
        img.m_Pos.z += img.m_Vel.z * dt;
        img.m_pHudCtrl->pos = img.m_Pos;
    }

    // Per-tint colour multiply on HUD scales
    Game* game = Game::GetInstance();
    HUD*  hud  = game ? game_work.mHud : nullptr;
    if (hud) {
        for (size_t i = 0; i < m_Tints.size(); ++i) {
            ScreenTint& t = m_Tints[i];
            t.m_CurrentT += dt;

            float tval = 0.0f;
            if (t.m_Length > 0.0f)
                tval = Clamp(t.m_CurrentT / t.m_Length, 0.0f, 1.0f);

            // Fade-in ramp over m_FadeIn seconds
            float fade = 1.0f;
            if (t.m_FadeIn > 0.0f)
                fade = Clamp(t.m_CurrentT / t.m_FadeIn, 0.0f, 1.0f);

            Vec3 col;
            col.x = Lerp(t.m_ColourFrom.x, t.m_ColourTo.x, tval) * fade;
            col.y = Lerp(t.m_ColourFrom.y, t.m_ColourTo.y, tval) * fade;
            col.z = Lerp(t.m_ColourFrom.z, t.m_ColourTo.z, tval) * fade;

            // Multiply into HUD scales[0..2]
            hud->scales[0] *= col.x;
            hud->scales[1] *= col.y;
            hud->scales[2] *= col.z;
        }
    }

    // Cull emitters when effect near end (currentLongest >= 0.8 * maxTotal)
    if (maxTotal > 0.0f && currentLongest >= maxTotal * 0.8f) {
        PSPParticleManager& pm = PSPParticleManager::GetInstance();
        for (size_t i = 0; i < m_Emmiters.size(); ++i) {
            Emmiter& em = m_Emmiters[i];
            if (em.m_pHandle) {
                pm.ClearEmitter(em.m_pHandle);
                em.m_pHandle = nullptr;
            }
        }
    }

    // Scheduled SFX firing
    if (m_TotalDuration > 0.0f) {
        float elapsed = m_TotalDuration - m_RemainingTime;
        Game* g = Game::GetInstance();
        GameSound* gs = g ? game_work.mGameSound : nullptr;

        for (int si = (int)m_Sounds.size() - 1; si >= 0; --si) {
            SoundEffect& sfx = m_Sounds[si];
            if (!sfx.m_VoiceHandle && elapsed >= sfx.m_StartT) {
                if (gs) sfx.m_VoiceHandle = gs->SFXPlay(sfx.m_SoundName, 1.0f, 1.0f);
                // erase after firing
                m_Sounds.erase(m_Sounds.begin() + si);
            }
        }
    }
}

// ---- ScreenEffect::Deactivate (binary @ 0x0011d43c) --------------------------

void ScreenEffect::Deactivate() {
    PSPParticleManager& pm = PSPParticleManager::GetInstance();
    for (size_t i = 0; i < m_Emmiters.size(); ++i) {
        Emmiter& em = m_Emmiters[i];
        if (em.m_pHandle) {
            pm.ClearEmitter(em.m_pHandle);
            em.m_pHandle = nullptr;
        }
    }

    for (size_t i = 0; i < m_Images.size(); ++i) {
        EffectImage& img = m_Images[i];
        if (img.m_pHudCtrl) {
            // TODO: if m_bIsMultiplyerBoard, trigger dismiss animation instead.
            img.m_pHudCtrl->m_bPendingRemoval = 1;
            img.m_pHudCtrl  = nullptr;
            img.m_bAddedToHUD = false;
        }
    }

    m_Emmiters.clear();
    m_Images.clear();
    m_Tints.clear();
    m_Sounds.clear();
}

// ---- ScreenEffect::LoadTextures (binary @ 0x0011d1ec) -------------------------

void ScreenEffect::LoadTextures() {
    for (size_t i = 0; i < m_Images.size(); ++i)
        m_Images[i].LoadTextures();
}
