#include "Colour.h"

// TODO: 0x00183f58 -- Colour::Lerp(Colour const&, float) const: build a, *this as
//   Colours then delegate to the 3-arg Lerp(this, a, *this, t).
void Colour::Lerp(Colour const&, float) const {}
// TODO: 0x00183e98 -- Colour::Lerp(Colour, Colour, float): this = a; per channel
//   this -= (b - a) * t (R/G/B/A), then clamp each to >= 0 (signed->float, truncate).
void Colour::Lerp(Colour, Colour, float) {}
// TODO: 0x00183f98 -- Colour::ToString() const: snprintf ARGB (a,r,g,b) into a
//   static 0x100 buffer and return it.
void Colour::ToString() const {}
