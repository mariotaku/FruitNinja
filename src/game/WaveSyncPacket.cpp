#include "WaveSyncPacket.h"

// WaveSyncPacket -- Defunct online multiplayer wave-sync packet.
// Binary: 4 ctors at 0x0012dd94 / 0x0012dddc / 0x0012de24 / 0x0012de64.

// Defunct: online multiplayer -- no-op stub; binary @ 0x0012dd94
WaveSyncPacket::WaveSyncPacket()
    : m_WaveIdx(0), m_field18(0), m_Score(0.0f), m_field20(0), m_field24(0)
{
    _pad25[0] = _pad25[1] = _pad25[2] = 0;
}

// Defunct: online multiplayer -- no-op stub; binary @ 0x0012dddc
WaveSyncPacket::WaveSyncPacket(long waveIdx)
    : m_WaveIdx(waveIdx), m_field18(0), m_Score(0.0f), m_field20(0), m_field24(0)
{
    _pad25[0] = _pad25[1] = _pad25[2] = 0;
}

// Defunct: online multiplayer -- no-op stub; binary @ 0x0012de24
WaveSyncPacket::WaveSyncPacket(long waveIdx, float score)
    : m_WaveIdx(waveIdx), m_field18(0), m_Score(score), m_field20(0), m_field24(0)
{
    _pad25[0] = _pad25[1] = _pad25[2] = 0;
}

// Defunct: online multiplayer -- no-op stub; binary @ 0x0012de64
WaveSyncPacket::WaveSyncPacket(long waveIdx, float score, long field18)
    : m_WaveIdx(waveIdx), m_field18(field18), m_Score(score), m_field20(0), m_field24(0)
{
    _pad25[0] = _pad25[1] = _pad25[2] = 0;
}
