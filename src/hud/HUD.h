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

    // +0x08..+0x1F: six tint multipliers, all init 1.0f.
    //   scales[0..2] = gameplay-mutable tint window (passed to HUDControl::Draw)
    //   scales[3..5] = world tint window (passed to SplatEntity::DrawSplat)
    // ASM-verified: 2026-04-29T03:29Z binary @ 0x00144a90 (HUD::Draw)
    float scales[6];

    // +0x20: slow-motion multiplier. 1.0 = normal speed, <1.0 = slow-mo.
    // Read by TutorialControl::CanShowTute (binary @ 0x00162fb8).
    // HUD::Update resets it to 1.0f at the start of each tick; the wave
    // system writes values < 1.0 during last-fruit slow-motion.
    float m_globalTimeScale;

    // Binary @ 0x00144bb0
    // DIFFERS: binary doesn't init m_globalTimeScale in ctor (Update sets it each frame);
    //          port initialises to 1.0 here which is harmless.
    HUD();

    // Binary @ 0x00144cd0
    ~HUD();

    // Binary @ 0x00144d18 — controls.clear() only; does NOT reset scales/timeScale
    void Init();

    // Binary @ 0x00144c5c
    void Release();

    // Binary @ 0x00144db0 — bool is "true=push_front, false=push_back"
    void AddControl(HUDControl* ctrl, bool pushFront = false);

    // Binary @ 0x00144c40
    void RemoveControl(HUDControl* ctrl);

    // Binary @ 0x00144b28
    void BeginDraw(float dt);

    // Binary @ 0x00144a90
    // DIFFERS: port adds per-control world.Reset() for matrix discipline; binary leaves
    //          matrix discipline to each control.
    // ASM-verified: 2026-04-29T03:29Z binary @ 0x00144a90 (HUD::Draw)
    void Draw(int layerMask);

    // Binary @ 0x00144d20
    // delete c matches binary's vtable+4 deleting-dtor (semantically equivalent).
    void Update(float dt);

    // Binary @ 0x00144b78 — unconditional Reset() dispatch on every control
    void ResetControls();

    // Binary @ 0x00144c00
    void OnPause();

    // Binary @ 0x00144a20 — null-checked iterate, vtable+0x38 Save dispatch (slot 14)
    void Save();

    // Binary @ 0x00144a58 — unconditional Skip() dispatch on every control (slot 13)
    void Skip();

    // Binary @ 0x00144dcc — two-pass: collect controls whose SetToMultiplayerState
    //                       returned true, then RemoveControl each
    void SetToMultiplayerState();
};

#endif
