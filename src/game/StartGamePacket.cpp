#include "game/StartGamePacket.h"

// Defunct: P2P MP game-start packet -- no-op stub; v1.6.1 binary @ 0x00158dc0
// MP-revival: base ctor stamps m_PacketType=103(0x67)/m_PacketSize=sizeof(StartGamePacket)
// so PacketFactory::Create's switch and NetworkManager::Update's PeekPacketType route
// this packet correctly (see NetworkPacket.h's 2-arg base-ctor overload).
// Default ctor: cmd=0 (invalid/unset) -- only used as a Deserialize target.
StartGamePacket::StartGamePacket()
    : Mortar::NetworkPacket(103, sizeof(StartGamePacket)),
      m_Cmd(0), m_Value(0)
{
}

// MP-revival: cmd/value ctor -- see header for cmd meanings (1=ready,
// 2=seed+go, 3=mode).
StartGamePacket::StartGamePacket(int32_t cmd, int32_t value)
    : Mortar::NetworkPacket(103, sizeof(StartGamePacket)),
      m_Cmd(cmd), m_Value(value)
{
}

// MP-revival: real wire serialisation
void StartGamePacket::Serialize(Mortar::ByteWriter& w) const {
    NetworkPacket::WriteHeader(w);
    w.I32(m_Cmd);
    w.I32(m_Value);
}

// MP-revival: real wire deserialisation (inverse of Serialize above)
void StartGamePacket::Deserialize(Mortar::ByteReader& r) {
    NetworkPacket::ReadHeader(r);
    m_Cmd = r.I32();
    m_Value = r.I32();
}
