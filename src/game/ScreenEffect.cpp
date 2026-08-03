#include "ScreenEffect.h"
#include "PowerUp.h"
#include "util/StringHash.h"
#include "math/MathUtil.h"
#include "particle/PSPParticleManager.h"
#include "hud/HUDControl3d.h"
#include "hud/ScoreMultiplyerBoard.h"
#include "hud/TimeSinkControl.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "audio/GameSound.h"
#include "engine/asset/TextureManager.h"
#include "engine/asset/Texture.h"
#include "Game.h"
#include "engine/util/AsciiString.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include "game/GameWork.h"
#include "engine/render/Layout.h"

using namespace Mortar;

// Particle hardware flag, read by ScreenEffect::Parse (v1.6.1 @0x00149800 area).
// ASM-spec v1.6.1 IsFastHardware @0x0011f394: loads the MortarGame singleton from
// the GOT and tail-dispatches vtable slot +0xc (Mortar::MortarGame::IsFastHardware
// @0x0011fb88 = `ldrb r0,[r0,#0xf4]`). No null test.
static bool IsFastHardware() {
    return Game::GetInstance()->IsFastHardware();
}

// ---- Emmiter::Parse (v1.6.1 Emmiter::Parse @0x00148458) ----------------------

void Emmiter::Parse(TiXmlElement* xml) {
    if (!xml) return;
    // v1.6.1 Emmiter::Parse @0x00148458: "pos" -> m_Offset(+0x08),
    //   "anchor" -> m_VelocityScale(+0x14), "particle" -> m_NameHash(+0x00).
    // ParseVector null-guards internally and yields (0,0,0), matching the
    // binary's unconditional writes -- call it unconditionally rather than
    // gating on Attribute() to stay byte-faithful (see screeneffect-frenzy-re.md).
    m_Offset         = ParseVector(xml->Attribute("pos"));
    m_VelocityScale  = ParseVector(xml->Attribute("anchor"));

    const char* particle = xml->Attribute("particle");
    if (particle) m_NameHash = StringHash(particle);
}

// ---- ParseVector: read "x,y,z" (comma-separated) into Vec3 -------------------
// v1.6.1 EffectImage::Parse @0x001491e4 uses comma-separated vector parsing.
// Binary: _Z11ParseVectorPKc

_Vector3<float> ParseVector(const char* s)
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (s) sscanf(s, "%f,%f,%f", &x, &y, &z);
    return _Vector3<float>(x, y, z);
}

// ---- ParseColour: read "r g b a" (space-separated) into Colour ---------------

