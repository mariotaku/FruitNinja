#ifndef FN_GAME_TASK_INPUT_H
#define FN_GAME_TASK_INPUT_H

// GameTaskInitInput -- v1.6.1 GameTaskInitInput @ 0x001cae0c (thunk @ 0x0011512c).
//
// Sets up per-session input bindings, in this order:
//   - InputManager::LoadConfigFile("Input/Input.txt")   (Defunct stub in the port)
//   - 16-slot loop: zero game_work.m_FingerSpawnPos[i], allocate one pooled
//     type-3 entity (SlashEntity) via Mortar::ActorManager::Add(3, true) into
//     g_pSlashEntities[i], Init it, then RegisterInputCallback x3 --
//     "TouchMove_X<i>" and "TouchMove_Y<i>" -> PointerMoveCallback,
//     "TouchDown_<i>" -> TouchDownCallback. "TouchReleased_<i>" is formatted and
//     then dropped: v1.6.1 binds NO per-finger release callback.
//   - 6 global named callbacks: PointerMove, PointerPressed, PointerReleased,
//     PointerPressedX, RegressMenu, ShowPauseMenu.
//
// This is the ONLY place the blade is wired to input. SlashEntity does not
// subscribe to the InputManager itself.
//
// Port specific: because it is the only seam, it is also where the two
// blade-suppression rules live (both fold away under __bada__):
//   - the settings modal captures input, so no slot feeds a blade while it is up;
//   - while FN::g_MotionMode is ON, a slot that originated on
//     FN::MOTION_CLICK_ONLY_CHANNEL (the mouse's UI channel) feeds no blade, so
//     a click drives widgets only and never draws or cuts. The blade then comes
//     from the hover channel alone.
// Both are per-slot decisions taken here, NOT at the SDL layer -- dropping the
// finger in the translator is what used to move the press edge to button-UP.

void GameTaskInitInput();

struct InputEvent;

// v1.6.1 TouchDownCallback @ 0x001cbf18 -- handler for "TouchDown_<i>". Offers
// the press to game_work.m_pActiveTouchSink first, else runs
// g_pSlashEntities[n]->TouchDown, then stamps m_FingerSpawnPos[n].z (2 = press
// edge, 1 = held). Fires EVERY tick a finger is held, not just on the edge.
// n comes from the button key id at InputEvent +0x08 (m_KeyId - 0x89).
bool TouchDownCallback(InputEvent* ev);

// v1.6.1 PointerMoveCallback @ 0x001cbfcc -- handler for "PointerMove" AND for
// every "TouchMove_X<i>" / "TouchMove_Y<i>". Branches on the axis key code at
// InputEvent +0x06: 0x74/0x75 write game_work.worldPos, 0x99..0xa8 and
// 0xa9..0xb8 drive the sink or g_pSlashEntities[n] and store
// m_FingerSpawnPos[n].x / .y.
bool PointerMoveCallback(InputEvent* ev);

// v1.6.1 RegressMenuCallback @ 0x001ca350 -- the "RegressMenu" action handler
// (back-key input). Unconditionally sets game_work.m_bFrameDirty (see
// GameTaskInput.cpp), which MenuButton::Update's back-key force-slice block
// (m_bBackdropActive-gated) picks up and routes to whichever menu screen's
// back-bomb button is currently active. Declared here (not just file-local
// in GameTaskInput.cpp) so other translation units (GameSDL.cpp's ESC
// handler) can call it directly instead of poking m_bFrameDirty themselves.
bool RegressMenuCallback(InputEvent* ev);

#endif  // FN_GAME_TASK_INPUT_H
