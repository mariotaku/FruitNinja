#ifndef FN_GAME_FRUIT_SLICED_PACKET_H
#define FN_GAME_FRUIT_SLICED_PACKET_H

// FruitSlicedPacket -- P2P multiplayer fruit-slice broadcast packet.
// Defunct: P2P MP -- stub per stub-don't-skip policy.
//
// Binary ctors @ 0x0012cda8 (parameterised), 0x0012ce48 (default).
// Polymorphic: vptr @ +0x00 (via base Mortar::NetworkPacket).
// Packet type ID = 0x65, total packet payload size = 0x24 (36 bytes).
// Mortar::NetworkPacket base occupies +0x00..+0x0f (16 bytes).
// FruitSlicedPacket own fields start at +0x10 (20 bytes of own fields).

#include "engine/network/NetworkPacket.h"
#include <cstdint>

class FruitSlicedPacket : public Mortar::NetworkPacket {
public:
    // Wire fields in binary serialize order (FruitSlicedPacket::Serialize @ 0x00156f80,
    // ctor @ 0x001570b0). NOTE port offsets are 4 less than the binary's actual byte
    // offsets (see header banner) -- binary writes fruitId@+0x14, x@+0x18, y@+0x1a,
    // angle@+0x1c, player@+0x20; the +0x20 reserved word here has no ctor/serialize site.
    long     m_FruitId;     // +0x10 -- ID of sliced fruit (WriteInt32; ctor param 1)
    uint16_t m_SliceX;      // +0x14 -- slice point X, 16-bit (WriteInt16; ctor param 2)
    uint16_t m_SliceY;      // +0x16 -- slice point Y, 16-bit (WriteInt16; ctor param 3)
    float    m_SliceAngle;  // +0x18 -- slice direction/angle (WriteDec32 float; ctor param 4)
    long     m_PlayerIdx;   // +0x1c -- player/blade slot (WriteInt32; ctor param 5)
    long     m_reserved20;  // +0x20 -- purpose unknown; no ctor/serialize site in binary

    // Defunct: P2P MP slice-broadcast packet -- no-op stub; v1.6.1 binary @ 0x0012ce48
    FruitSlicedPacket();

    // Defunct: P2P MP slice-broadcast packet -- no-op stub; v1.6.1 binary @ 0x0012cda8
    FruitSlicedPacket(long fruitId, uint16_t sliceX, uint16_t sliceY, float sliceAngle, long playerIdx, long reserved20);

    virtual ~FruitSlicedPacket() {}
};

#if defined(__bada__)
static_assert(sizeof(FruitSlicedPacket) == 36,
    "FruitSlicedPacket must be 36 bytes on ARM32/Bada");
#endif

#endif // FN_GAME_FRUIT_SLICED_PACKET_H
