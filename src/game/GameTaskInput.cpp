// Analysed: 2026-05-03T00:00
// GameTaskInitInput -- binary @ 0x00169670 (357 lines).

#include "game/GameTaskInput.h"
#include "Game.h"
#include "input/InputManager.h"
#include "util/StringHash.h"
#include "math/Vec3.h"
#include "entities/ActorManager.h"
#include "screens/PauseScreen.h"
#include <cstdio>

// ASM-verified: 2026-05-03 binary @ 0x00169670 (re-analyst)

// 16-slot touch zone position table.
// Binary: g_TaskState+0xa0..0xa8 region, 12-byte stride (Vec3).
// GOT[+0x77cc] supplies the zero-vec3 default used in the loop.
static Vec3 g_TouchZoneTable[16];

// 16-slot entity pointer table — one ActorManager type-3 entity per touch zone.
// Binary: stored at g_TaskState+0x24..+0x60 (4 bytes each, 16 entries).
static Entity* g_TouchEntities[16];

// Forward declarations for input callbacks (bodies below).
static bool PointerMoveCallback(InputEvent* ev);
static bool PointerDownCallback(InputEvent* ev);
static bool PointerUpCallback(InputEvent* ev);
static bool PointerDownXboxCallback(InputEvent* ev);
static bool PauseGameCallback(InputEvent* ev);
static bool RegressMenuCallback(InputEvent* ev);
static bool ShowPauseMenuCallback(InputEvent* ev);

// TouchDownCallback -- used in per-zone loop for "TouchReleased_<i>" action.
// Binary name from the zone-loop registration (distinct from PointerDownCallback
// at 0x00168e24 which handles global "PointerPressed").
// TODO: implement full body (binary addr TBD from zone-loop decompile).
static bool TouchDownCallback(InputEvent* ev);


// GameTaskInitInput() @ 0x00169670
// Initialises per-session input bindings: config load, 16 touch zones,
// and 7 global action callbacks.
void GameTaskInitInput() {
    // --- Section A: Config load @ 0x16967e ---
    // TODO: implement Mortar::InputManager::LoadConfigFile (binary @ 0x1969d8)
    // Binary: InputManager::GetInstance()->LoadConfigFile("Input/Input.txt");
    // Port specific: LoadConfigFile not ported; SDL2 input does not use
    // an action-mapper config file. Call site preserved as comment so
    // call-graph shape matches binary.

    InputManager* im = InputManager::GetInstance();
    if (!im) return;

    // --- Section B: 16-zone loop @ 0x169690 ---
    // Binary @ 0x00169670: TouchDown registered TWICE, TouchMove_X computed but unused -- preserve verbatim.
    Vec3 defaultPos(0.0f, 0.0f, 0.0f);  // GOT[+0x77cc]
    for (int i = 0; i < 16; ++i) {
        g_TouchZoneTable[i] = defaultPos;

        Entity* e = ActorManager::GetInstance()->Add(3, true);
        g_TouchEntities[i] = e;

        Vec3 initPos = defaultPos;
        // Binary: Entity::vtable[+0x08] called as (0, 0, &initPos).
        // Port Entity::Init signature is (int, int, int); Vec3* cast to int.
        // Port specific: calling with 0,0,0 -- third arg is Vec3* in binary ARM32.
        e->Init(0, 0, 0);

        char nameDown[16], nameMove[16], nameUp[20];
        snprintf(nameDown, sizeof(nameDown), "TouchDown_%d", i);
        snprintf(nameMove, sizeof(nameMove), "TouchMove_X%d", i);
        snprintf(nameUp,   sizeof(nameUp),   "TouchReleased_%d", i);

        // Binary @ 0x00169670: TouchDown_<i> registered TWICE -- preserve verbatim.
        // Port specific: binary RegisterInputCallback(hash, fnPtr) takes 2 args;
        // port wraps with actionFlags = INPUT_ACTION_DOWN | INPUT_ACTION_MOVE | INPUT_ACTION_UP.
        im->RegisterInputCallback(StringHash(nameDown),
                                  INPUT_ACTION_DOWN | INPUT_ACTION_MOVE | INPUT_ACTION_UP,
                                  PointerMoveCallback);
        im->RegisterInputCallback(StringHash(nameDown),
                                  INPUT_ACTION_DOWN | INPUT_ACTION_MOVE | INPUT_ACTION_UP,
                                  PointerMoveCallback);
        im->RegisterInputCallback(StringHash(nameUp),
                                  INPUT_ACTION_DOWN | INPUT_ACTION_MOVE | INPUT_ACTION_UP,
                                  TouchDownCallback);

        // nameMove hash is computed but never registered in binary -- snprintf
        // called to match binary stack layout; hash deliberately not registered.
        (void)nameMove;
    }

    // --- Section C: 7 global named callbacks @ 0x169a32 ---
    // Port specific: binary RegisterInputCallback(hash, fnPtr) is 2-arg;
    // port uses 3-arg (hash, flags, callback).
    im->RegisterInputCallback(StringHash("PointerMove"),
                              INPUT_ACTION_MOVE,
                              PointerMoveCallback);       // binary @ 0x0016a4b4

    im->RegisterInputCallback(StringHash("PointerPressed"),
                              INPUT_ACTION_DOWN,
                              PointerDownCallback);       // binary @ 0x00168e24

    im->RegisterInputCallback(StringHash("PointerReleased"),
                              INPUT_ACTION_UP,
                              PointerUpCallback);         // binary @ 0x00168e48

    im->RegisterInputCallback(StringHash("PointerPressedX"),
                              INPUT_ACTION_DOWN,
                              PointerDownXboxCallback);   // binary @ 0x0016a41c

    im->RegisterInputCallback(StringHash("PauseGame"),
                              INPUT_ACTION_DOWN | INPUT_ACTION_UP,
                              PauseGameCallback);         // binary @ 0x00168fd8

    im->RegisterInputCallback(StringHash("RegressMenu"),
                              INPUT_ACTION_DOWN | INPUT_ACTION_UP,
                              RegressMenuCallback);       // binary @ 0x00168e9c

    im->RegisterInputCallback(StringHash("ShowPauseMenu"),
                              INPUT_ACTION_DOWN | INPUT_ACTION_UP,
                              ShowPauseMenuCallback);     // binary @ 0x00168e6c
}

