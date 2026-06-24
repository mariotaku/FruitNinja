#include "WaveSyncPacket.h"

// WaveSyncPacket -- Defunct online multiplayer wave-sync packet.
// Binary: 4 ctors at 0x0012dd94 / 0x0012dddc / 0x0012de24 / 0x0012de64.

// Defunct: online multiplayer -- no-op stub; binary nm: @ 0x00149430
WaveSyncPacket::WaveSyncPacket()
    : m_WaveIdx(0), m_WaveData18(0), m_Score(0.0f), m_reserved20(0), m_Flag24(0)
{
    _pad25[0] = _pad25[1] = _pad25[2] = 0;
}

// Defunct: online multiplayer -- no-op stub; binary nm: @ 0x00149360
WaveSyncPacket::WaveSyncPacket(long waveIdx, long waveData18, float score)
    : m_WaveIdx(waveIdx), m_WaveData18(waveData18), m_Score(score), m_reserved20(0), m_Flag24(0)
{
    _pad25[0] = _pad25[1] = _pad25[2] = 0;
}
