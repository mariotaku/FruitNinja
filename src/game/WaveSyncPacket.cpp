#include "WaveSyncPacket.h"

// WaveSyncPacket -- Defunct online multiplayer wave-sync packet.
// Binary: 4 ctors at 0x0012dd94 / 0x0012dddc / 0x0012de24 / 0x0012de64.

// Defunct: online multiplayer -- no-op stub; binary nm: @ 0x00149430
WaveSyncPacket::WaveSyncPacket()
    : m_WaveIdx(0), m_WaveData14(0), m_Score(0.0f), m_reserved1c(0), m_Flag20(0), m_reserved24(0)
{
    _pad21[0] = _pad21[1] = _pad21[2] = 0;
}

// Defunct: online multiplayer -- no-op stub; binary nm: @ 0x00149360
WaveSyncPacket::WaveSyncPacket(long waveIdx, long waveData14, float score)
    : m_WaveIdx(waveIdx), m_WaveData14(waveData14), m_Score(score), m_reserved1c(0), m_Flag20(0), m_reserved24(0)
{
    _pad21[0] = _pad21[1] = _pad21[2] = 0;
}
