#ifndef FN_GAME_WAVE_SYNC_PACKET_H
#define FN_GAME_WAVE_SYNC_PACKET_H

// WaveSyncPacket — online multiplayer wave-sync packet.
// Defunct: online MP — stub per project policy (stub-don't-skip).
//
// Binary: 4 ctors at 0x0012dd94 / 0x0012dddc / 0x0012de24 / 0x0012de64.
// Packet type ID = 0x66, size = 0x28 (40 bytes total with NetworkPacket base 0x14).
//
// NetworkPacket base (0x14 bytes) is not ported as a full class since the
// network subsystem is defunct; the fields are inlined here to preserve the
// binary struct layout for call-graph purposes.

#include <cstdint>

class WaveSyncPacket {
public:
    // NetworkPacket base fields (+0x00..+0x13, inline — base class not ported)
    // +0x00: vtable pointer (4 bytes in ARM32)
    // +0x04..+0x13: NetworkPacket ID / size / reserved fields
    uint8_t  m_base[0x14];         // +0x00..+0x13 — NetworkPacket base (ID=0x66, size=0x28)

    // WaveSyncPacket own fields
    long     m_WaveIdx;            // +0x14
    long     m_field18;            // +0x18
    float    m_Score;              // +0x1c
    int      m_field20;            // +0x20 — initialised to 0
    uint8_t  m_field24;            // +0x24 — initialised to 0
    uint8_t  _pad25[3];

    // Defunct: online multiplayer — no-op stub; binary @ 0x0012dd94
    WaveSyncPacket();

    // Defunct: online multiplayer — no-op stub; binary @ 0x0012dddc
    WaveSyncPacket(long waveIdx);

    // Defunct: online multiplayer — no-op stub; binary @ 0x0012de24
    WaveSyncPacket(long waveIdx, float score);

    // Defunct: online multiplayer — no-op stub; binary @ 0x0012de64
    WaveSyncPacket(long waveIdx, float score, long field18);
};

#endif // FN_GAME_WAVE_SYNC_PACKET_H
