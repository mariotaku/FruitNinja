#pragma once
#include <stdint.h>
namespace Mortar {
// Build-generated loop-point lookup for Ogg-transcoded sfx (the .ogg carries
// no loop metadata; the raw .wav.pcm 20-byte header did). Defined by
// SfxLoopTable.generated.cpp, emitted by stage-assets.py --gen-loop-table
// from the same header parse that drives the Ogg transcode. Keys are the
// lowercased basename, no extension. Both return the "no loop / unknown
// name" sentinel 0 -- entries only exist for loopStart > 0.
//
// Loop-start in SAMPLES (raw header word[4]) -- SDL/stb_vorbis consumer,
// which decodes at the source rate so sample offsets apply directly.
uint32_t SfxLoopStartSamples(const char* name);
// Loop-start in SECONDS (loopStart / source sample rate) -- Web Audio
// consumer: decodeAudioData resamples to ctx.sampleRate, so the source rate
// is unrecoverable from the decoded AudioBuffer and source.loopStart takes
// seconds. Derived from the same generated {loopStart, rate} entry.
double SfxLoopStartSeconds(const char* name);
}
