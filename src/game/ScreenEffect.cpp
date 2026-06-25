#include "ScreenEffect.h"
#include "PowerUp.h"
#include "util/StringHash.h"
#include "math/MathUtil.h"
#include "particle/PSPParticleManager.h"
#include "hud/HUDControl3d.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "audio/GameSound.h"
#include "engine/asset/TextureManager.h"
#include "engine/asset/Texture.h"
#include "Game.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include "game/GameWork.h"

using namespace Mortar;

// Binary @ 0x0011e150 area — check particle hardware flag.
// Port: always return true (all hardware considered "fast").
static bool IsFastHardware() {
    Game* g = Game::GetInstance();
    if (g) return g->IsFastHardware();
    return true;
}

// ---- Emmiter::Parse (binary @ 0x0011e150 child "emmiter" block) ---------------

void Emmiter::Parse(TiXmlElement* xml) {
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

// ---- ParseVector: read "x,y,z" (comma-separated) into Vec3 -------------------
// v1.6.1 EffectImage::Parse @0x001491e4 uses comma-separated vector parsing.

static Vec3 ParseVector(const char* s) {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (s) sscanf(s, "%f,%f,%f", &x, &y, &z);
    return Vec3(x, y, z);
}

// ---- ParseColour: read "r g b a" (space-separated) into Colour ---------------

static Colour ParseColour(const char* s) {
    int r = 255, g = 255, b = 255, a = 255;
    if (s) sscanf(s, "%d %d %d %d", &r, &g, &b, &a);
    return Colour((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a);
}

// ---- ParseMaskWords: comma-split token match -> OR (1<<idx) -------------------
// v1.6.1 ParseMaskWords @0x0014f404: splits str on commas, matches each token
// against the provided word hashes; sets bit (1<<matchIndex) in the result.
// Tokens: index 0 = "scale" (bit 0 = 1), index 1 = "fade" (bit 1 = 2).

static uint32_t ParseMaskWords(const char* str) {
    if (!str || !*str) return 0u;
    static const uint32_t kHashScale = StringHash("scale");
    static const uint32_t kHashFade  = StringHash("fade");
    uint32_t bits = 0u;
    char buf[64];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* tok = buf;
    while (tok && *tok) {
        char* comma = strchr(tok, ',');
        if (comma) *comma = '\0';
        // trim leading spaces
        while (*tok == ' ') ++tok;
        uint32_t h = StringHash(tok);
        if (h == kHashScale) bits |= 1u;
        if (h == kHashFade)  bits |= 2u;
        tok = comma ? (comma + 1) : NULL;
    }
    return bits;
}

// ---- EffectImage::Parse @0x001491e4 ------------------------------------------

void EffectImage::Parse(TiXmlElement* xml) {
    if (!xml) return;

    // "pos" -> m_Pos (comma-separated Vec3)
    const char* pos = xml->Attribute("pos");
    if (pos) m_Pos = ParseVector(pos);

    // "anchor" -> m_Vel (comma-separated Vec3)
    const char* anchor = xml->Attribute("anchor");
    if (anchor) m_Vel = ParseVector(anchor);

    // "transitionMove" -> both m_SizeIn and m_SizeOut
    const char* transMove = xml->Attribute("transitionMove");
    if (transMove) {
        Vec3 v = ParseVector(transMove);
        m_SizeIn  = v;
        m_SizeOut = v;
    }
    // "transitionMoveIn" overrides m_SizeIn; "transitionMoveOut" overrides m_SizeOut
    const char* moveIn = xml->Attribute("transitionMoveIn");
    if (moveIn) m_SizeIn = ParseVector(moveIn);
    const char* moveOut = xml->Attribute("transitionMoveOut");
    if (moveOut) {
        m_SizeOut = ParseVector(moveOut);
#if !defined(__bada__)
        m_VelOut = m_SizeOut;
#endif
    }

    // "texture" -> load texture; set m_ColourScale = (texWidth, texHeight, 0)
    // v1.6.1 EffectImage::Parse @0x001491e4: VectorUnsignedToFloat from tex+0x24/+0x28
    const char* tex = xml->Attribute("texture");
    if (tex) {
#if !defined(__bada__)
        strncpy(m_TexName, tex, sizeof(m_TexName) - 1);
        m_TexName[sizeof(m_TexName) - 1] = '\0';
        char texPath[80];
        snprintf(texPath, sizeof(texPath), "%s.tex", m_TexName);
        Mortar::SmartPtr<Mortar::Texture> loaded =
            Mortar::TextureManager::LoadLocalisedTexture(texPath);
        if (loaded.IsValid()) {
            m_Texture    = loaded;
            m_ColourScale = Vec3((float)loaded->GetWidth(),
                                 (float)loaded->GetHeight(),
                                 0.0f);
        }
#endif
    }

    // "scale" -> m_ColourScale (explicit override; binary *=1.0 no-op after)
    const char* scale = xml->Attribute("scale");
    if (scale) m_ColourScale = ParseVector(scale);

    // "slowHardwareScale" -> float; if !IsFastHardware() multiply into m_ColourScale
    const char* slowScale = xml->Attribute("slowHardwareScale");
    if (slowScale) {
        float ss = (float)atof(slowScale);
        if (!IsFastHardware()) {
            m_ColourScale.x *= ss;
            m_ColourScale.y *= ss;
            m_ColourScale.z *= ss;
        }
    }

    // "pulseSpeed" -> m_Freq
    const char* pulseSpeed = xml->Attribute("pulseSpeed");
    if (pulseSpeed) m_Freq = (float)atof(pulseSpeed);

    // "pulseScale" -> m_Amp1, copy to m_Amp2
    const char* pulseScale = xml->Attribute("pulseScale");
    if (pulseScale) { m_Amp1 = (float)atof(pulseScale); m_Amp2 = m_Amp1; }

    // "pulseScalePositive" -> m_Amp1 ; "pulseScaleNegative" -> m_Amp2
    const char* pulsePos = xml->Attribute("pulseScalePositive");
    if (pulsePos) m_Amp1 = (float)atof(pulsePos);
    const char* pulseNeg = xml->Attribute("pulseScaleNegative");
    if (pulseNeg) m_Amp2 = (float)atof(pulseNeg);

    // "transitionTime" -> m_FadeRate
    const char* transitionTime = xml->Attribute("transitionTime");
    if (transitionTime) m_FadeRate = (float)atof(transitionTime);

    // "timeStart" -> m_StartT ; "timeEnd" -> m_EndT
    const char* timeStart = xml->Attribute("timeStart");
    if (timeStart) m_StartT = (float)atof(timeStart);
    const char* timeEnd = xml->Attribute("timeEnd");
    if (timeEnd) m_EndT = (float)atof(timeEnd);

    // "colour" -> m_Tint (RGBA space-separated)
    const char* colour = xml->Attribute("colour");
    if (colour) m_Tint = ParseColour(colour);

    // "transition" -> m_FlagBits via ParseMaskWords (bit0="scale", bit1="fade")
    // v1.6.1 ParseMaskWords @0x0014f404
    const char* transition = xml->Attribute("transition");
    if (transition) m_FlagBits = ParseMaskWords(transition);

    // "drawOrder" -> m_GroupMask (kept from commit 1af02f65)
    {
        const char* drawOrder = xml->Attribute("drawOrder");
        if (!drawOrder) {
            m_GroupMask = HUD_LAYER_DEFAULT;
        } else if (strcmp(drawOrder, "none") == 0) {
            m_GroupMask = HUD_LAYER_NONE;
        } else if (strcmp(drawOrder, "normal") == 0) {
            m_GroupMask = HUD_LAYER_DEFAULT;
        } else if (strcmp(drawOrder, "post") == 0) {
            m_GroupMask = HUD_LAYER_BUTTONS;
        } else if (strcmp(drawOrder, "before_splats") == 0) {
            m_GroupMask = HUD_LAYER_MENU_BG;
        } else if (strcmp(drawOrder, "after_splats") == 0) {
            m_GroupMask = HUD_LAYER_POST_ACTOR;
        } else if (strcmp(drawOrder, "before_bomb") == 0) {
            m_GroupMask = HUD_LAYER_P2_SCORE;
        } else if (strcmp(drawOrder, "after_bomb") == 0) {
            m_GroupMask = HUD_LAYER_SLIDER;
        } else if (strcmp(drawOrder, "top_most") == 0) {
            m_GroupMask = HUD_LAYER_TOP_MOST;
        } else {
            m_GroupMask = HUD_LAYER_DEFAULT;
        }
    }

    // "deferPoints" / "defer" -> m_bIsMultiplyerBoard
    // v1.6.1 EffectImage::Parse @0x001491e4
    const char* defer = xml->Attribute("deferPoints");
    if (!defer) defer = xml->Attribute("defer");
    if (defer) m_bIsMultiplyerBoard = (strcmp(defer, "true") == 0 || strcmp(defer, "1") == 0);

    // "scaleToScreen" -> m_bLowEndOnly
    // v1.6.1 EffectImage::Parse @0x001491e4
    const char* scaleToScreen = xml->Attribute("scaleToScreen");
    if (scaleToScreen) m_bLowEndOnly = (strcmp(scaleToScreen, "true") == 0);
}

void EffectImage::LoadTextures() {
    // Binary @ 0x0011d1e4 trampolines to ReloadableTexture::Load (0x001213b8)
    // which calls TextureManager::LoadLocalisedTexture("<m_pName>.tex"). XML
    // attribute values omit the .tex suffix (e.g. `texture="arcade_60seconds"`
    // -> file `arcade_60seconds.tex`). Mirror the binary's name+".tex" append.
    // Parse already loads and sets m_ColourScale from texture dims; this is a
    // reload path used when the texture cache is flushed and restored.
#if !defined(__bada__)
    if (m_TexName[0] == '\0') return;
    if (m_Texture.IsValid()) return;  // already loaded (Parse loads on first call)
    char texPath[80];
    snprintf(texPath, sizeof(texPath), "%s.tex", m_TexName);
    Mortar::SmartPtr<Mortar::Texture> loaded =
        Mortar::TextureManager::LoadLocalisedTexture(texPath);
    if (loaded.IsValid()) {
        m_Texture     = loaded;
        // Restore m_ColourScale dims if they were zeroed after an unload
        if (m_ColourScale.x == 0.0f && m_ColourScale.y == 0.0f) {
            m_ColourScale = Vec3((float)loaded->GetWidth(),
                                 (float)loaded->GetHeight(),
                                 0.0f);
        }
    }
#endif
}

// ---- ScreenTint::Parse -------------------------------------------------------

void ScreenTint::Parse(TiXmlElement* xml) {
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

void SoundEffect::Parse(TiXmlElement* xml) {
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

// Binary @ 0x0011d5a0 -- "compact push/pop" = just member destruction.
// Deactivate() is called by external callers (PowerUpManager) before destruction;
// the dtor trusts member destructors for std::vector and SmartPtr cleanup.
// ASM-spec v1.6.1 ScreenEffect::~ScreenEffect @ 0x0011d5a0
ScreenEffect::~ScreenEffect() {
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

void ScreenEffect::Parse(TiXmlElement* xml) {
    const char* name = xml->Attribute("name");
    if (!name) name = "void";
    strcpy(m_Name, name);
    m_NameHash = StringHash(m_Name);

    xml->QueryFloatAttribute("length", &m_TotalDuration);
    if (m_TotalDuration > 0.0f)
        m_RemainingTime = m_TotalDuration;

    for (TiXmlElement child = xml->FirstChildElement();
         child; child = child.NextSiblingElement()) {
        const char* tag = child.Name();

        // Hardware filter — "fast" = IsFastHardware() must be true;
        // "slow" = IsFastHardware() must be false.
        const char* hw = child.Attribute("hardware");
        if (!hw || ((strcmp(hw, "fast") != 0 || IsFastHardware()) &&
                    (strcmp(hw, "slow") != 0 || !IsFastHardware()))) {
            if (strcmp(tag, "image") == 0) {
                EffectImage img;
                img.Parse(&child);
                m_Images.push_back(img);
            } else if (strcmp(tag, "emmiter") == 0) {
                Emmiter em;
                em.Parse(&child);
                m_Emmiters.push_back(em);
            } else if (strcmp(tag, "tint") == 0) {
                ScreenTint tint;
                tint.Parse(&child);
                m_Tints.push_back(tint);
            } else if (strcmp(tag, "sound") == 0) {
                SoundEffect sfx;
                sfx.Parse(&child);
                m_Sounds.push_back(sfx);
            }
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
        // Size comes from m_ColourScale (= texture dims written by Parse);
        // NOT from m_SizeIn (which is the slide-move offset).
        // v1.6.1 ScreenEffect::Update @0x00148844 writes ctrl->size from m_ColourScale each frame.
        ctrl->size = img.m_ColourScale;
        ctrl->m_DrawColour = img.m_Tint;
        // v1.6.1 ScreenEffect::Activate @0x00148f08: for alpha-driven images
        // (m_FlagBits & 2) the binary zeroes the control's alpha byte
        // (HUDControl+0x5f = m_DrawColour.a) BEFORE AddControl, so the control is
        // invisible on the activation frame until the first Update computes alpha
        // from m_CurrentVis. Without this both ready_set_go textures draw full for
        // one frame (the activation flash).
        if (img.m_FlagBits & 2u) {
            ctrl->m_DrawColour.a = 0;
        }
        // Texture assignment per binary @ 0x0011dd2e onwards: the loaded
        // ReloadableTexture's SmartPtr is copied into HUDControl3d.m_Texture
        // (at HUDControl3d+0x74). Without this the spawned HUD control has
        // no texture and HUDControl3d::Draw's `if (m_Texture)` gate skips
        // rendering -- which is the root cause of arcade_60seconds /
        // arcade_go / blitz_1..6 / ice_cover never appearing on screen.
#if !defined(__bada__)
        ctrl->m_Texture = img.m_Texture;
#endif
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

// ---- ScreenEffect::Update (binary @ 0x00148844) ------------------------------

void ScreenEffect::Update(float dt, float currentLongest, float maxTotal) {
    // Standalone-mode override: when m_TotalDuration > 0, this effect manages
    // its own timeline rather than inheriting the PowerUp's.
    if (m_RemainingTime > 0.0f) {
        m_RemainingTime -= dt;
        currentLongest = m_RemainingTime;
        maxTotal       = m_TotalDuration;
    }

    // Per-image fade + slide + size logic
    // v1.6.1 ScreenEffect::Update @0x00148844
    for (size_t i = 0; i < m_Images.size(); ++i) {
        EffectImage& img = m_Images[i];
        if (!img.m_pHudCtrl) continue;

        float wStart = maxTotal * img.m_StartT;
        float wEnd   = maxTotal * img.m_EndT;

        bool useMoveIn = false;

        // v1.6.1 ScreenEffect::Update @0x00148844: currentLongest counts DOWN;
        // window (wEnd,wStart], m_StartT>m_EndT.
        if (img.m_FadeRate <= 0.0f) {
            // @0x00148960: hard on/off: ON iff wEnd <= currentLongest <= wStart
            bool on = (currentLongest >= wEnd && currentLongest <= wStart);
            img.m_CurrentVis = on ? 1.0f : 0.0f;
            useMoveIn = on;
        } else if (currentLongest > wStart) {
            // @0x001488f4: not started yet
            img.m_CurrentVis = 0.0f;
            useMoveIn = true;
        } else if (currentLongest <= wEnd + img.m_FadeRate) {
            // @0x0014892c: fade-OUT (uses m_SizeOut)
            float r = (currentLongest - wEnd) / img.m_FadeRate;
            if (r < 0.0f) r = 0.0f; else if (r > 1.0f) r = 1.0f;
            img.m_CurrentVis = r;
            useMoveIn = false;
        } else {
            // @0x0014890c: fade-IN (wEnd+fade < currentLongest <= wStart; uses m_SizeIn)
            img.m_CurrentVis += dt / img.m_FadeRate;
            if (img.m_CurrentVis > 1.0f) img.m_CurrentVis = 1.0f;
            useMoveIn = true;
        }

        // Ease: e = (1 - vis)^2
        float e = 1.0f - img.m_CurrentVis;
        e = e * e;

        // ctrl.pos = m_Pos * (480,320,0) + move_offset * e
        // m_Pos is in normalised [0..1] space; multiply by screen dims
        // v1.6.1 @0x00148844: pos = m_Pos*(480,320,0) + (useMoveIn?m_SizeIn:m_SizeOut)*e
        const Vec3& moveOffset = useMoveIn ? img.m_SizeIn : img.m_SizeOut;
        img.m_pHudCtrl->pos.x = img.m_Pos.x * 480.0f + moveOffset.x * e;
        img.m_pHudCtrl->pos.y = img.m_Pos.y * 320.0f + moveOffset.y * e;
        img.m_pHudCtrl->pos.z = img.m_Pos.z * 0.0f   + moveOffset.z * e;

        // Size = m_ColourScale * ((m_FlagBits & 1) ? (1 - e) : 1.0f)
        // m_ColourScale holds texture dims (written by Parse)
        float scaleFactor = (img.m_FlagBits & 1u) ? (1.0f - e) : 1.0f;
        Vec3 sz;
        sz.x = img.m_ColourScale.x * scaleFactor;
        sz.y = img.m_ColourScale.y * scaleFactor;
        sz.z = img.m_ColourScale.z * scaleFactor;

        // Pulse oscillation on size
        // ASM-verified: 2026-06-24T00:00Z v1.6.1 ScreenEffect::Update @ 0x00148adc (asm-inspector)
        //   m_SinIdx += dt*32760.0f*m_Freq; DAT_00148ca4 = 0x46fff000 = 32760.0f
        img.m_SinIdx += (uint16_t)(img.m_Freq * 32760.0f * dt);
        float sinVal = SinIdx(img.m_SinIdx);
        float amp = (sinVal < 0.0f) ? img.m_Amp2 : img.m_Amp1;
        float pulse = sinVal * amp + 1.0f;
        sz.x *= pulse;
        sz.y *= pulse;
        sz.z *= pulse;

        img.m_pHudCtrl->size = sz;

        // Alpha: fade bit (bit1=2) -> alpha * m_CurrentVis, else full tint alpha
        // v1.6.1 @0x00148844
        float alpha = (img.m_FlagBits & 2u)
            ? ((float)img.m_Tint.a * img.m_CurrentVis)
            : (float)img.m_Tint.a;
        if (alpha < 0.0f) alpha = 0.0f;
        // TODO: v1.6.1 @0x00148844 -- binary reads hud->alpha (HUD+0x24) and multiplies
        //   into alpha here. Port's HUD+0x24 = m_globalTimeScale (slow-motion; not a
        //   global alpha). Semantic mismatch -- skip multiplication until field is RE'd.
        img.m_pHudCtrl->m_DrawColour = Colour(
            img.m_Tint.r,
            img.m_Tint.g,
            img.m_Tint.b,
            (uint8_t)alpha
        );
    }

    // Per-tint colour multiply on HUD scales
    HUD* hud = game_work.mHud;
    for (size_t i = 0; i < m_Tints.size(); ++i) {
        ScreenTint& t = m_Tints[i];
        t.m_CurrentT += dt;

        float tval = 0.0f;
        if (t.m_Length > 0.0f)
            tval = Clamp(t.m_CurrentT / t.m_Length, 0.0f, 1.0f);

        float fade = 1.0f;
        if (t.m_FadeIn > 0.0f)
            fade = Clamp(t.m_CurrentT / t.m_FadeIn, 0.0f, 1.0f);

        Vec3 col;
        col.x = Lerp(t.m_ColourFrom.x, t.m_ColourTo.x, tval) * fade;
        col.y = Lerp(t.m_ColourFrom.y, t.m_ColourTo.y, tval) * fade;
        col.z = Lerp(t.m_ColourFrom.z, t.m_ColourTo.z, tval) * fade;

        hud->scales[0] *= col.x;
        hud->scales[1] *= col.y;
        hud->scales[2] *= col.z;
    }

    // v1.6.1 @0x00148d24: when remaining time < 0.8f, halt emitter spawning.
    for (size_t i = 0; i < m_Emmiters.size(); ++i) {
        Emmiter& em = m_Emmiters[i];
        if (em.m_pHandle && currentLongest < 0.8f) {
            em.m_pHandle->m_RateScale = 0.0f;
        }
    }

    // v1.6.1 @0x00148d84: fire SFX when remaining time crosses m_StartT threshold.
    {
        GameSound* gs = game_work.mGameSound;

        for (size_t si = 0; si < m_Sounds.size(); ++si) {
            SoundEffect& sfx = m_Sounds[si];
            if (currentLongest > maxTotal * sfx.m_StartT) continue;
            sfx.m_StartT = 100.0f;
            if (!sfx.m_VoiceHandle) {
                Mortar::Delegate1<bool, Mortar::MortarSound*> emptyDelegate;
                sfx.m_VoiceHandle = gs->SFXPlay(sfx.m_SoundName, 0.6599f, 1.0f, emptyDelegate);
            }
        }
        // TODO: 0x00148e3c — binary also: (a) while (maxTotal*m_EndT <= currentLongest) erases the
        //   record, and (b) a separate m_EndT<0 one-shot path (0x148da4) plays at vol 1.0 without
        //   storing a handle. Port once the loop erase semantics are confirmed.
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
