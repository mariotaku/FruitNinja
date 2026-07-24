#ifndef FN_GAME_TASK_INPUT_H
#define FN_GAME_TASK_INPUT_H

// GameTaskInitInput -- free function matching binary 0x00169670 (357 lines).
//
// Sets up per-session input bindings:
//   - InputManager::LoadConfigFile
//   - 16-slot rotated touch region loop: Mortar::ActorManager::Add(3, true) per slot,
//     Mortar::Entity::Init with zone position, InputManager::RegisterInputCallback x3
//     per slot (touch/swipe/move handlers, "touchN"/"swipeN"/"moveN").
//   - 7 global input callbacks (keys, accelerometer, etc.)
//
// Stored results: per-slot Mortar::Entity ptrs -> g_TaskState +0x24..+0x60
//                 per-slot Vec3 zones  -> g_TaskState-adjacent InputZones array
//                 callback table       -> InputManager singleton.
//
// TODO: full body.
// TODO: multi-touch / multi-player input zones (16-slot loop body).

void GameTaskInitInput();

struct InputEvent;

// v1.6.1 RegressMenuCallback @ 0x001ca350 -- the "RegressMenu" action handler
// (back-key input). Unconditionally sets game_work.m_bFrameDirty (see
// GameTaskInput.cpp), which MenuButton::Update's back-key force-slice block
// (m_bBackdropActive-gated) picks up and routes to whichever menu screen's
// back-bomb button is currently active. Declared here (not just file-local
// in GameTaskInput.cpp) so other translation units (GameSDL.cpp's ESC
// handler) can call it directly instead of poking m_bFrameDirty themselves.
bool RegressMenuCallback(InputEvent* ev);

#endif  // FN_GAME_TASK_INPUT_H
