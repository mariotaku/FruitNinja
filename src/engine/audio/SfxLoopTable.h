#pragma once
#include <stdint.h>
namespace Mortar {
// Loop-start in SAMPLES for a sound name (basename, no extension, lowercase), or 0 if none/unknown.
// Defined by the build-generated SfxLoopTable.generated.cpp (stage-assets.py --ogg-audio).
uint32_t SfxLoopStartSamples(const char* name);
}
