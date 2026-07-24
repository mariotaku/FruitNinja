#include "game/StartGamePacket.h"

// Defunct: P2P MP game-start packet -- no-op stub; v1.6.1 binary @ 0x00158dc0
// MP-revival: base ctor stamps m_PacketType=103(0x67)/m_PacketSize=sizeof(StartGamePacket)
// so PacketFactory::Create's switch and NetworkManager::Update's PeekPacketType route
// this packet correctly (see NetworkPacket.h's 2-arg base-ctor overload).
StartGamePacket::StartGamePacket()
    : Mortar::NetworkPacket(103, sizeof(StartGamePacket)),
      m_Flags(0x18bb8), m_GameSeed(0)
{
}

// Defunct: P2P MP game-start packet -- no-op stub; v1.6.1 binary @ 0x00158dc0
StartGamePacket::StartGamePacket(int gameSeed)
    : Mortar::NetworkPacket(103, sizeof(StartGamePacket)),
      m_Flags(0x18bb8), m_GameSeed(gameSeed)
{
}

// MP-revival: real wire serialisation; v1.6.1 StartGamePacket::StartGamePacket @ 0x00158dc0
void StartGamePacket::Serialize(Mortar::ByteWriter& w) const {
    NetworkPacket::WriteHeader(w);
    w.I32(m_Flags);
    w.I32(m_GameSeed);
}

// MP-revival: real wire deserialisation (inverse of Serialize above)
void StartGamePacket::Deserialize(Mortar::ByteReader& r) {
    NetworkPacket::ReadHeader(r);
    m_Flags = r.I32();
    m_GameSeed = r.I32();
}
