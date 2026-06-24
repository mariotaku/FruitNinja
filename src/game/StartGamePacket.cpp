#include "game/StartGamePacket.h"

// Defunct: P2P MP game-start packet -- no-op stub; v1.6.1 binary @ 0x00158dc0
StartGamePacket::StartGamePacket()
    : m_Flags(0x18bb8), m_GameSeed(0), m_reserved18(0)
{
}

// Defunct: P2P MP game-start packet -- no-op stub; v1.6.1 binary @ 0x157b20 (id=103 / case 0x67)
StartGamePacket::StartGamePacket(int flags)
    : m_Flags(flags), m_GameSeed(0), m_reserved18(0)
{
}
