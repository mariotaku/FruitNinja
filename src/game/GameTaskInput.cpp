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

// 16-slot entity pointer table — one Mortar::ActorManager type-3 entity per touch zone.
// Binary: stored at g_TaskState+0x24..+0x60 (4 bytes each, 16 entries).
static Mortar::Entity* g_TouchEntities[16];

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

    Mortar::InputManager* im = Mortar::InputManager::GetInstance();
    if (!im) return;

    // --- Section B: 16-zone loop @ 0x169690 ---
    // Binary @ 0x00169670: TouchDown registered TWICE, TouchMove_X computed but unused -- preserve verbatim.
    //
    // DIFFERS: binary creates 16 pooled SlashEntity instances here (one per
    //   touch zone). Port owns SlashEntity as a singleton (g_pSlashEntity)
    //   and EntityFactory returns nullptr for type 3, so Add(3) returns
    //   nullptr. Skip the per-zone Mortar::Entity creation + Init for now and leave
    //   g_TouchEntities[i] = nullptr; PointerMoveCallback dispatch must
    //   null-check downstream. Full fix requires SlashEntity to be poolable
    //   (R5+) or a dedicated TouchZoneEntity stub.
    Vec3 defaultPos(0.0f, 0.0f, 0.0f);  // GOT[+0x77cc]
    for (int i = 0; i < 16; ++i) {
        g_TouchZoneTable[i] = defaultPos;

        Mortar::Entity* e = Mortar::ActorManager::GetInstance()->Add(3, true);
        g_TouchEntities[i] = e;

        Vec3 initPos = defaultPos;
        // Binary: Mortar::Entity::vtable[+0x08] called as (nullptr, 0, &initPos).
        // Port-specific null-guard: skip Init when factory refused type 3.
        if (e) e->Init(nullptr, 0, &initPos);

        char nameDown[16], nameMove[16], nameUp[20];
        snprintf(nameDown, sizeof(nameDown), "TouchDown_%d", i);
        snprintf(nameMove, sizeof(nameMove), "TouchMove_X%d", i);
        snprintf(nameUp,   sizeof(nameUp),   "TouchReleased_%d", i);

        // Binary @ 0x00169670: TouchDown_<i> registered TWICE -- preserve verbatim.
        // Binary RegisterInputCallback(hash, fnPtr) is 2-arg (no actionFlags).
        im->RegisterInputCallback(StringHash(nameDown), PointerMoveCallback);
        im->RegisterInputCallback(StringHash(nameDown), PointerMoveCallback);
        im->RegisterInputCallback(StringHash(nameUp),   TouchDownCallback);

        // nameMove hash is computed but never registered in binary -- snprintf
        // called to match binary stack layout; hash deliberately not registered.
        (void)nameMove;
    }

    // --- Section C: 7 global named callbacks @ 0x169a32 ---
    // Binary RegisterInputCallback(hash, fnPtr) is 2-arg (no actionFlags param).
    im->RegisterInputCallback(StringHash("PointerMove"),     PointerMoveCallback);       // binary @ 0x0016a4b4
    im->RegisterInputCallback(StringHash("PointerPressed"),  PointerDownCallback);       // binary @ 0x00168e24
    im->RegisterInputCallback(StringHash("PointerReleased"), PointerUpCallback);         // binary @ 0x00168e48
    im->RegisterInputCallback(StringHash("PointerPressedX"), PointerDownXboxCallback);   // binary @ 0x0016a41c
    im->RegisterInputCallback(StringHash("PauseGame"),       PauseGameCallback);         // binary @ 0x00168fd8
    im->RegisterInputCallback(StringHash("RegressMenu"),     RegressMenuCallback);       // binary @ 0x00168e9c
    im->RegisterInputCallback(StringHash("ShowPauseMenu"),   ShowPauseMenuCallback);     // binary @ 0x00168e6c
}

// --- Input callback stubs ---
// Full bodies require InputEvent struct shape + Game field offsets.
// Binary addresses in comments are from the decompile of GameTaskInitInput.

// PointerMoveCallback @ 0x0016a4b4 (re-analyst 2026-05-18)
// DIFFERS: binary multiplexes by event action ID (0x74/0x75/0xCC/0xCD +
// zone ranges 0x99..0xA8/0xA9..0xB8), writing Game.worldPos.x/y,
// per-zone Entity pos.y/z, and dispatching SlashEntity::TouchMoveX/Y
// on the matching slot. Port has no action-ID byte on its InputEvent
// (different ABI than binary; port uses SDL's per-finger TouchMove_n
// callbacks bound to each SlashEntity directly in SlashEntity::Init).
// The per-finger path already covers TouchMoveX/Y dispatch, so this
// global hook stays a no-op pass-through. The worldPos.x/y writes are
// also handled by InputTranslatorSDL on the SDL backend.
static bool PointerMoveCallback(InputEvent* /*ev*/) {
    return false;  // pass-through; per-finger handlers do the real work
}

