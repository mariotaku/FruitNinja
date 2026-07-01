// test_menubutton_anchor -- Guards v1.6.1 MenuButton::Draw @0x0019c2e4
// scratch-backdrop anchor fix: GetAdjustedPos (vtable slot 15 @0x136c2c)
// = pos + Vec3(480,320,0)*m_HudScale.
//
// The main-menu quit-bomb sets pos=(0,0,0) and m_HudScale=(0.375,-0.3,0)
// (MainScreen::CreateQuitButton @0x00196a5c-0x00196b3c). Before the fix,
// Draw used raw pos for the scratch decal anchor, placing it at screen center
// (0,0) instead of on the bomb ring at (180,-96). This test pins the correct
// anchor formula so any future regression in GetAdjustedPos() usage is caught.
//
// Pure in-process: no GPU, no audio, no SDL. Runs in ctest -E screenshot.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

// No-op stub for Debug::Log so HUDControl.cpp's LOG_DEBUG ctor/dtor calls
// link without dragging in the SDL-bound src/debug/LoggerSDL.cpp.
#include "debug/Logger.h"
namespace Debug {
    void Log(LogLevel, const char*, const char*, ...) {}
}

#include "hud/HUDControl.h"
#include <cstdio>
#include <cmath>

static bool near_eq(float a, float b) {
    return fabsf(a - b) < 1e-4f;
}

static bool vec3_near(Vec3 a, Vec3 b) {
    return near_eq(a.x, b.x) && near_eq(a.y, b.y) && near_eq(a.z, b.z);
}

int main() {
    int failures = 0;

    // CASE 1: quit-bomb configuration (MainScreen::CreateQuitButton @0x00196a5c)
    //   pos = Vec3(0,0,0)      -- passed to MenuButton::Init @0x0019b994
    //   m_HudScale.x = 0.375f  -- 0.5 * 0.75f, binary @0x00196a74
    //   m_HudScale.y = -0.3f   -- -0.5 * 0.6f, binary @0x00196a9c
    //   m_HudScale.z = 0       -- Vec3 default ctor
    // GetAdjustedPos = (480*0.375, 320*(-0.3), 0*0) = (180, -96, 0).
    // The scratch-backdrop bug placed the decal at raw pos = (0,0,0) instead.
    {
        HUDControl ctrl;
        ctrl.pos          = Vec3(0.0f, 0.0f, 0.0f);
        ctrl.m_HudScale.x =  0.375f;
        ctrl.m_HudScale.y = -0.3f;
        // m_HudScale.z stays 0; z-contribution in GetAdjustedPos is 0*z anyway

        Vec3 result   = ctrl.GetAdjustedPos();
        Vec3 expected = Vec3(180.0f, -96.0f, 0.0f);

        if (!vec3_near(result, expected)) {
            fprintf(stderr,
                "FAIL [quit-bomb anchor]: GetAdjustedPos=(%.4f,%.4f,%.4f) "
                "expected=(%.4f,%.4f,%.4f)\n",
                result.x, result.y, result.z,
                expected.x, expected.y, expected.z);
            ++failures;
        } else {
            printf("[PASS] quit-bomb anchor: GetAdjustedPos=(%.0f,%.0f,%.0f)\n",
                result.x, result.y, result.z);
        }
    }

    // CASE 2: position-anchored button (pos carries real coords, m_HudScale stays zero)
    // Most buttons keep their coords in pos and leave m_HudScale at (0,0,0).
    // For these, GetAdjustedPos must equal pos unchanged.
    // This case documents why only the quit-bomb was affected: it is the
    // only button that stores its position in m_HudScale rather than pos.
    {
        HUDControl ctrl;
        ctrl.pos = Vec3(16.0f, -66.0f, 0.0f);  // e.g. DojoScreen play-button pos
        // m_HudScale = (0,0,0) from Vec3 default ctor: no offset contribution

        Vec3 result   = ctrl.GetAdjustedPos();
        Vec3 expected = ctrl.pos;

        if (!vec3_near(result, expected)) {
            fprintf(stderr,
                "FAIL [pos-anchored]: GetAdjustedPos=(%.4f,%.4f,%.4f) "
                "expected=(%.4f,%.4f,%.4f)\n",
                result.x, result.y, result.z,
                expected.x, expected.y, expected.z);
            ++failures;
        } else {
            printf("[PASS] pos-anchored: GetAdjustedPos=(%.0f,%.0f,%.0f) matches pos\n",
                result.x, result.y, result.z);
        }
    }

    if (failures == 0) {
        printf("[PASS] All menubutton_anchor cases passed.\n");
        return 0;
    }
    return 1;
}