static Colour ParseColour(const char* s) {
    int r = 255, g = 255, b = 255, a = 255;
    if (s) sscanf(s, "%d %d %d %d", &r, &g, &b, &a);
    return Colour((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a);
}

// ---- ParseMaskWords: comma-split token match -> OR (1<<idx) -------------------
// ASM-spec v1.6.1 ParseMaskWords @0x0014f404 (_Z14ParseMaskWordsPKcPmi)
// Generic helper: comma-split str; for each trimmed token hash it and scan
// wordHashes[count]; on match OR (1<<idx) into the result bitmask.

uint32_t ParseMaskWords(const char* str, unsigned long* wordHashes, int count) {
    if (!str || !*str) return 0u;
    uint32_t bits = 0u;
    char buf[64];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* tok = buf;
    while (tok && *tok) {
        char* comma = strchr(tok, ',');
        if (comma) *comma = '\0';
        while (*tok == ' ') ++tok;
        unsigned long h = (unsigned long)StringHash(tok);
        for (int i = 0; i < count; i++) {
            if (wordHashes[i] == h) {
                bits |= (1u << (unsigned)i);
                break;
            }
        }
        tok = comma ? (comma + 1) : NULL;
    }
    return bits;
}

// Specialization for EffectImage::Parse: hard-coded "scale"(bit 0) / "fade"(bit 1).
// Rewired to call the generic ParseMaskWords above (identical algorithm, same result).
static uint32_t ParseMaskWords(const char* str) {
    static unsigned long kWordHashes[2] = {
        (unsigned long)StringHash("scale"),
        (unsigned long)StringHash("fade")
    };
    return ParseMaskWords(str, kWordHashes, 2);
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
        _Vector3<float> v = ParseVector(transMove);
        m_SizeIn  = v;
        m_SizeOut = v;
    }
    // "transitionMoveIn" overrides m_SizeIn; "transitionMoveOut" overrides m_SizeOut
    const char* moveIn = xml->Attribute("transitionMoveIn");
    if (moveIn) m_SizeIn = ParseVector(moveIn);
    const char* moveOut = xml->Attribute("transitionMoveOut");
    if (moveOut) {
        m_SizeOut = ParseVector(moveOut);
    }

    // "texture" -> load texture; set m_ColourScale = (texWidth, texHeight, 0)
    // v1.6.1 EffectImage::Parse @0x001491e4: VectorUnsignedToFloat from tex+0x24/+0x28
    // DIFFERS: original ReloadableTexture::operator=(const char*) (base class,
    //   @0x0014f7fc) does NOT load -- the base Parse only stashes m_pPath, so a
    //   binary-faithful Parse would leave m_ColourScale at (0,0,0) here and defer
    //   the texture-dims read to LoadTextures(). This port loads eagerly (needed
    //   so m_ColourScale is populated before the first Activate/Update); kept as
    //   an intentional behavioural deviation, not a fidelity bug -- do not "fix"
    //   without re-verifying whether LoadTextures() runs before first use.
    const char* tex = xml->Attribute("texture");
    if (tex) {
        // Qualified call, not `*this = tex`: EffectImage's implicit copy-assignment
        // operator= (const EffectImage&) hides ALL base operator= overloads
        // (ordinary C++ member-hiding rules), so an unqualified `*this = tex`
        // would try to construct a temporary EffectImage from const char* and
        // fail to compile. Calling the base overload by qualified name bypasses
        // the hiding and performs the real deep-copy-into-m_pPath assignment.
        ReloadableTexture::operator=(tex);
        char texPath[80];
        snprintf(texPath, sizeof(texPath), "%s.tex", m_pPath);
        Mortar::SmartPtr<Mortar::Texture> loaded =
            Mortar::TextureManager::LoadLocalisedTexture(texPath);
        if (loaded.IsValid()) {
            m_Texture    = loaded;
            m_ColourScale = _Vector3<float>((float)loaded->GetWidth(),
                                            (float)loaded->GetHeight(),
                                            0.0f);
        }
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
        } else if (strcmp(drawOrder, "post_post") == 0) {
            m_GroupMask = HUD_LAYER_FADE_MODAL;
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

    // "deferPoints"/"defer" -> m_DeferKind (0=none,1=points,2=time)
    // v1.6.1 EffectImage::Parse @0x001491e4
    const char* deferPoints = xml->Attribute("deferPoints");
    if (deferPoints && strcmp(deferPoints, "true") == 0) {
        m_DeferKind = 1;
    } else {
        const char* defer = xml->Attribute("defer");
        if (defer) {
            if (strcmp(defer, "none") == 0)        m_DeferKind = 0;
            else if (strcmp(defer, "points") == 0) m_DeferKind = 1;
            else if (strcmp(defer, "time") == 0)   m_DeferKind = 2;
        }
    }

    // "scaleToScreen" -> m_bScaleToScreen
    // v1.6.1 EffectImage::Parse @0x001491e4
    const char* scaleToScreen = xml->Attribute("scaleToScreen");
    if (scaleToScreen) m_bScaleToScreen = (strcmp(scaleToScreen, "true") == 0);
}

// ASM-spec v1.6.1 EffectImage::LoadTextures @0x001481d0: single-instruction tail-call thunk
// b -> ReloadableTexture::Load @0x0014fad8. All path building ("%s.tex", buf[64]) is in
// global ::LoadTexture @0x0014f88c, not here.
void EffectImage::LoadTextures() {
    ReloadableTexture::Load();
}

// ---- ScreenTint::Parse (v1.6.1 ScreenTint::Parse @0x00148324) ----------------

void ScreenTint::Parse(TiXmlElement* xml) {
    if (!xml) return;

    xml->QueryFloatAttribute("timeStart", &m_TimeStart);
    xml->QueryFloatAttribute("timeEnd",   &m_TimeEnd);

    // "tint" sets BOTH back+hud tint; backTint/hudTint then override independently.
    ParseFloats(xml->Attribute("tint"), &m_BackTint.x, 3);
    m_HudTint = m_BackTint;
    ParseFloats(xml->Attribute("backTint"), &m_BackTint.x, 3);
    ParseFloats(xml->Attribute("hudTint"),  &m_HudTint.x, 3);

    xml->QueryFloatAttribute("transitionTime", &m_TransitionTime);
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
    // v1.6.1 EffectImage::EffectImage(const&) @0x00145bd4: m_pHudCtrl (+0x08) and
    // m_bAddedToHUD (+0x0c) are copied VERBATIM, not nulled -- PowerUpManager::
    // ActivateScreenEffect Activate()s a temp then push_back()s it into the live
    // list; if this copy nulled the control pointer, the surviving list element
    // would never see the HUD control and ScreenEffect::Update's
    // `if (!img.m_pHudCtrl) continue;` would skip it forever (blitz_1..6 never
    // drawing). Ownership is fine to preserve: EffectImage has no dtor and nothing
    // ever calls delete on m_pHudCtrl directly -- Deactivate() sets
    // m_bPendingRemoval and lets the HUD self-remove the control.
    // Sound handles are not copied
    for (size_t i = 0; i < m_Sounds.size(); ++i)
        m_Sounds[i].m_VoiceHandle = nullptr;
}

// ASM-spec v1.6.1 ScreenEffect::~ScreenEffect @0x00148728: compiler-generated
//   member teardown ONLY — ~vector<SoundEffect> (this+0x24, @0x0014873c),
//   ~vector<ScreenTint> (this+0x18, @0x00148760), ~vector<EffectImage>
//   (this+0x0c, @0x00148768; element dtor @0x00145ca4 is solely
//   ~ReloadableTexture: delete[] m_pPath + SmartPtr<Texture> release), then
//   ~vector<Emmiter> (this+0x00, @0x00148788) — i.e. reverse declaration order,
//   exactly what an empty body emits implicitly. NO Deactivate call, NO HUD or
//   emitter or voice release, NO game_work.pM_pHud access. Live handles are
//   released by Deactivate(), which external callers (PowerUpManager) invoke
//   before destruction; if they don't, the binary leaks them too. Deleting
//   variant D0 is the sibling @0x00148794.
//   The empty body below is therefore already faithful — the 108-byte figure is
//   four vector dtor calls plus the literal pool, not unported logic.
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
        // v1.6.1 EffectImage::EffectImage(const&) @0x00145bd4: m_pHudCtrl/
        // m_bAddedToHUD copied verbatim -- see copy-ctor comment above.
        for (size_t i = 0; i < m_Sounds.size(); ++i)
            m_Sounds[i].m_VoiceHandle = nullptr;
    }
    return *this;
}