// PointerDownCallback @ 0x00168e24 -- Game[+0x9c]=1, Game[+0x9e]=1.
// Both fields are per-frame "pointer-down-this-frame" flags consumed
// elsewhere (binary readers not RE'd; cleared per frame somewhere in
// GameUpdate). Wiring them keeps the call-graph binary-faithful.
static bool PointerDownCallback(InputEvent* /*ev*/) {
    Game* g = Game::GetInstance();
    if (!g) return false;
    g->field_0x9c = 1;
    g->m_bPointerActive = 1;
    return false;
}

// PointerUpCallback @ 0x00168e48 -- Game[+0x9d]=1, Game[+0x9e]=0.
static bool PointerUpCallback(InputEvent* /*ev*/) {
    Game* g = Game::GetInstance();
    if (!g) return false;
    g->field_0x9d = 1;
    g->m_bPointerActive = 0;
    return false;
}

// PointerDownXboxCallback @ 0x0016a41c (re-analyst 2026-05-18)
// Despite the "Xbox" name this is the down-edge handler used when the
// input config supplies a "PointerPressedX" action. Binary writes same
// Game fields as PointerDownCallback then dispatches SlashEntity::
// TouchDown on the matching per-finger entity. Port covers the
// TouchDown dispatch via per-finger SlashEntity callbacks bound in
// SlashEntity::Init -- so just the Game-field writes here.
static bool PointerDownXboxCallback(InputEvent* /*ev*/) {
    Game* g = Game::GetInstance();
    if (!g) return false;
    g->field_0x9c = 1;
    g->m_bPointerActive = 1;
    return false;
}

// PauseGameCallback @ 0x00168fd8
// Binary: if (ev != NULL) { if (g_GameData[+2] == 0) PauseGame(); else UnpauseGame(); }
// g_GameData[+2] = pausedFlag in port (false=running, true=paused).
// ASM-verified: 2026-05-18 binary @ 0x00168fd8 (re-analyst)
static bool PauseGameCallback(InputEvent* ev) {
    if (!ev) return true;
    Game* game = Game::GetInstance();
    if (!game) return true;
    if (!game->pausedFlag) {
        PauseScreen::PauseGame();
    } else {
        PauseScreen::UnpauseGame();
    }
    return true;
}

// RegressMenuCallback @ 0x00168e9c
// Binary: g_GameData[+0x604] = 1; (unconditional)
// +0x604 is m_bFrameDirty in port (same slot ShowPauseMenuCallback writes
// when its gate passes -- both actions flip the same "menu input pending"
// latch consumed downstream).
// ASM-verified: 2026-05-18 binary @ 0x00168e9c (re-analyst)
static bool RegressMenuCallback(InputEvent* ev) {
    (void)ev;
    Game* g = Game::GetInstance();
    if (!g) return true;
    g->m_bFrameDirty = 1;
    return true;
}

// ShowPauseMenuCallback @ 0x00168e6c
// Binary: if (m_TransitionTimer == 0.0f && pausedFlag == 0)
//             g_GameData[+0x604] = 1;
// +0x604 is m_bFrameDirty -- same field as RegressMenuCallback.
// ASM-verified: 2026-05-18 binary @ 0x00168e6c (re-analyst)
static bool ShowPauseMenuCallback(InputEvent* ev) {
    (void)ev;
    Game* g = Game::GetInstance();
    if (!g) return true;
    if (g->m_TransitionTimer == 0.0f && !g->pausedFlag) {
        g->m_bFrameDirty = 1;
    }
    return true;
}

// TouchDownCallback -- misnamed; registered for "TouchReleased_<i>" actions
// in the zone loop (binary GOT slot 0x00169a64 trampoline). Per re-analyst
// the binary dispatches SlashEntity::TouchUp on the matching per-zone entity.
// Port covers this via per-finger TouchUp_n callbacks bound in SlashEntity::
// Init directly -- so this global hook is a no-op pass-through.
static bool TouchDownCallback(InputEvent* /*ev*/) {
    return false;  // pass-through; per-finger TouchUp_n handlers do the work
}
