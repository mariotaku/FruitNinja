#ifndef FN_ENGINE_NETWORK_NETWORK_PACKET_H
#define FN_ENGINE_NETWORK_NETWORK_PACKET_H

// Mortar::NetworkPacket -- base class for all P2P multiplayer packets.
// Defunct: P2P MP -- stub per stub-don't-skip policy.
//
// Binary ctor @ v1.6.1 NetworkPacket::NetworkPacket @ 0x002333a4.
// Signature: NetworkPacket(this, int typeId, int byteSize)
//   byteSize = full derived-class sizeof (passed to NetworkPacket base ctor).
// Layout: vptr @ +0x00 (polymorphic root), total base size = 0x14 (20 bytes):
//   +0x04  int32  m_PacketSize   (= byteSize param = full derived sizeof)
//   +0x08  int32  m_Reserved08   (= 0)
//   +0x0c  int32  m_PacketType   (= typeId param)
//   +0x10  int32  m_Reserved10   (= 0; binary ctor zeroes 4th word at +0x10)

#include <cstdint>
#include "engine/network/ByteBuffer.h"

namespace Mortar {

class NetworkPacket {
public:
    int32_t m_PacketSize;   // +0x04 -- full derived sizeof passed to ctor as byteSize
    int32_t m_Reserved08;   // +0x08 -- initialised to 0
    int32_t m_PacketType;   // +0x0c -- packet type ID (e.g. 0x65, 0x66, 0x67...)
    int32_t m_Reserved10;   // +0x10 -- initialised to 0 (4th word zeroed by base ctor)

    // Defunct: P2P MP -- no-op stub; v1.6.1 NetworkPacket::NetworkPacket @ 0x002333a4
    NetworkPacket() : m_PacketSize(0), m_Reserved08(0), m_PacketType(0), m_Reserved10(0) {}
    virtual ~NetworkPacket() {}

protected:
    // MP-revival: base-ctor overload matching the binary's real 2-arg
    // NetworkPacket(this, int typeId, int byteSize) signature (see class
    // comment above). The default ctor above stays byte-for-byte faithful to
    // the binary's own default (Reserved fields BSS-zeroed); this overload is
    // what every concrete packet subclass should delegate to so m_PacketType
    // and m_PacketSize carry their documented wire values instead of the
    // stub's 0/0 -- otherwise PacketFactory::Create's m_PacketType switch
    // (and NetworkManager::Update's PeekPacketType) never route correctly.
    NetworkPacket(int32_t typeId, int32_t byteSize)
        : m_PacketSize(byteSize), m_Reserved08(0), m_PacketType(typeId), m_Reserved10(0) {}

public:

    // MP-revival: wire (de)serialisation hooks. Not present as distinct
    // virtuals in the binary (the real P2P layer is unrecoverable) -- this is
    // port-only infra for the revived transport. Subclasses call
    // WriteHeader/ReadHeader first, then their own payload fields in the
    // order documented in their header.
    virtual void Serialize(ByteWriter& w) const { WriteHeader(w); }
    virtual void Deserialize(ByteReader& r) { ReadHeader(r); }

protected:
    // Writes the 20-byte base header (m_PacketSize, m_Reserved08, m_PacketType,
    // m_Reserved10) in field order, verbatim from the stored fields (the ctor
    // is responsible for setting m_PacketSize to the derived class's sizeof).
    void WriteHeader(ByteWriter& w) const {
        w.I32(m_PacketSize);
        w.I32(m_Reserved08);
        w.I32(m_PacketType);
        w.I32(m_Reserved10);
    }

    void ReadHeader(ByteReader& r) {
        m_PacketSize = r.I32();
        m_Reserved08 = r.I32();
        m_PacketType = r.I32();
        m_Reserved10 = r.I32();
    }
};

} // namespace Mortar

#if defined(__bada__)
static_assert(sizeof(Mortar::NetworkPacket) == 0x14,
    "Mortar::NetworkPacket must be 20 bytes on ARM32/Bada");
#endif

#endif // FN_ENGINE_NETWORK_NETWORK_PACKET_H