// --- Input callback stubs ---
// Full bodies require InputEvent struct shape + Game field offsets.
// Binary addresses in comments are from the decompile of GameTaskInitInput.

// PointerMoveCallback @ 0x0016a4b4
// Binary: dispatches TouchMoveX / TouchMoveY on the matching SlashEntity
// via Game[+0xa0..+0xb8] zone table.
// TODO: dispatch SlashEntity::TouchMoveX/Y with finger position.
static bool PointerMoveCallback(InputEvent* ev) {
    (void)ev;
    // TODO: implement PointerMoveCallback (binary @ 0x0016a4b4)
    return true;
}

// PointerDownCallback @ 0x00168e24
// Binary: Game[+0x9c]=1; Game[+0x9e]=1.
// TODO: set corresponding Game fields when they are added to Game.h.
static bool PointerDownCallback(InputEvent* ev) {
    (void)ev;
    // TODO: implement PointerDownCallback (binary @ 0x00168e24)
    // Binary: g_GameData[+0x9c] = 1; g_GameData[+0x9e] = 1;
    return true;
}

// PointerUpCallback @ 0x00168e48
// Binary: Game[+0x9d]=1; Game[+0x9e]=0.
// TODO: set corresponding Game fields when they are added to Game.h.
static bool PointerUpCallback(InputEvent* ev) {
    (void)ev;
    // TODO: implement PointerUpCallback (binary @ 0x00168e48)
    // Binary: g_GameData[+0x9d] = 1; g_GameData[+0x9e] = 0;
    return true;
}

// PointerDownXboxCallback @ 0x0016a41c
// TODO: implement (binary @ 0x0016a41c)
static bool PointerDownXboxCallback(InputEvent* ev) {
    (void)ev;
    // TODO: implement PointerDownXboxCallback (binary @ 0x0016a41c)
    return true;
}

// PauseGameCallback @ 0x00168fd8
// Binary: if (g_GameData[+2] == 0) PauseGame(); else UnpauseGame();
// g_GameData[+2] = gameActiveFlag in port (0=paused, else=active).
static bool PauseGameCallback(InputEvent* ev) {
    (void)ev;
    Game* game = Game::GetInstance();
    if (!game) return true;
    if (game->gameActiveFlag == 0) {
        PauseScreen::PauseGame();
    } else {
        PauseScreen::UnpauseGame();
    }
    return true;
}

// RegressMenuCallback @ 0x00168e9c
// Binary: g_GameData->m_bRegressMenu = 1.
// TODO: m_bRegressMenu not yet in Game.h; add field at +0x9c (or its actual offset)
//       and replace this stub with: Game::GetInstance()->m_bRegressMenu = 1;
static bool RegressMenuCallback(InputEvent* ev) {
    (void)ev;
    // TODO: implement RegressMenuCallback (binary @ 0x00168e9c)
    // Binary: g_GameData[m_bRegressMenu] = 1; (offset TBD in port)
    return true;
}

// ShowPauseMenuCallback @ 0x00168e6c
// TODO: implement (binary @ 0x00168e6c)
static bool ShowPauseMenuCallback(InputEvent* ev) {
    (void)ev;
    // TODO: implement ShowPauseMenuCallback (binary @ 0x00168e6c)
    return true;
}

// TouchDownCallback -- registered for "TouchReleased_<i>" actions in zone loop.
// Binary addr: TBD from zone-loop decompile; distinct from global PointerDownCallback.
// TODO: implement full body.
static bool TouchDownCallback(InputEvent* ev) {
    (void)ev;
    // TODO: implement TouchDownCallback (zone-loop "TouchReleased_<i>")
    return true;
}
