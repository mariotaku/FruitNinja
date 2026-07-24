#ifndef FN_GAME_WAVE_SYNC_PACKET_H
#define FN_GAME_WAVE_SYNC_PACKET_H

// WaveSyncPacket -- online multiplayer wave-sync packet.
// Defunct: online MP -- stub per stub-don't-skip policy.
//
// Binary: 4 ctors at 0x0012dd94 / 0x0012dddc / 0x0012de24 / 0x0012de64.
// Polymorphic: vptr @ +0x00 (via base Mortar::NetworkPacket).
// Packet type ID = 0x66, total packet payload size = 0x28 (40 bytes).
// Mortar::NetworkPacket base occupies +0x00..+0x13 (20 bytes).
// WaveSyncPacket own fields start at +0x14 (20 bytes of own fields).

#include "engine/network/NetworkPacket.h"
#include <cstdint>

class WaveSyncPacket : public Mortar::NetworkPacket {
public:
    // Wire fields in binary serialize order (WaveSyncPacket::Serialize @ 0x00159230,
    // 3-arg ctor @ 0x00159360).
    long     m_WaveIdx;     // +0x14 -- wave index (WriteInt32; ctor param 1)
    long     m_WaveData18;  // +0x18 -- wave payload (WriteInt32; ctor param 2; purpose unknown)
    float    m_Score;       // +0x1c -- score (WriteDec32 float; ctor param 3)
    int      m_reserved20;  // +0x20 -- init 0 (WriteInt32; purpose unknown)
    uint8_t  m_Flag24;      // +0x24 -- init 0 (WriteBool; boolean flag, purpose unknown)
    uint8_t  _pad25[3];

    // Defunct: online multiplayer -- no-op stub; binary nm confirms:
    //   WaveSyncPacket()                   @ 0x00149430
    //   WaveSyncPacket(long, long, float)  @ 0x00149360
    WaveSyncPacket();
    WaveSyncPacket(long waveIdx, long waveData18, float score);

    virtual ~WaveSyncPacket() {}

    // MP-revival: wire (de)serialisation; v1.6.1 WaveSyncPacket::Serialize @ 0x00159230.
    // Payload order: m_WaveIdx (i32), m_WaveData18 (i32), m_Score (f32),
    // m_reserved20 (i32), m_Flag24 (bool as u8).
    virtual void Serialize(Mortar::ByteWriter& w) const;
    virtual void Deserialize(Mortar::ByteReader& r);
};

#if defined(__bada__)
static_assert(sizeof(WaveSyncPacket) == 40,
    "WaveSyncPacket must be 40 bytes on ARM32/Bada");
#endif

#endif // FN_GAME_WAVE_SYNC_PACKET_H
