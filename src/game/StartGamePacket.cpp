#include "game/StartGamePacket.h"

// Defunct: P2P MP game-start packet -- no-op stub; binary @ 0x157b20 (id=102)
StartGamePacket::StartGamePacket()
    : m_GameMode(0), m_field18(0), m_field1c(0), m_field20(0), m_field24(0)
{
}

// Defunct: P2P MP game-start packet -- no-op stub
StartGamePacket::StartGamePacket(int gameMode)
    : m_GameMode(gameMode), m_field18(0), m_field1c(0), m_field20(0), m_field24(0)
{
}
