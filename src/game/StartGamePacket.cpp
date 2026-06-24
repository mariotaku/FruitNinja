#include "game/StartGamePacket.h"

// Defunct: P2P MP game-start packet -- no-op stub; v1.6.1 binary @ 0x00158dc0
StartGamePacket::StartGamePacket()
    : m_Flags(0x18bb8), m_GameSeed(0)
{
}

// Defunct: P2P MP game-start packet -- no-op stub; v1.6.1 binary @ 0x00158dc0
StartGamePacket::StartGamePacket(int gameSeed)
    : m_Flags(0x18bb8), m_GameSeed(gameSeed)
{
}
