# StringHash Algorithm

Binary: 0x00252a10 (v1.6.1) / 0x0019c5d4 (v1.0)
ASM-verified: 2026-06-12 binary @ 0x00252a10 / 0x0019c5d4 (re-analyst)

Bob Jenkins' **lookup3** hash with case-insensitive folding. Used throughout the game for asset references, stat keys, achievement IDs, and particle emitter lookups.

Verified test vectors (case-insensitive):
- "watermelon" -> 0x158bc245
- "apple_red"  -> 0xdac1f38f
- "banana"     -> 0x5ff2eb92

The canonical implementation is in `src/engine/util/StringHash.cpp`.

## Key Properties

- **Case-insensitive**: "Apple" and "apple" produce the same hash
- **Initial value**: `a = b = 0x9e3779b9; c = 0x805;` -- no `+ len` at init
- **Len addition**: `c += len` once, after the main 12-byte loop, before the tail switch
- **Main-loop mix**: both subtractions before XOR in each step: `((a-c)-b)^(c>>13)` etc.
- **Final mix**: uses a temp variable `t` and a non-obvious variable rotation (see source)
- **Little-endian byte packing**: bytes assembled as `b0 + (b1<<8) + (b2<<16) + (b3<<24)`
- **Used for**: FRUIT_INFO name hashes, particle emitter IDs, stat keys, achievement IDs, sound name lookups

## Also: FileStringHash (0x0019c394)

101 lines. Similar Jenkins hash but with different initial values and used for file path hashing. Not needed for gameplay port.

