#ifndef FN_GAME_PRELOAD_SOUNDS_H
#define FN_GAME_PRELOAD_SOUNDS_H

// PreloadSounds -- binary 0x00101cac.
// Calls SoundManager::PreLoadSound for 25 hard-coded WAV names + per-fruit sounds
// + 7 arcade%d.wav variants + 3 other %d.wav patterns.
//
// Without this, sounds load on-demand causing audible hitches on first slice/explosion.
//
// TODO: implement full body -- see docs/engine/initialisation-asm-audit.md Section 2 call #48.

void PreloadSounds();

#endif // FN_GAME_PRELOAD_SOUNDS_H
