#ifndef FN_GAME_TASK_INPUT_H
#define FN_GAME_TASK_INPUT_H

// GameTaskInitInput -- free function matching binary 0x00169670 (357 lines).
//
// Sets up per-session input bindings:
//   - InputManager::LoadConfigFile
//   - 16-slot rotated touch region loop: ActorManager::Add(3, true) per slot,
//     Entity::Init with zone position, InputManager::RegisterInputCallback x3
//     per slot (touch/swipe/move handlers, "touchN"/"swipeN"/"moveN").
//   - 7 global input callbacks (keys, accelerometer, etc.)
//
// Stored results: per-slot Entity ptrs -> g_TaskState +0x24..+0x60
//                 per-slot Vec3 zones  -> g_TaskState-adjacent InputZones array
//                 callback table       -> InputManager singleton.
//
// TODO: full body -- see docs/systems/gameinit-todos.md step 18.
// TODO: multi-touch / multi-player input zones (16-slot loop body).

void GameTaskInitInput();

#endif  // FN_GAME_TASK_INPUT_H
