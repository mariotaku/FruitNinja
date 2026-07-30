// GameTaskInitInput -- v1.6.1 GameTaskInitInput @ 0x001cae0c (thunk @ 0x0011512c).

#include "game/GameTaskInput.h"
#include "Game.h"
#include "input/InputManager.h"
#include "util/StringHash.h"
#include "math/_Vector3.h"
#include "entities/ActorManager.h"
#include "screens/PauseScreen.h"
#include <cstdio>
#include "game/GameWork.h"

// ASM-spec v1.6.1 GameTaskInitInput @ 0x001cae0c (thunk @ 0x0011512c): pending re-verification

// 16-slot touch zone position table.
// Binary: g_TaskState+0xa0..0xa8 region, 12-byte stride (Vec3).
// GOT[+0x77cc] supplies the zero-vec3 default used in the loop.
static _Vector3<float> g_TouchZoneTable[16];

// 16-slot entity pointer table — one Mortar::ActorManager type-3 entity per touch zone.
// Binary: stored at g_TaskState+0x24..+0x60 (4 bytes each, 16 entries).
static Mortar::Entity* g_TouchEntities[16];

// Forward declarations for input callbacks (bodies below).
bool PointerMoveCallback(InputEvent* ev);
bool PointerDownCallback(InputEvent* ev);
bool PointerUpCallback(InputEvent* ev);
bool PointerDownXboxCallback(InputEvent* ev);
static bool PauseGameCallback(InputEvent* ev);
bool RegressMenuCallback(InputEvent* ev);
bool ShowPauseMenuCallback(InputEvent* ev);

// TouchDownCallback -- registered for "TouchDown_<i>" in the per-zone loop.
// v1.6.1 TouchDownCallback @ 0x001cbf18 dispatches InputSink::TouchDown /
// SlashEntity::TouchDown for key codes 0x89+i (distinct from PointerDownCallback
// at 0x001ca2bc which handles global "PointerPressed").
// TODO: v1.6.1 0x001cbf18 (TouchDownCallback) -- port full body once
// InputSink/key-code-0x89+i dispatch is RE'd.
bool TouchDownCallback(InputEvent* ev);


