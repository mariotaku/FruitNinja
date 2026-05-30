#ifndef FN_GAME_LEADERBOARD_LIST_H
#define FN_GAME_LEADERBOARD_LIST_H

// Defunct: LeaderboardList -- ScrollingMenu subclass for online leaderboard rows.
// Binary ctors @ 0x00147128 / 0x00147158. Vtable @ 0x001e9828.
// sizeof(LeaderboardList) == 0x12C (300 bytes) on ARM32:
//   ScrollingMenu base: 256 bytes (0x00..0xFF)
//   field_0x104:        4 bytes  (uint32_t, zero-init in ctor)
//   m_callback:        36 bytes  (Mortar::Delegate0<void>; Mortar::Delegate0 = 36 bytes)
//   Total: 256 + 4 + 36 = 296? No -- gap at 0x100..0x103 (4 bytes ScrollingMenu trailing pad)
//   then 0x104 (4) + 0x108..0x12B (36) = 256 + 4 + 4 + 36 = 300 = 0x12C. Correct.

#include "hud/ScrollingMenu.h"
#include "engine/util/Delegate.h"
#include <cstdint>

class LeaderboardList : public ScrollingMenu {
public:
    // Defunct: LeaderboardList -- no-op stub; binary @ 0x00147128
    LeaderboardList() {}
    ~LeaderboardList() override {}

    // Defunct: online leaderboards -- no-op stub; binary address unknown.
    void Init() override {}

#ifdef __bada__
    // +0x100..+0x103: ScrollingMenu trailing padding (not a new field; layout gap)
    // +0x104: state/index field (zero-init in ctor)
    uint32_t m_field104;                    // +0x104
    // +0x108..+0x12B: 0-arg void callback delegate
    Mortar::Delegate0<void> m_callback;     // +0x108
#endif
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
#include <cstddef>
static_assert(sizeof(LeaderboardList) == 0x12C, "LeaderboardList size mismatch");
static_assert(offsetof(LeaderboardList, m_field104) == 0x104, "LeaderboardList::m_field104 offset");
static_assert(offsetof(LeaderboardList, m_callback) == 0x108, "LeaderboardList::m_callback offset");
#endif

#endif // FN_GAME_LEADERBOARD_LIST_H
