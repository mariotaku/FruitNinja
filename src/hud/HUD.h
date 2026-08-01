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
//   +0x20: float m_DrawAlpha               (per-frame HUD draw-alpha; HUD::Update writes 1.0 each tick
//                                           v1.6.1 HUD+0x20; read by ScoreControl::Draw/PreDraw,
//                                           MissControl::Draw for score/miss alpha)
//   +0x24: float m_globalTimeScale          (slow-mo multiplier; ctor writes 1.0;
//                                           SuperFruitControl/MainScreen write <1.0)
class HUD {
public:
    // +0x00: control list (8 bytes on this libstdc++ build)
    std::list<HUDControl*> controls;

    // +0x08..+0x1F: six floats, all init 1.0f by ctor.
    //   HUD::Draw reads scales[0..2] as a Vec3 (&this->scales[0]).
    //   scales[3..5] are preserved for size-fidelity; no HUD read site in scope.
    float scales[6];

    // +0x20: per-frame HUD draw-alpha. HUD::Update writes 1.0 each tick
    // (v1.6.1 HUD::Update @0x0018c3c0: vstr.32 s15,[r4,#0x20] @0x0018c3e0, s15=1.0).
    // Read by ScoreControl::Draw (@0x1abce8) and ScoreControl::PreDraw (@0x1aceac)
    // and MissControl::Draw (@0x001521ac) for score/miss alpha calculation.
    // ctor-UNINITIALIZED in binary; also read by GameOverScreen::PreDrawOrder
    // @0x00186894 as tex-title alpha (always 0 at ctor -> tex title suppressed).
    // ASM-spec v1.6.1 GameOverScreen::PreDrawOrder @0x00186894: alpha = *(hud+0x20) * 255
    float m_DrawAlpha;

    // +0x24: slow-motion multiplier. 1.0 = normal speed, <1.0 = slow-mo.
    // Written 1.0f by HUD::Update each tick; SuperFruitControl/MainScreen write <1.0.
    // Read by SuperFruitControl::Update, MainScreen::Update, TutorialControl.
    float m_globalTimeScale;

    HUD();
    ~HUD();
    void Init();
    void Release();
    void AddControl(HUDControl* ctrl, bool pushFront = false);
    void RemoveControl(HUDControl* ctrl);
    void BeginDraw(float dt);
    void Draw(long layerMask);
    void Update(float dt);
#ifndef __bada__
    // Port specific: no binary counterpart. Per-PRESENT UI tick -- walks
    // `controls` and calls HUDControl::UpdateRealtime(dtSeconds) on each
    // active one (default no-op; see HUDControl.h). Generalizes the
    // per-present dispatch so any HUD control (ScrollingMenu, SettingsScreen,
    // future widgets) can opt into display-refresh-rate motion instead of the
    // fixed 60Hz sim step, without Game.cpp needing a per-control special case.
    // Called by Game::tickRealtimeUi. Excluded under __bada__ since
    // HUDControl::UpdateRealtime doesn't exist there.
    void UpdateRealtime(float dtSeconds);
#endif
    void ResetControls();
    void OnPause();
    void Save();
    void Skip();
    void SetToMultiplayerState();

    // Port specific: modal input capture (e.g. SettingsScreen). While set,
    // HUD::Update only updates the modal control itself plus any control at
    // HUD_LAYER_TOP_MOST (its spawned dropdown ListBox/VerticalScroller,
    // also top-most) -- other controls (menu buttons, gameplay HUD) are
    // frozen so touches don't pass through to them. No binary counterpart.
#if !defined(__bada__)
    void SetInputModal(HUDControl* c) { m_pInputModal = c; }
    HUDControl* GetInputModal() const { return m_pInputModal; }

private:
    // Port specific: see SetInputModal/GetInputModal above. Not part of the
    // binary layout -- excluded on __bada__ builds so sizeof(HUD) stays 0x28.
    HUDControl* m_pInputModal;
#else
    void SetInputModal(HUDControl*) {}
    HUDControl* GetInputModal() const { return nullptr; }
#endif
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(HUD) == 0x28, "HUD size mismatch (v1.6.1 @0x001ce208)");
static_assert(__builtin_offsetof(HUD, scales)           == 0x08, "HUD::scales offset");
static_assert(__builtin_offsetof(HUD, m_DrawAlpha)         == 0x20, "HUD::m_DrawAlpha offset");
static_assert(__builtin_offsetof(HUD, m_globalTimeScale) == 0x24, "HUD::m_globalTimeScale offset");
#endif

#endif