// GameTaskInitInput() -- v1.6.1 @ 0x001cae0c (thunk @ 0x0011512c)
// Initialises per-session input bindings: config load, 16 touch zones,
// and 6 global action callbacks.
void GameTaskInitInput() {
    // --- Section A: Config load @ 0x16967e ---
    // TODO: implement Mortar::InputManager::LoadConfigFile (binary @ 0x1969d8)
    // Binary: InputManager::GetInstance()->LoadConfigFile("Input/Input.txt");
    // Port specific: LoadConfigFile not ported; SDL2 input does not use
    // an action-mapper config file. Call site preserved as comment so
    // call-graph shape matches binary.

    Mortar::InputManager* im = Mortar::InputManager::GetInstance();
    if (!im) return;

    // --- Section B: 16-zone loop, v1.6.1 GameTaskInitInput @ 0x001cae0c ---
    //
    // DIFFERS: binary creates 16 pooled SlashEntity instances here (one per
    //   touch zone). Port owns SlashEntity as a singleton (g_pSlashEntity)
    //   and EntityFactory returns nullptr for type 3, so Add(3) returns
    //   nullptr. Skip the per-zone Mortar::Entity creation + Init for now and leave
    //   g_TouchEntities[i] = nullptr; PointerMoveCallback dispatch must
    //   null-check downstream. Full fix requires SlashEntity to be poolable
    //   (R5+) or a dedicated TouchZoneEntity stub.
    _Vector3<float> defaultPos(0.0f, 0.0f, 0.0f);  // GOT[+0x77cc]
    for (int i = 0; i < 16; ++i) {
        g_TouchZoneTable[i] = defaultPos;

        Mortar::Entity* e = Mortar::ActorManager::GetInstance()->Add(3, true);
        g_TouchEntities[i] = e;

        _Vector3<float> initPos = defaultPos;
        // Binary: Mortar::Entity::vtable[+0x08] called as (nullptr, 0, &initPos).
        // Port-specific null-guard: skip Init when factory refused type 3.
        if (e) e->Init(nullptr, 0, &initPos);

        char nameDown[16], nameMove[16], nameUp[20];
        snprintf(nameDown, sizeof(nameDown), "TouchDown_%d", i);
        snprintf(nameMove, sizeof(nameMove), "TouchMove_X%d", i);
        snprintf(nameUp,   sizeof(nameUp),   "TouchReleased_%d", i);

        // Binary RegisterInputCallback(hash, fnPtr) is 2-arg (no actionFlags).
        im->RegisterInputCallback(StringHash(nameMove), PointerMoveCallback);
        nameMove[10] = 'Y';  // binary in-place byte patch X->Y (local_7a=0x59 @ 0x001caf68)
        im->RegisterInputCallback(StringHash(nameMove), PointerMoveCallback);
        im->RegisterInputCallback(StringHash(nameDown), TouchDownCallback);

        // nameUp ("TouchReleased_<i>") is snprintf'd for stack-layout fidelity
        // only -- v1.6.1 GameTaskInitInput @ 0x001cae0c never hashes/registers
        // it (confirmed by disassembly: no bl StringHash/RegisterInputCallback
        // follows the 3rd snprintf at 0x001caee4).
        (void)nameUp;
    }

    // --- Section C: 6 global named callbacks, v1.6.1 GameTaskInitInput @ 0x001cae0c ---
    // Binary RegisterInputCallback(hash, fnPtr) is 2-arg (no actionFlags param).
    im->RegisterInputCallback(StringHash("PointerMove"),     PointerMoveCallback);       // v1.6.1 @ 0x001cbfcc
    im->RegisterInputCallback(StringHash("PointerPressed"),  PointerDownCallback);       // v1.6.1 @ 0x001ca2bc
    im->RegisterInputCallback(StringHash("PointerReleased"), PointerUpCallback);         // v1.6.1 @ 0x001ca2e4
    im->RegisterInputCallback(StringHash("PointerPressedX"), PointerDownXboxCallback);   // v1.6.1 @ 0x001cbec8
    im->RegisterInputCallback(StringHash("RegressMenu"),     RegressMenuCallback);       // v1.6.1 @ 0x001ca350
    im->RegisterInputCallback(StringHash("ShowPauseMenu"),   ShowPauseMenuCallback);     // v1.6.1 @ 0x001ca310
    // TODO: v1.6.1 PauseScreen::Update @0x001a5f1c -- verify PauseGameCallback
    // (body @ 0x001a5978, thunk @ 0x0010d2ec) wiring site; not registered here
    // in v1.6.1 GameTaskInitInput -- PauseScreen::Update wires it directly.
}

// --- Input callback stubs ---
// Full bodies require InputEvent struct shape + Game field offsets.
// TODO: v1.6.1 -- the 6 addresses below were resolved but bodies not
// re-decompiled against v1.6.1 in this pass (only GameTaskInitInput's call
// sites were corrected); re-verify each body against its v1.6.1 address.

// PointerMoveCallback -- v1.6.1 @ 0x001cbfcc
// DIFFERS: binary multiplexes by event action ID (0x74/0x75/0xCC/0xCD +
// zone ranges 0x99..0xA8/0xA9..0xB8), writing Game.worldPos.x/y,
// per-zone Entity pos.y/z, and dispatching SlashEntity::TouchMoveX/Y
// on the matching slot. Port has no action-ID byte on its InputEvent
// (different ABI than binary; port uses SDL's per-finger TouchMove_n
// callbacks bound to each SlashEntity directly in SlashEntity::Init).
// The per-finger path already covers TouchMoveX/Y dispatch, so this
// global hook stays a no-op pass-through. The worldPos.x/y writes are
// also handled by InputTranslatorSDL on the SDL backend.
bool PointerMoveCallback(InputEvent* /*ev*/) {
    return false;  // pass-through; per-finger handlers do the real work
}

