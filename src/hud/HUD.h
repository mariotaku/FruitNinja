#ifndef FN_HUD_H
#define FN_HUD_H

#include "HUDControl.h"
#include "MissControl.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include <list>

// Binary sizeof(HUD) == 0x28 (operator new(0x28) @0x001ce208 v1.6.1 HUD::HUD).
// Layout:
//   +0x00: std::list<HUDControl*> controls  (8 bytes)
//   +0x08: float scales[6]                  (24 bytes, all 1.0f at ctor)
//   +0x20: float m_TitleTexAlpha               (ctor-UNINITIALIZED; always 0; suppresses tex-title)
//   +0x24: float m_globalTimeScale          (1.0f sentinel written by HUD::Update each tick)
class HUD {
public:
    // +0x00: control list (8 bytes on this libstdc++ build)
    std::list<HUDControl*> controls;

    // +0x08..+0x1F: six floats, all init 1.0f by ctor.
    //   HUD::Draw reads scales[0..2] as a Vec3 (&this->scales[0]).
    //   scales[3..5] are preserved for size-fidelity; no HUD read site in scope.
    float scales[6];

    // +0x20: ctor-UNINITIALIZED in binary (reads 0); sourced by GameOverScreen::PreDrawOrder
    // @0x00186894 as the texture-title alpha -- always 0 -> tex title suppressed, TTF title shows.
    // ASM-spec v1.6.1 GameOverScreen::PreDrawOrder @0x00186894: alpha = *(hud+0x20) * 255
    float m_TitleTexAlpha;

    // +0x24: slow-motion multiplier. 1.0 = normal speed, <1.0 = slow-mo.
    // Written 1.0f by HUD::Update each tick; SuperFruitControl/MainScreen write <1.0.
    // Read by SuperFruitControl::Update, MainScreen::Update, MissControl, ScoreControl,
    // TutorialControl (all via *(float*)(hud+0x24) in binary).
    float m_globalTimeScale;

    HUD();
    ~HUD();
    void Init();
    void Release();
    void AddControl(HUDControl* ctrl, bool pushFront = false);
    void RemoveControl(HUDControl* ctrl);
    void BeginDraw(float dt);
    void Draw(int layerMask);
    void Update(float dt);
    void ResetControls();
    void OnPause();
    void Save();
    void Skip();
    void SetToMultiplayerState();
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(HUD) == 0x28, "HUD size mismatch (v1.6.1 @0x001ce208)");
static_assert(__builtin_offsetof(HUD, scales)           == 0x08, "HUD::scales offset");
static_assert(__builtin_offsetof(HUD, m_TitleTexAlpha)     == 0x20, "HUD::m_TitleTexAlpha offset");
static_assert(__builtin_offsetof(HUD, m_globalTimeScale) == 0x24, "HUD::m_globalTimeScale offset");
#endif

#endif
