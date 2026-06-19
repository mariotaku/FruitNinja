#include "game/StartGamePacket.h"

// Defunct: P2P MP game-start packet -- no-op stub; binary @ 0x157b20 (id=103 / case 0x67)
StartGamePacket::StartGamePacket()
    : m_GameMode(0), m_field18(0)
{
}

// Defunct: P2P MP game-start packet -- no-op stub; binary @ 0x157b20 (id=103 / case 0x67)
StartGamePacket::StartGamePacket(int gameMode)
    : m_GameMode(gameMode), m_field18(0)
{
}
