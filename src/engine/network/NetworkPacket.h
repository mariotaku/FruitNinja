#ifndef FN_ENGINE_NETWORK_NETWORK_PACKET_H
#define FN_ENGINE_NETWORK_NETWORK_PACKET_H

// Mortar::NetworkPacket -- base class for all P2P multiplayer packets.
// Defunct: P2P MP -- stub per stub-don't-skip policy.
//
// Binary ctor @ 0x00102c3c (thunk via PTR_NetworkPacket_001f1364).
// Layout: vptr @ +0x00 (polymorphic root), payload fields @ +0x04..+0x13.
// Total base size = 0x14 (20 bytes) as passed to ctor:
//   NetworkPacket::NetworkPacket(this, typeId, byteSize)
// where byteSize is the FULL derived-class size.

#include <cstdint>

namespace Mortar {

class NetworkPacket {
public:
    // +0x04: packet type ID (e.g. 0x65 for FruitSlicedPacket, 0x66 for WaveSyncPacket)
    uint8_t  m_typeId;        // +0x04
    uint8_t  m_pad05[3];     // +0x05..+0x07
    // +0x08..+0x13: remaining NetworkPacket header fields (size, flags, etc.)
    // Semantics not RE'd; preserved as opaque pad to maintain offset 0x14 for derived.
    uint8_t  m_headerPad[12]; // +0x08..+0x13

    // Defunct: P2P MP -- no-op stub; binary @ 0x00102c3c
    NetworkPacket() {}
    virtual ~NetworkPacket() {}
};

} // namespace Mortar

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(Mortar::NetworkPacket) == 0x14,
    "Mortar::NetworkPacket must be 20 bytes on ARM32/Bada");
#endif

#endif // FN_ENGINE_NETWORK_NETWORK_PACKET_H
