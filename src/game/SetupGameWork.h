#ifndef FN_GAME_SETUP_GAME_WORK_H
#define FN_GAME_SETUP_GAME_WORK_H

// SetupGameWork -- binary 0x0010b4e8.
// Writes 23 game-state fields, increments "plays_total" save counter,
// and copies saveData+0x110 into Game+0x30.

void SetupGameWork();

#endif // FN_GAME_SETUP_GAME_WORK_H
