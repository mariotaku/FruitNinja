#ifndef FN_GAME_PRELOAD_SOUNDS_H
#define FN_GAME_PRELOAD_SOUNDS_H

// PreloadSounds -- binary @ 0x0010b204 (PLT stub @ 0x00101cac).
// Calls SoundManager::Initialise(nullptr), then 24 hard-coded sound names,
// then per-fruit sounds from FRUIT_INFO, then 7 Sword-swipe-N and 3 Visceral-impact-N.
// Without this, sounds load on-demand causing audible hitches on first slice/explosion.

void PreloadSounds();

#endif // FN_GAME_PRELOAD_SOUNDS_H
