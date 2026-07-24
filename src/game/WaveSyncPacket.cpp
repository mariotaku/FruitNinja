#include "WaveSyncPacket.h"

// WaveSyncPacket -- Defunct online multiplayer wave-sync packet.
// Binary: 4 ctors at 0x0012dd94 / 0x0012dddc / 0x0012de24 / 0x0012de64.

// Defunct: online multiplayer -- no-op stub; v1.6.1 WaveSyncPacket::WaveSyncPacket() @0x00159430
// MP-revival: base ctor stamps m_PacketType=102(0x66)/m_PacketSize=sizeof(WaveSyncPacket)
// so PacketFactory::Create's switch and NetworkManager::Update's PeekPacketType route
// this packet correctly (see NetworkPacket.h's 2-arg base-ctor overload).
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

// MP-revival: real wire serialisation; v1.6.1 WaveSyncPacket::Serialize @ 0x00159230
void WaveSyncPacket::Serialize(Mortar::ByteWriter& w) const {
    NetworkPacket::WriteHeader(w);
    w.I32(static_cast<int32_t>(m_WaveIdx));
    w.I32(static_cast<int32_t>(m_WaveData18));
    w.F32(m_Score);
    w.I32(m_reserved20);
    w.U8(m_Flag24);
}

// MP-revival: real wire deserialisation (inverse of Serialize above)
void WaveSyncPacket::Deserialize(Mortar::ByteReader& r) {
    NetworkPacket::ReadHeader(r);
    m_WaveIdx = r.I32();
    m_WaveData18 = r.I32();
    m_Score = r.F32();
    m_reserved20 = r.I32();
    m_Flag24 = r.U8();
}
