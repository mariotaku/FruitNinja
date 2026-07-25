#include "game/StartGamePacket.h"

// Defunct: P2P MP game-start packet -- no-op stub; v1.6.1 binary @ 0x00158dc0
// Base ctor stamps m_PacketType=103(0x67)/m_PacketSize=sizeof(StartGamePacket),
// matching the binary's ctor chain to NetworkPacket(typeId, byteSize).
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
