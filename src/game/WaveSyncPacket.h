#ifndef FN_GAME_WAVE_SYNC_PACKET_H
#define FN_GAME_WAVE_SYNC_PACKET_H

// WaveSyncPacket -- online multiplayer wave-sync packet.
// Defunct: online MP -- stub per stub-don't-skip policy.
//
// Binary: 4 ctors at 0x0012dd94 / 0x0012dddc / 0x0012de24 / 0x0012de64.
// Polymorphic: vptr @ +0x00 (via base Mortar::NetworkPacket).
// Packet type ID = 0x66, total packet payload size = 0x28 (40 bytes).
// Mortar::NetworkPacket base occupies +0x00..+0x0f (16 bytes).
// WaveSyncPacket own fields start at +0x10 (24 bytes of own fields).

#include "engine/network/NetworkPacket.h"
#include <cstdint>

class WaveSyncPacket : public Mortar::NetworkPacket {
public:
    long     m_WaveIdx;   // +0x10
    long     m_field14;   // +0x14
    float    m_Score;     // +0x18
    int      m_field1c;   // +0x1c -- initialised to 0
    uint8_t  m_field20;   // +0x20 -- initialised to 0
    uint8_t  _pad21[3];
    int      m_field24;   // +0x24 -- initialised to 0

    // Defunct: online multiplayer -- no-op stub; binary nm confirms:
    //   WaveSyncPacket()                   @ 0x00149430
    //   WaveSyncPacket(long, long, float)  @ 0x00149360
    WaveSyncPacket();
    WaveSyncPacket(long waveIdx, long field14, float score);

    virtual ~WaveSyncPacket() {}
};

#if defined(__bada__)
static_assert(sizeof(WaveSyncPacket) == 40,
    "WaveSyncPacket must be 40 bytes on ARM32/Bada");
#endif

#endif // FN_GAME_WAVE_SYNC_PACKET_H