// ---- ScreenEffect::Parse (v1.6.1 ScreenEffect::Parse @0x00149800) ------------

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

// ---- ScreenEffect::Activate (binary @ 0x00148f08) ----------------------------

void ScreenEffect::Activate() {
    PSPParticleManager& pm = PSPParticleManager::GetInstance();

    // Spawn particle emitters
    // v1.6.1 ScreenEffect::Activate @0x00148f08
    // DIFFERS: opt-in widescreen (Layout::HalfWidth); faithful 240 under __bada__ --
    // kScreenAnchor.x is the binary's 480.0f (= 2*240, screen full-width) used to
    // scale an emitter's "anchor" (m_VelocityScale) attribute into a spawn position.
    // frenzy's two "speed" emitters use anchor=+-0.5 to sit at the LEFT/RIGHT screen
    // edges (480*0.5=240=+HalfWidth, 480*-0.5=-240=-HalfWidth); widen that edge term
    // by k so they track +-HalfWidth() in widescreen instead of leaving the widened
    // sides empty. A centre emitter (anchor.x==0) is unaffected (k-scale of 0 is 0).
    // k==1.0f under __bada__, so this is identity there.
#ifdef __bada__
    const float k = 1.0f;
#else
    const float k = Layout::HalfWidth() / 240.0f;
#endif
    const _Vector3<float> kScreenAnchor(480.0f * k, 320.0f, 0.0f);
    for (size_t i = 0; i < m_Emmiters.size(); ++i) {
        Emmiter& em = m_Emmiters[i];
        // Only spawn if not already active (m_pHandle == nullptr = not running)
        if (!em.m_pHandle) {
            // @0x00148f34: EmitterExists gate -- misleadingly named; nonzero means
            // "template exists, go add it". Skip AddEmitter entirely if it's 0.
            if (!pm.EmitterExists(em.m_NameHash)) continue;
            pm.AddEmitter(em.m_NameHash, &em.m_pHandle, false);
            if (em.m_pHandle) {
                // @0x00148f68-b4: single m_Pos write, no m_Vel write here.
                em.m_pHandle->m_Pos = kScreenAnchor * em.m_VelocityScale + em.m_Offset;
                // @0x00148fb8-c8: drop a one-shot emitter that already ended.
                if (PSPParticleManager::EmitterEnds(em.m_pHandle->m_pTemplate)) {
                    em.m_pHandle = nullptr;
                }
            }
        }
    }

    // Create HUD controls for images
    Game* game = Game::GetInstance();
    HUD* hud   = game ? game_work.mHud : nullptr;

    for (size_t i = 0; i < m_Images.size(); ++i) {
        EffectImage& img = m_Images[i];

        // v1.6.1 ScreenEffect::Activate @0x00148f08: binary @0x0014900c has NO
        // entry guard here -- it unconditionally creates a control per image.
        // The removed `if (img.m_bAddedToHUD) continue;` was a port-invented
        // gate: EffectImage's default ctor sets m_bAddedToHUD=true, so every
        // freshly-parsed image skipped this loop entirely and nothing ever
        // drew. See tmp/asm-verify/screeneffect-pipeline-re.md.

        // v1.6.1 ScreenEffect::Activate @0x00148f08: dispatch on m_DeferKind --
        // 1="points" -> ScoreMultiplyerBoard (Arcade x2 deferred-points board);
        // 2="time" -> TimeSinkControl (Berry-Blast time-defer board);
        // 0="none" -> plain HUDControl3d.
        HUDControl3d* ctrl;
        if (img.m_DeferKind == 1) {
            ScoreMultiplyerBoard* board = new ScoreMultiplyerBoard();
            board->m_pOwner = m_pOwnerPowerUp;
            ctrl = board;
        } else if (img.m_DeferKind == 2) {
            TimeSinkControl* sink = new TimeSinkControl();
            sink->m_pPowerUp = m_pOwnerPowerUp;
            ctrl = sink;
        } else {
            ctrl = new HUDControl3d();
        }
        // pos offset (480, 320, 0) added internally by HUDControl3d::Draw
        ctrl->pos = img.m_Pos;
        // Size comes from m_ColourScale (= texture dims written by Parse);
        // NOT from m_SizeIn (which is the slide-move offset).
        // v1.6.1 ScreenEffect::Update @0x00148844 writes ctrl->size from m_ColourScale each frame.
        // @0x00149148: activation-frame only -- zero size when transition bit0
        // ("scale") is set, since Update's first tick computes size from
        // m_ColourScale*(1-e) anyway; otherwise start at the full texture size.
        ctrl->size = (img.m_FlagBits & 1u) ? _Vector3<float>(0.0f, 0.0f, 0.0f) : img.m_ColourScale;
        // DIFFERS: opt-in widescreen (Layout::HalfWidth); faithful (no-op) under
        // __bada__ -- widen full-screen overlay quads (XML "scaleToScreen") on the
        // activation frame too, matching Update's per-frame widen below. No-op when
        // m_FlagBits&1 zeroed the size above (0 * k == 0).
#ifndef __bada__
        if (img.m_bScaleToScreen) {
            ctrl->size.x *= (Layout::HalfWidth() / 240.0f);
        }
#endif
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
        ctrl->m_Texture = img.m_Texture;
        // Binary @ 0x0011dd2e: `str r1, [r2, #0x34]` -- raw copy of
        // EffectImage::m_GroupMask, no `?: 1` fallback. If data has 0,
        // the binary writes 0 (HUD_LAYER_NONE -> the control is filtered
        // out of all HUD::Draw passes).
        ctrl->m_LayerFlags = (int)img.m_GroupMask;
        img.m_pHudCtrl    = ctrl;
        img.m_CurrentVis  = 0.0f;

        // ASM-verified: 2026-07-26T00:00Z v1.6.1 ScreenEffect::Activate @ 0x00149174..0x00149194 (asm-inspector)
        // HUD null -> zero the breadcrumb byte (strbeq, r0 is 0 in that arm) and
        // skip the add; otherwise AddControl and leave the flag at its ctor
        // default of 1. The binary never writes m_bAddedToHUD = true here, and
        // never writes it unconditionally -- Update's lazy-add block relies on
        // that, since a zero here is what tells it an add is still pending.
        if (hud) {
            hud->AddControl(ctrl, false);
        } else {
            img.m_bAddedToHUD = false;
        }
    }
}

