#ifndef FN_GAME_SETUP_GAME_WORK_H
#define FN_GAME_SETUP_GAME_WORK_H

// SetupGameWork -- binary 0x0010b4e8.
// Writes 16 game-state fields including gameMode default (Classic=2 in binary),
// score threshold, music/sound fields, increments "plays_total" save counter,
// and copies saveData+0x110 into Game+0x30.
//
// TODO: implement full body -- see docs/engine/initialisation-asm-audit.md Section 4.

void SetupGameWork();

#endif // FN_GAME_SETUP_GAME_WORK_H
