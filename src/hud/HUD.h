#ifndef FN_HUD_H
#define FN_HUD_H

// Analysed: 2026-04-30T00:00

#include "HUDControl.h"
#include "MissControl.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include <list>

// Matches original HUD class (0x24 bytes)
class HUD {
public:
    // +0x00: control list (8 bytes on this libstdc++ build)
    std::list<HUDControl*> controls;

    // +0x08..+0x1F: six floats, all init 1.0f by ctor.
    //   HUD::Draw reads scales[0..2] as a Vec3 (&this->scales[0]).
    //   scales[3..5] are preserved for size-fidelity; no HUD read site in scope.
    float scales[6];

    // +0x20: slow-motion multiplier. 1.0 = normal speed, <1.0 = slow-mo.
    // Read by TutorialControl::CanShowTute (binary @ 0x00162fb8).
    // HUD::Update resets it to 1.0f at the start of each tick; the wave
    // system writes values < 1.0 during last-fruit slow-motion.
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

#endif
