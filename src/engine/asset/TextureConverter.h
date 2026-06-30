#ifndef FN_ENGINE_ASSET_TEXTURE_CONVERTER_H
#define FN_ENGINE_ASSET_TEXTURE_CONVERTER_H

// Mortar texture-conversion utility functions.
//
// These are standalone Mortar-namespace free functions that do not depend on
// DataStreamReader or the TextureInfo namespace migration.  They live here so
// callers only need a single lightweight header rather than pulling in the full
// TextureFileFormat include tree.
//
// Binary addresses (v1.6.1):
//   COUNT_BITS               @0x0026ba14
//   CorrectHalfTexel         @0x0025805c
//   MultiplyColorComponent   @0x0024b0b0
//   GetConversionChannelRank @0x0026b75c

#include <cstdint>

namespace Mortar { namespace TextureInfo { struct ChannelDescription; } }

namespace Mortar {

// COUNT_BITS(unsigned long) -- parallel bit-population count (Hamming weight).
// Implements the standard parallel-popcount algorithm with the binary's exact
// step-4 mask (0xFFFF00FF on the shifted half, per Ghidra decompile).
// For 32-bit inputs the result is identical to __builtin_popcount.
// Binary v1.6.1 Mortar::COUNT_BITS @0x0026ba14.
int COUNT_BITS(unsigned long x);

// CorrectHalfTexel(float, float&, float&) -- shrink or expand a UV [u,v] range
// by halfTexel on each endpoint to avoid texel-bleed at atlas boundaries.
// If u < v (u is the near end): inset (u += halfTexel, v -= halfTexel).
// Else (reversed or equal):     outset (u -= halfTexel, v += halfTexel).
// Binary v1.6.1 Mortar::CorrectHalfTexel @0x0025805c.
void CorrectHalfTexel(float halfTexel, float& u, float& v);

// MultiplyColorComponent(unsigned char, unsigned char) -- float-path colour multiply.
// result = (float)a/255 * (float)b/255 * 255, truncated to unsigned char.
// Must use the float divide-then-multiply path; integer (a*b+127)/255 differs.
// Binary v1.6.1 Mortar::MultiplyColorComponent @0x0024b0b0.
unsigned char MultiplyColorComponent(unsigned char a, unsigned char b);

// GetConversionChannelRank(ChannelDescription const&, ChannelDescription const&)
// Returns a conversion-cost rank for mapping src channel to dst channel.
// Lower rank = better match.  Combines a type-mismatch penalty (typeRank) with
// a bit-depth penalty (1 << (3 + |bitDiff| + downscale)).
// Binary v1.6.1 Mortar::GetConversionChannelRank @0x0026b75c.
int GetConversionChannelRank(const TextureInfo::ChannelDescription& src,
                             const TextureInfo::ChannelDescription& dst);

}  // namespace Mortar

#endif  // FN_ENGINE_ASSET_TEXTURE_CONVERTER_H