// ---- ScreenEffect::Update (binary @ 0x00148844) ------------------------------

void ScreenEffect::Update(float dt, float currentLongest, float maxTotal) {
    // Standalone-mode override: when m_TotalDuration > 0, this effect manages
    // its own timeline rather than inheriting the PowerUp's.
    // v1.6.1 ScreenEffect::Update @0x00148844 prologue: currentLongest takes the
    // PRE-decrement m_RemainingTime (s16 is loaded before the subtract; only the
    // decremented value is stored back). Using the post-decrement value fired
    // every threshold/fade/SFX one frame early.
    if (m_RemainingTime > 0.0f) {
        currentLongest  = m_RemainingTime;
        m_RemainingTime -= dt;
        maxTotal        = m_TotalDuration;
    }

    // Per-image fade + slide + size logic
    // v1.6.1 ScreenEffect::Update @0x00148844
    for (size_t i = 0; i < m_Images.size(); ++i) {
        EffectImage& img = m_Images[i];
        // Port specific: no binary counterpart -- v1.6.1 Update @0x00148844 writes
        // through ctrl (`ldr r11,[r5,#8]`) with no null check at all, because
        // Activate always allocates one control per image so it is never null
        // there. Defensive only; see the copy-ctor note above for the case that
        // motivated it. The per-write sites below are likewise UNGATED in the
        // binary (pos @0x00148a28, size @0x00148a6c, alpha @0x00148a98) -- only
        // the alpha-modulate block that dereferences the HUD itself is gated on
        // game_work.pM_pHud (@0x00148a9c-0x00148ad0), and that gate is present.
        // Do not add a game_work.mHud gate here: Update never runs after HUD
        // teardown, unlike Deactivate @0x00148510 which does and needs one.
        if (!img.m_pHudCtrl) continue;

        // ASM-verified: 2026-07-26T00:00Z v1.6.1 ScreenEffect::Update @ 0x001488a8..0x001488d4 (asm-inspector)
        // Lazy HUD add. m_bAddedToHUD (+0x0c) is an inverted "add pending" latch,
        // not an "is added" flag: the ctor defaults it to 1, and Activate zeroes
        // it ONLY on its HUD-was-null arm (strbeq @0x00149184). So 0 means "the
        // control exists but Activate could not add it", and this block is the
        // one that adds it once the HUD appears. On the normal HUD-present path
        // the flag stays 1 and this never fires -- it cannot double-add.
        // NOTE: BOTH tests are GENUINE. 0x001488ac-0x001488c4:
        //   ldr r0,[r3,#0x40] ; cmp r0,#0x0 ; beq 0x001488d8
        //   ldrb r2,[r5,#0xc] ; cmp r2,#0x0 ; bne 0x001488d8
        if (game_work.mHud && !img.m_bAddedToHUD) {
            img.m_bAddedToHUD = true;
            game_work.mHud->AddControl(img.m_pHudCtrl, false);
        }

        float wStart = maxTotal * img.m_StartT;
        float wEnd   = maxTotal * img.m_EndT;

        bool useMoveIn = false;

        // v1.6.1 ScreenEffect::Update @0x00148844: currentLongest counts DOWN;
        // window (wEnd,wStart], m_StartT>m_EndT.
        if (img.m_FadeRate <= 0.0f) {
            // @0x00148960: hard on/off: ON iff wEnd <= currentLongest <= wStart
            bool on = (currentLongest >= wEnd && currentLongest <= wStart);
            img.m_CurrentVis = on ? 1.0f : 0.0f;
            // v1.6.1 ScreenEffect::Update @0x00148844: r7=1 always in this branch
            // (0x00148978/0x0014897c both fall into 0x00148984 `mov r7,#0x1`), not
            // gated on `on` -- the OFF state still slides using m_SizeIn.
            useMoveIn = true;
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

        // v1.6.1 ScreenEffect::Update @0x00148844:
        //   pos = m_Pos + Vec3(480,320,0)(componentwise)*m_Vel(anchor,+0x1c) + moveOffset*e
        // FIX: the previous port multiplied m_Pos itself by (480,320,0) (treating it as
        // a normalised [0..1] fraction of the screen) and dropped the anchor term (m_Vel,
        // +0x1c) entirely. m_Pos is already an absolute centered-ortho position (matches
        // every other position in this codebase -- see docs/engine/coordinate-system.md);
        // the (480,320,0) screen-anchor scales the XML "anchor" attribute instead. This is
        // a general fix (affects every EffectImage, not just the x2 board).
        const _Vector3<float>& moveOffset = useMoveIn ? img.m_SizeIn : img.m_SizeOut;
        // DIFFERS: opt-in widescreen (Layout::HalfWidth); faithful 480 under __bada__ --
        // same widen as Activate()'s emitter kScreenAnchor.x above. A corner/edge-anchored
        // image (e.g. freeze's "clock_freeze", anchor="0.5,0.5") uses anchor.x==0.5 to sit
        // at the right screen edge (480*0.5=240=+HalfWidth); widen that edge term by k so
        // it tracks +-HalfWidth() in widescreen instead of staying pinned at the old 3:2
        // corner. A centered image (anchor.x==0, e.g. the FREEZE banner) is unaffected
        // (k-scale of 0 is 0).
#ifdef __bada__
        const float kAnchorX = 1.0f;
#else
        const float kAnchorX = Layout::HalfWidth() / 240.0f;
#endif
        const _Vector3<float> kScreenAnchor(480.0f * kAnchorX, 320.0f, 0.0f);
        img.m_pHudCtrl->pos = img.m_Pos + kScreenAnchor * img.m_Vel + moveOffset * e;

        // Size = m_ColourScale * ((m_FlagBits & 1) ? (1 - e) : 1.0f)
        // m_ColourScale holds texture dims (written by Parse)
        float scaleFactor = (img.m_FlagBits & 1u) ? (1.0f - e) : 1.0f;
        _Vector3<float> sz;
        sz.x = img.m_ColourScale.x * scaleFactor;
        sz.y = img.m_ColourScale.y * scaleFactor;
        sz.z = img.m_ColourScale.z * scaleFactor;

        // DIFFERS: opt-in widescreen (Layout::HalfWidth); faithful (no-op) under
        // __bada__ -- widen full-screen overlay quads (XML "scaleToScreen", e.g.
        // freeze's "ice_cover") to span +-HalfWidth() instead of leaving the
        // widened field sides uncovered. Only images explicitly marked
        // scaleToScreen="true" are widened; small positioned effect images
        // (clock_freeze, blitz_*, hud_x2_sign, etc.) are untouched.
#ifndef __bada__
        if (img.m_bScaleToScreen) {
            sz.x *= (Layout::HalfWidth() / 240.0f);
        }
#endif

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
        // v1.6.1 ScreenEffect::Update @0x00148a9c-0x00148ad0: multiplies the alpha byte
        // by HUD::m_globalTimeScale (+0x24) for EVERY image, not just fade-flagged
        // ones. NOTE: the HUD test is GENUINE --
        //   ldr r2,[r3,#0x40] ; cmp r2,#0x0 ; beq 0x00148ad4   (0x00148aa4-0x00148aac)
        // The binary has no explicit alpha<0 clamp; vcvt.u32.f32 saturates instead, so
        // the port's clamp is equivalent.
        if (game_work.mHud) {
            alpha = alpha * game_work.mHud->m_globalTimeScale;
            if (alpha < 0.0f) alpha = 0.0f;
        }
        img.m_pHudCtrl->m_DrawColour = Colour(
            img.m_Tint.r,
            img.m_Tint.g,
            img.m_Tint.b,
            (uint8_t)alpha
        );
    }

    // Per-tint colour multiply on HUD scales (v1.6.1 ScreenEffect::Update @0x00148844 tint tail)
    // NOTE: the `if (hud)` below is GENUINE. 0x00148c10-0x00148c18:
    //   ldr r2,[r2,#0x40] ; cmp r2,#0x0 ; beq 0x00148ce8
    // skipping the 3-iteration multiply loop at 0x00148c24-0x00148ce4. The binary
    // re-loads mHud per tint; caching it once is equivalent.
    HUD* hud = game_work.mHud;
    for (size_t i = 0; i < m_Tints.size(); ++i) {
        ScreenTint& t = m_Tints[i];

        if (t.m_TransitionTime <= 0.0f) {
            t.m_CurrentT = 1.0f;                             // no transition -> instantly full
        } else if (currentLongest <= t.m_TimeStart * maxTotal) {
            float fEnd = t.m_TimeEnd * maxTotal;
            if (currentLongest <= fEnd + t.m_TransitionTime) {
                // fade-OUT
                t.m_CurrentT = Clamp((currentLongest - fEnd) / t.m_TransitionTime, 0.0f, 1.0f);
            } else {
                // fade-IN
                t.m_CurrentT = std::min(t.m_CurrentT + dt / t.m_TransitionTime, 1.0f);
            }
        } else {
            t.m_CurrentT = 0.0f;                             // not started
        }

        if (hud) {
            float back[3] = { t.m_BackTint.x, t.m_BackTint.y, t.m_BackTint.z };
            float fore[3] = { t.m_HudTint.x,  t.m_HudTint.y,  t.m_HudTint.z  };
            for (int k = 0; k < 3; ++k) {
                float fb = Clamp((back[k] - 1.0f) * t.m_CurrentT + 1.0f, 0.0f, 1.0f);
                hud->scales[3 + k] *= fb;                     // WORLD/background tint
                float fh = Clamp((fore[k] - 1.0f) * t.m_CurrentT + 1.0f, 0.0f, 1.0f);
                hud->scales[0 + k] *= fh;                     // HUD/foreground tint
            }
        }
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

        for (size_t si = 0; si < m_Sounds.size(); ) {
            SoundEffect& sfx = m_Sounds[si];
            if (currentLongest > maxTotal * sfx.m_StartT) { ++si; continue; }
            sfx.m_StartT = 100.0f;

            if (sfx.m_EndT < 0.0f) {
                // ASM-spec v1.6.1 ScreenEffect::Update @0x00148da4: one-shot
                // fire-and-forget path -- plays once at full volume, never stores
                // a handle, and the record is erased immediately so it can't
                // retrigger. This is the path freeze/frenzy/fourth_banana/scorex2
                // stingers use (their XML has no timeEnd attribute).
                Mortar::Delegate1<bool, Mortar::MortarSound*> emptyDelegate;
                gs->SFXPlay(sfx.m_SoundName, 1.0f, 1.0f, emptyDelegate);
                m_Sounds.erase(m_Sounds.begin() + si);
                continue;
            }

            if (!sfx.m_VoiceHandle) {
                Mortar::Delegate1<bool, Mortar::MortarSound*> emptyDelegate;
                // v1.6.1 ScreenEffect::Update @0x00148e04: volume constant is
                // 0x3f28f5c3 = 0.66f exactly.
                sfx.m_VoiceHandle = gs->SFXPlay(sfx.m_SoundName, 0.66f, 1.0f, emptyDelegate);
            }
            ++si;
        }
        // TODO: v1.6.1 ScreenEffect::Update @0x00148e3c — windowed (m_EndT>=0)
        //   sounds: binary also `while (maxTotal*m_EndT <= currentLongest) erase`s
        //   the record once the window closes. Port once the erase-timing
        //   semantics are confirmed.
    }
}

// ---- ScreenEffect::Deactivate (binary @ 0x00148510) --------------------------

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
        // ASM-spec v1.6.1 ScreenEffect::Deactivate @0x00148510: EffectImage loop guard is
        // (game_work.pM_pHud != 0 && img.m_pHudCtrl != 0) -- the global-HUD null check is the
        // binary's defence against the atexit ~PowerUpManager -> Deactivate path running after
        // GameDestroy has deleted the HUD (GameDestroy @0x0011cea4 nulls game_work.pM_pHud and
        // never drains PowerUpManager; the manager is a __aeabi_atexit local static @0x00140848).
        // NOTE: BOTH tests are GENUINE. 0x0014859c-0x001485b0:
        //   ldr r3,[r3,#0x40] ; cmp r3,#0x0 ; beq 0x00148640
        //   ldr r5,[r8,#0x8]  ; cmp r5,#0x0 ; beq 0x00148640
        // mHud is loaded and tested but never used in the loop body -- a pure liveness
        // probe, which corroborates the atexit-teardown rationale above.
        if (game_work.mHud && img.m_pHudCtrl) {
            // v1.6.1 ScreenEffect::Deactivate @0x00148510
            if (img.m_DeferKind == 1) {
                // Arcade x2 board: bank final payout (doubled points if the window ran
                // to completion), snapshot position, detach from ScreenEffect -- the
                // control STAYS in the HUD and self-animates the payout/dismiss.
                ScoreMultiplyerBoard* board = static_cast<ScoreMultiplyerBoard*>(img.m_pHudCtrl);
                bool nearComplete = m_pOwnerPowerUp && m_pOwnerPowerUp->GetCurrentTimeProgress() <= 0.01f;
                board->m_ScoreValue = nearComplete ? (m_pOwnerPowerUp->m_DeferredPoints * 2) : 0;
                board->m_BasePosition = board->pos;
                board->m_pOwner = 0;
            } else if (img.m_DeferKind == 2) {
                // Berry-Blast time-sink board: mirror the ScoreMultiplyerBoard detach
                // pattern -- clear the owner, zero the payout if the window was
                // aborted early, and leave the control in the HUD to self-animate.
                // v1.6.1 ScreenEffect::Deactivate @0x00148510
                TimeSinkControl* sink = static_cast<TimeSinkControl*>(img.m_pHudCtrl);
                sink->m_pPowerUp = 0;
                if (m_pOwnerPowerUp && m_pOwnerPowerUp->GetCurrentTimeProgress() > 0.01f) {
                    sink->m_TargetScore = 0;
                }
                // Board stays in the HUD and self-animates via
                // TimeSinkControl::Update (marks m_bPendingRemoval itself once
                // m_TimeElapsed > 1.08f, after banking the time award).
            } else {
                img.m_pHudCtrl->m_bPendingRemoval = 1;
            }
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

// ---- ScreenEffect::UnloadTextures --------------------------------------------
// Counterpart of LoadTextures, reached from PowerUp::UnloadTextures @0x00140ae4.
// EffectImage derives from Mortar::ReloadableTexture, whose m_Texture SmartPtr is
// at base offset +0x00 -- so the release goes through the base's Unload()
// (v1.6.1 ReloadableTexture::Unload @0x0014f878), not through any
// EffectImage-specific field.

void ScreenEffect::UnloadTextures() {
    for (size_t i = 0; i < m_Images.size(); ++i)
        m_Images[i].Unload();
}
