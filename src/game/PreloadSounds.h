#ifndef FN_GAME_PRELOAD_SOUNDS_H
#define FN_GAME_PRELOAD_SOUNDS_H

// v1.6.1 PreloadSounds @0x0011bb94 (_Z13PreloadSoundsv).
// Calls SoundManager::Initialise(nullptr), then 24 hard-coded sound names,
// then per-fruit sounds from FRUIT_INFO, then 7 Sword-swipe-N and 3 Visceral-impact-N.
// Without this, sounds load on-demand causing audible hitches on first slice/explosion.

void PreloadSounds();

#endif // FN_GAME_PRELOAD_SOUNDS_H
