#ifndef FN_PAUSE_SCREEN_H
#define FN_PAUSE_SCREEN_H

// Analysed: 2026-04-30T00:00
//
// PauseScreen : HUDControl3d (size = 0xd8)
//
// Binary refs:
//   ctor   PauseScreen::PauseScreen @ 0x00101778 (PLT thunk)
//   Init   vtable[2] called from GameInit step 12 (0x0016cad8..0x0016caf8)
//   size   operator new(0xd8)
//   stored at g_TaskState +0x04
//
// Full PauseScreen struct + LoadContent body not yet RE'd.
// TODO: full body -- see docs/systems/gameinit-todos.md step 12.
//

#include "hud/HUDControl3d.h"

class PauseScreen : public HUDControl3d {
public:
    // TODO: PauseScreen fields (+0x7c..+0xd8) -- RE gap, step 12.

    PauseScreen();
    ~PauseScreen();

    // vtable[2]: Init / LoadContent -- called from GameInit immediately after ctor.
    // Binary: (*vtable[2])(pauseScreen) at 0x0016caf4.
    // TODO: full body -- see docs/systems/gameinit-todos.md step 12.
    void Init() override;

    // vtable[3]: Release
    void Release() override;

    // vtable[4]: Update
    void Update(float dt) override;

    // vtable[5]: Draw
    void Draw(const Vec3& hudScale, int layerMask) override;

    int GetType() override { return 1; }
};

#endif  // FN_PAUSE_SCREEN_H
