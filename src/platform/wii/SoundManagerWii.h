#ifndef FN_PLATFORM_WII_SOUND_MANAGER_WII_H
#define FN_PLATFORM_WII_SOUND_MANAGER_WII_H

// Port specific: Wii-only audio pump seam.
//
// SoundManagerWii.cpp streams music (see that file's header comment) via two
// ping-pong ASND buffers refilled from disk. ASND runs the mixer in an audio
// interrupt with no per-frame hook the port controls, and refilling a buffer
// means a (potentially slow, SD/USB/DVD) blocking file read -- which must
// NEVER happen inside an interrupt handler. So the refill is driven from the
// main thread instead: mainWii.cpp's loop calls AudioStreamPump() once per
// frame (alongside g_game.tickRealtimeUi(), see mainWii.cpp), and the pump
// checks whether ASND has drained a buffer and, if so, reads + queues the
// next chunk.
//
// Deliberately NOT a SoundManager virtual / vtable method: the binary's
// SoundManager has no such slot (see src/engine/audio/SoundManager.h), and
// every other backend (SDL/WebAudio) needs no equivalent pump (their audio
// callback pulls samples directly from a fully RAM-resident buffer). Adding
// it to the shared class would be a Wii-only addition to a binary-faithful
// vtable, which the "stub-don't-skip" / vtable-fidelity policy forbids
// (CLAUDE.md "Binary fidelity for tooling" -- vtable slot count/order must
// match the binary). A free function in a Wii-only header has no such
// constraint.
//
// Only declared/defined when FRUIT_PLATFORM_WII is set.
#ifdef FRUIT_PLATFORM_WII

namespace fn {
namespace wii {

// Call once per frame from mainWii.cpp's main loop. No-op if no music is
// currently streaming (SoundManager::SongPlay hasn't been called, or
// SongStop/SongPause has silenced it). Cheap when idle: a couple of
// ASND_StatusVoice polls, no allocation.
void AudioStreamPump();

} // namespace wii
} // namespace fn

#endif // FRUIT_PLATFORM_WII

#endif // FN_PLATFORM_WII_SOUND_MANAGER_WII_H
