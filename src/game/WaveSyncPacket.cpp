#include "WaveSyncPacket.h"

// WaveSyncPacket -- Defunct online multiplayer wave-sync packet.
// Binary: 4 ctors at 0x0012dd94 / 0x0012dddc / 0x0012de24 / 0x0012de64.

// Defunct: online multiplayer -- no-op stub; v1.6.1 WaveSyncPacket::WaveSyncPacket() @0x00159430
// Base ctor stamps m_PacketType=102(0x66)/m_PacketSize=sizeof(WaveSyncPacket),
// matching the binary's ctor chain to NetworkPacket(typeId, byteSize).
WaveSyncPacket::WaveSyncPacket()
    : Mortar::NetworkPacket(102, sizeof(WaveSyncPacket)),
      m_WaveIdx(0), m_WaveData18(0), m_Score(0.0f), m_reserved20(0), m_Flag24(0)
{
    _pad25[0] = _pad25[1] = _pad25[2] = 0;
}

// Defunct: online multiplayer -- no-op stub; v1.6.1 WaveSyncPacket::WaveSyncPacket(long,long,float) @0x00159360
WaveSyncPacket::WaveSyncPacket(long waveIdx, long waveData18, float score)
    : Mortar::NetworkPacket(102, sizeof(WaveSyncPacket)),
      m_WaveIdx(waveIdx), m_WaveData18(waveData18), m_Score(score), m_reserved20(0), m_Flag24(0)
{
    _pad25[0] = _pad25[1] = _pad25[2] = 0;
}
