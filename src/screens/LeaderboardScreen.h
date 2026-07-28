#ifndef FN_SCREENS_LEADERBOARD_SCREEN_H
#define FN_SCREENS_LEADERBOARD_SCREEN_H

// Defunct: LeaderboardScreen -- online leaderboard UI; no-op stub.
// Binary ctor @ 0x00191c88. HUDControl3d-derived.
// sizeof(LeaderboardScreen) == 0xAC (172 bytes) on ARM32:
//   HUDControl3d base: 0x7C (124 bytes, includes SmartPtr<Texture>+SmartPtr<Model>)
//   Derived fields:    0x2E (46 bytes, ctor-writes at +0x7c..+0xa9; last field m_bFieldA9 @0xA9)
//   Natural 4-byte tail pad: +0xAA..+0xAB (2 bytes)
//   Total: 124 + 46 + 2 = 172 = 0xAC.

#include "hud/HUDControl3d.h"
#include <cstdint>

class LeaderboardScreen : public HUDControl3d {
public:
    // Defunct: LeaderboardScreen -- no-op stub; v1.6.1 LeaderboardScreen ctor @ 0x00191c88
    LeaderboardScreen() {}
    ~LeaderboardScreen() override {}

    // Defunct: LeaderboardScreen -- no-op stub; v1.6.1 LeaderboardScreen::LoadContent @ 0x00191aac
    // (unreachable in v1.6.1: no live caller, so the 6 textures are never loaded)
    static void LoadContent() {}

    // TODO: v1.6.1 0x00192320 (LeaderboardScreen::UnLoadContent) — this is NOT a defunct no-op.
    // The symbol is LIVE: its PLT thunk @0x00106cd4 is called from GameDestroy @0x0011ceec, and
    // the body clears s_isContentLoaded and releases the 6 leaderboard textures. The port body
    // below is an unimplemented gap that is harmless ONLY because the matching LoadContent is
    // unreachable in v1.6.1, so there is nothing to release. Implement both together.
    // The GameInitialise.cpp teardown call site is present and must stay.
    static void UnLoadContent() {}

    // Defunct: LeaderboardScreen -- no-op stub; v1.6.1 LeaderboardScreen::OnLeaderboardListPopulated @ 0x0019167c
    void OnLeaderboardListPopulated(void* /*list*/) {}

    // Defunct: LeaderboardScreen -- no-op stub; v1.6.1 LeaderboardScreen::LoadLeaderboards @ 0x001916d8
    void LoadLeaderboards(int /*gameMode*/, int /*boardId*/) {}

    // Defunct: LeaderboardScreen -- no-op stub
    void Update(float /*dt*/) override {}

private:
    // Derived fields +0x7C..+0xA9 (46 bytes, layout opaque).
    // Ctor-writes confirmed: HUDControlFns* at +0x7c, uint32_t at +0x80,
    // floats at +0x84..+0x94, uint8_t at +0x99, floats at +0x9c/+0xa0/+0xa4,
    // uint8_t at +0xa8/+0xa9. Last field m_bFieldA9 @0xA9.
    // Natural 4-byte alignment adds 2 tail-pad bytes -> sizeof == 0xAC.
    uint8_t m_pad[0xAA - 0x7C];
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(LeaderboardScreen) == 0xAC, "LeaderboardScreen size mismatch");
#endif

#endif // FN_SCREENS_LEADERBOARD_SCREEN_H
