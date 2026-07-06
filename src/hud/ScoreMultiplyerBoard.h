#ifndef FN_HUD_SCORE_MULTIPLYER_BOARD_H
#define FN_HUD_SCORE_MULTIPLYER_BOARD_H

// ScoreMultiplyerBoard : HUDControl3d (size = 0x9c)
// Arcade "x2" deferred-points board -- shown while the x2 PowerUp is active
// (green pending-points counter), then self-animates a payout of the doubled
// score once the PowerUp expires (blue number pop -> slide off).
//
// Binary addresses (v1.6.1):
//   ctor    0x001adf70
//   Reset   0x001adee4
//   Update  0x001ae000
//   Draw    0x001ae434
//   Save    0x001ae5dc
//   GetType inherited from HUDControl3d (returns 1)
//
// Lifecycle: ScreenEffect::Activate (kind==1, XML defer="points"/deferPoints="true")
// constructs this and sets m_pOwner to the owning PowerUp; every Update() while
// m_pOwner is set just mirrors PowerUp::m_DeferredPoints (the "counter climbing"
// phase -- the board's own position/size/colour are driven by ScreenEffect::Update's
// normal per-image transform in this phase, same as any other EffectImage).
// ScreenEffect::Deactivate (kind==1) captures the final payout value into
// m_ScoreValue, snapshots the current screen position into m_BasePosition, and
// clears m_pOwner -- from that point the board is detached from ScreenEffect
// (removed from m_Images) and self-animates via its own Update() until it
// marks itself for removal.

#include "HUDControl3d.h"

class PowerUp;

class ScoreMultiplyerBoard : public HUDControl3d {
public:
    // +0x7c: position snapshot taken at Deactivate; self-animation origin.
    Vec3 m_BasePosition;
    // +0x88: owning PowerUp while the x2 window is active; NULL once detached
    // (self-animating payout phase).
    PowerUp* m_pOwner;
    // +0x8c: pending (not-yet-banked) doubled points; mirrors
    // m_pOwner->m_DeferredPoints while owned; frozen once detached.
    int m_PendingCount;
    // +0x90: final payout value, written by ScreenEffect::Deactivate. 0 means
    // "retreat with no payout" (x2 expired without banking any points); -1
    // (ctor default) means "never activated".
    int m_ScoreValue;
    // +0x94: self-animation clock; starts at 0 the frame m_pOwner is cleared.
    float m_AnimTime;
    // +0x98: green-counter shrink / blue-number-pop scale factor; also used
    // directly as the DrawString scale multiplier (m_Scale*35).
    float m_Scale;

    // ctor -- v1.6.1 ScoreMultiplyerBoard::ScoreMultiplyerBoard @0x001adf70
    ScoreMultiplyerBoard();
    ~ScoreMultiplyerBoard() override {}

    // v1.6.1 ScoreMultiplyerBoard::Reset @0x001adee4
    void Reset() override;
    // v1.6.1 ScoreMultiplyerBoard::Update @0x001ae000
    void Update(float dt) override;
    // v1.6.1 ScoreMultiplyerBoard::Draw @0x001ae434
    void Draw(float* hudScaleRaw) override;
    // v1.6.1 ScoreMultiplyerBoard::Save @0x001ae5dc
    void Save() override;
};

#ifdef __bada__
#include <cstddef>
static_assert(offsetof(ScoreMultiplyerBoard, m_BasePosition) == 0x7c, "ScoreMultiplyerBoard::m_BasePosition @ +0x7c");
static_assert(offsetof(ScoreMultiplyerBoard, m_pOwner)       == 0x88, "ScoreMultiplyerBoard::m_pOwner @ +0x88");
static_assert(offsetof(ScoreMultiplyerBoard, m_PendingCount) == 0x8c, "ScoreMultiplyerBoard::m_PendingCount @ +0x8c");
static_assert(offsetof(ScoreMultiplyerBoard, m_ScoreValue)   == 0x90, "ScoreMultiplyerBoard::m_ScoreValue @ +0x90");
static_assert(offsetof(ScoreMultiplyerBoard, m_AnimTime)     == 0x94, "ScoreMultiplyerBoard::m_AnimTime @ +0x94");
static_assert(offsetof(ScoreMultiplyerBoard, m_Scale)        == 0x98, "ScoreMultiplyerBoard::m_Scale @ +0x98");
static_assert(sizeof(ScoreMultiplyerBoard) == 0x9c, "ScoreMultiplyerBoard size mismatch"); // v1.6.1 @0x001adf70
#endif

#endif // FN_HUD_SCORE_MULTIPLYER_BOARD_H
