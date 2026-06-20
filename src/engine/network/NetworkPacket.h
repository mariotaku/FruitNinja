#ifndef FN_ENGINE_NETWORK_NETWORK_PACKET_H
#define FN_ENGINE_NETWORK_NETWORK_PACKET_H

// Mortar::NetworkPacket -- base class for all P2P multiplayer packets.
// Defunct: P2P MP -- stub per stub-don't-skip policy.
//
// Binary ctor @ v1.6.1 NetworkPacket::NetworkPacket @ 0x002333a4.
// Signature: NetworkPacket(this, int typeId, int byteSize)
//   byteSize = full derived-class sizeof (passed to NetworkPacket base ctor).
// Layout: vptr @ +0x00 (polymorphic root), total base size = 0x10 (16 bytes):
//   +0x04  int32  m_PacketSize   (= byteSize param = full derived sizeof)
//   +0x08  int32  m_Reserved08   (= 0)
//   +0x0c  int32  m_PacketType   (= typeId param)

#include <cstdint>

namespace Mortar {

class NetworkPacket {
public:
    int32_t m_PacketSize;   // +0x04 -- full derived sizeof passed to ctor as byteSize
    int32_t m_Reserved08;   // +0x08 -- initialised to 0
    int32_t m_PacketType;   // +0x0c -- packet type ID (e.g. 0x65, 0x66, 0x67...)

    // Defunct: P2P MP -- no-op stub; v1.6.1 NetworkPacket::NetworkPacket @ 0x002333a4
    NetworkPacket() : m_PacketSize(0), m_Reserved08(0), m_PacketType(0) {}
    virtual ~NetworkPacket() {}
};

} // namespace Mortar

#if defined(__bada__)
static_assert(sizeof(Mortar::NetworkPacket) == 0x10,
    "Mortar::NetworkPacket must be 16 bytes on ARM32/Bada");
#endif

#endif // FN_ENGINE_NETWORK_NETWORK_PACKET_H
