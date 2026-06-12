#ifndef FN_SCREENS_CHALLENGE_MENU_SCREEN_H
#define FN_SCREENS_CHALLENGE_MENU_SCREEN_H

//
// ChallengeMenuScreen : HUDControl3d  (v1.6.1 binary -- DEFUNCT placeholder)
//
// Only `_GLOBAL__I_ChallengeMenuScreen_cpp` @ 0x00166010 exists in the binary
// (static-init of Colour/Vec3/SmartPtr<Texture> file-scope globals). No ctor,
// Update, Draw, or class-method symbols survive.
//
// Reachability: GameModeScreen::ChallengesCallback @ 0x00181154 sets
// m_State = 0xe. GameModeScreen::Update case 0xe is an EMPTY break -- the
// challenge screen transition is not implemented in this build.
//
// Also symbol-less in this build (same pattern):
//   ChallengeHistoryScreenSL.cpp, ChallengeScreenSL.cpp,
//   CreateChallengeScreenSL.cpp, BuyStarfruitScreen.cpp.
//

#include "hud/HUDControl3d.h"

class ChallengeMenuScreen : public HUDControl3d {
public:
    // Defunct: ChallengeMenuScreen -- no surviving class methods in binary
    // (only _GLOBAL__I_ static-init @ 0x00166010) -- placeholder, no-op stub
    ChallengeMenuScreen() {}

    // Defunct: ChallengeMenuScreen -- no surviving class methods in binary
    // (only _GLOBAL__I_ static-init @ 0x00166010) -- placeholder, no-op stub
    ~ChallengeMenuScreen() override {}

    // Defunct: ChallengeMenuScreen -- no surviving class methods in binary
    // (only _GLOBAL__I_ static-init @ 0x00166010) -- placeholder, no-op stub
    void Init() override {}

    // Defunct: ChallengeMenuScreen -- no surviving class methods in binary
    // (only _GLOBAL__I_ static-init @ 0x00166010) -- placeholder, no-op stub
    void Update(float /*dt*/) override {}

    // Defunct: ChallengeMenuScreen -- no surviving class methods in binary
    // (only _GLOBAL__I_ static-init @ 0x00166010) -- placeholder, no-op stub
    void Draw(const Vec3& /*hudScale*/, int /*layerMask*/) override {}

    // Defunct: ChallengeMenuScreen -- no surviving class methods in binary
    // (only _GLOBAL__I_ static-init @ 0x00166010) -- placeholder, no-op stub
    int GetType() override { return 1; }

    // Defunct: ChallengeMenuScreen -- no surviving class methods in binary
    // (only _GLOBAL__I_ static-init @ 0x00166010) -- placeholder, no-op stub
    static void LoadContent() {}

    // Defunct: ChallengeMenuScreen -- no surviving class methods in binary
    // (only _GLOBAL__I_ static-init @ 0x00166010) -- placeholder, no-op stub
    static void UnLoadContent() {}
};

#endif // FN_SCREENS_CHALLENGE_MENU_SCREEN_H