// PointerDownCallback -- v1.6.1 @ 0x001ca2bc -- Game[+0x9c]=1, Game[+0x9e]=1.
// Both fields are per-frame "pointer-down-this-frame" flags consumed
// elsewhere (binary readers not RE'd; cleared per frame somewhere in
// GameUpdate). Wiring them keeps the call-graph binary-faithful.
bool PointerDownCallback(InputEvent* /*ev*/) {
    Game* g = Game::GetInstance();
    if (!g) return false;
    game_work.m_bTouchDownThisFrame = 1;
    game_work.m_bPointerActive = 1;
    return false;
}

// PointerUpCallback -- v1.6.1 @ 0x001ca2e4 -- Game[+0x9d]=1, Game[+0x9e]=0.
bool PointerUpCallback(InputEvent* /*ev*/) {
    Game* g = Game::GetInstance();
    if (!g) return false;
    game_work.m_bTouchUpThisFrame = 1;
    game_work.m_bPointerActive = 0;
    return false;
}

// PointerDownXboxCallback -- v1.6.1 @ 0x001cbec8
// Despite the "Xbox" name this is the down-edge handler used when the
// input config supplies a "PointerPressedX" action. Binary writes same
// Game fields as PointerDownCallback then dispatches SlashEntity::
// TouchDown on the matching per-finger entity. Port covers the
// TouchDown dispatch via per-finger SlashEntity callbacks bound in
// SlashEntity::Init -- so just the Game-field writes here.
bool PointerDownXboxCallback(InputEvent* /*ev*/) {
    Game* g = Game::GetInstance();
    if (!g) return false;
    game_work.m_bTouchDownThisFrame = 1;
    game_work.m_bPointerActive = 1;
    return false;
}

// v1.6.1 PauseScreen::PauseGameCallback @ 0x001a5978 (thunk @ 0x0010d2ec).
// Wired from PauseScreen::Update (@ 0x001a5f1c/0x001a5f24), not from
// GameTaskInitInput -- see TODO above Section C.
// Binary: if (ev != NULL) { if (g_GameData[+2] == 0) PauseGame(); else UnpauseGame(); }
// g_GameData[+2] = pausedFlag in port (false=running, true=paused).
// ASM-spec v1.6.1 PauseGameCallback @ 0x001a5978: pending re-verification
static bool PauseGameCallback(InputEvent* ev) {
    if (!ev) return true;
    Game* game = Game::GetInstance();
    if (!game) return true;
    if (!game_work.bM_Mode) {
        PauseGame();
    } else {
        UnpauseGame();
    }
    return true;
}

// RegressMenuCallback -- v1.6.1 @ 0x001ca350
// Binary: g_GameData[+0x604] = 1; (unconditional)
// +0x604 is m_bFrameDirty in port (same slot ShowPauseMenuCallback writes
// when its gate passes -- both actions flip the same "menu input pending"
// latch consumed downstream).
// ASM-spec v1.6.1 RegressMenuCallback @ 0x001ca350: pending re-verification
bool RegressMenuCallback(InputEvent* ev) {
    (void)ev;
    Game* g = Game::GetInstance();
    if (!g) return true;
    game_work.m_bFrameDirty = 1;
    return true;
}

// ShowPauseMenuCallback -- v1.6.1 @ 0x001ca310
// Binary: if (m_TransitionTimer == 0.0f && pausedFlag == 0)
//             g_GameData[+0x604] = 1;
// +0x604 is m_bFrameDirty -- same field as RegressMenuCallback.
// ASM-spec v1.6.1 ShowPauseMenuCallback @ 0x001ca310: pending re-verification
bool ShowPauseMenuCallback(InputEvent* ev) {
    (void)ev;
    Game* g = Game::GetInstance();
    if (!g) return true;
    if (game_work.m_PauseAmount == 0.0f && !game_work.bM_Mode) {
        game_work.m_bFrameDirty = 1;
    }
    return true;
}

// TouchDownCallback -- registered for "TouchDown_<i>" actions in the zone
// loop. v1.6.1 TouchDownCallback @ 0x001cbf18 dispatches InputSink::TouchDown
// / SlashEntity::TouchDown for key codes 0x89+i. Port covers this via
// per-finger TouchDown_n callbacks bound in SlashEntity::Init directly --
// so this global hook is a no-op pass-through.
bool TouchDownCallback(InputEvent* /*ev*/) {
    return false;  // pass-through; per-finger TouchDown_n handlers do the work
}
