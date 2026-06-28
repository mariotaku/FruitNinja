#ifndef FN_UPDATE_MUSIC_H
#define FN_UPDATE_MUSIC_H

// UpdateMusic — music crossfade state machine
// Binary: 0x0016a68c
// Called every frame from GameUpdate when LoadingJob::IsLoaded().

// Analysed: 2026-04-26T00:00

void UpdateMusic(float dt);

// v1.6.1 PreloadInGameSounds @0x001cad28 — one-shot preload of in-game SFX.
// Idempotent (internal guard); safe to call from multiple paths (SkipToPause, UpdateMusic).
void PreloadInGameSounds();

#endif  // FN_UPDATE_MUSIC_H
