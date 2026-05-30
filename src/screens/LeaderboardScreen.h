#ifndef FN_SCREENS_LEADERBOARD_SCREEN_H
#define FN_SCREENS_LEADERBOARD_SCREEN_H

// Defunct: LeaderboardScreen -- online leaderboard UI; no-op stub.
// Binary ctors @ 0x00148140 / 0x001481c4. HUDControl3d-derived.
// sizeof(LeaderboardScreen) == 0xAA (170 bytes) on ARM32:
//   HUDControl3d base: 0x7C (124 bytes, includes SmartPtr<Texture>+SmartPtr<Model>)
//   Derived fields:    0x2E (46 bytes, ctor-writes at +0x7c..+0xa9)
//   Total: 124 + 46 = 170.

#include "hud/HUDControl3d.h"
#include <cstdint>

class LeaderboardScreen : public HUDControl3d {
public:
    // Defunct: LeaderboardScreen -- no-op stub; binary @ 0x001481c4
    LeaderboardScreen() {}
    ~LeaderboardScreen() override {}

    // Defunct: LeaderboardScreen -- no-op stub; binary @ 0x00148030
    static void LoadContent() {}

    // Defunct: LeaderboardScreen -- no-op stub
    static void UnLoadContent() {}

    // Defunct: LeaderboardScreen -- no-op stub; binary @ 0x00147cd0
    void OnLeaderboardListPopulated(void* /*list*/) {}

    // Defunct: LeaderboardScreen -- no-op stub; binary @ 0x00147d1c
    void LoadLeaderboards(int /*gameMode*/, int /*boardId*/) {}

    // Defunct: LeaderboardScreen -- no-op stub
    void Update(float /*dt*/) override {}

private:
    // Derived fields +0x7C..+0xA9 (46 bytes, layout opaque).
    // Ctor-writes confirmed: HUDControlFns* at +0x7c, uint32_t at +0x80,
    // floats at +0x84..+0x94, uint8_t at +0x99, floats at +0x9c/+0xa0/+0xa4,
    // uint8_t at +0xa8/+0xa9. Total span: 0x7C + 0x2E = 0xAA = 170.
    uint8_t m_pad[0xAA - 0x7C];
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
#include <cstddef>
static_assert(sizeof(LeaderboardScreen) == 0xAA, "LeaderboardScreen size mismatch");
#endif

#endif // FN_SCREENS_LEADERBOARD_SCREEN_H
