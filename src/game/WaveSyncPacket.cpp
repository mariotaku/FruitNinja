#include "WaveSyncPacket.h"
#include <cstring>

// WaveSyncPacket — Defunct online multiplayer wave-sync packet.
// Binary: 4 ctors at 0x0012dd94 / 0x0012dddc / 0x0012de24 / 0x0012de64.
// All ctors are stubs; the packet is never sent/received in the port.

// Defunct: online multiplayer — no-op stub; binary @ 0x0012dd94
WaveSyncPacket::WaveSyncPacket()
    : m_WaveIdx(0), m_field18(0), m_Score(0.0f), m_field20(0), m_field24(0)
{
    memset(m_base, 0, sizeof(m_base));
    memset(_pad25, 0, sizeof(_pad25));
}

// Defunct: online multiplayer — no-op stub; binary @ 0x0012dddc
WaveSyncPacket::WaveSyncPacket(long waveIdx)
    : m_WaveIdx(waveIdx), m_field18(0), m_Score(0.0f), m_field20(0), m_field24(0)
{
    memset(m_base, 0, sizeof(m_base));
    memset(_pad25, 0, sizeof(_pad25));
}

// Defunct: online multiplayer — no-op stub; binary @ 0x0012de24
WaveSyncPacket::WaveSyncPacket(long waveIdx, float score)
    : m_WaveIdx(waveIdx), m_field18(0), m_Score(score), m_field20(0), m_field24(0)
{
    memset(m_base, 0, sizeof(m_base));
    memset(_pad25, 0, sizeof(_pad25));
}

// Defunct: online multiplayer — no-op stub; binary @ 0x0012de64
WaveSyncPacket::WaveSyncPacket(long waveIdx, float score, long field18)
    : m_WaveIdx(waveIdx), m_field18(field18), m_Score(score), m_field20(0), m_field24(0)
{
    memset(m_base, 0, sizeof(m_base));
    memset(_pad25, 0, sizeof(_pad25));
}
