#include "game/PlayerDisconnectGamePacket.h"
#include <cstring>

// MP-revival: real ctor -- v1.6.1 binary @ 0x1580e8 (id=104, size=0x58)
PlayerDisconnectGamePacket::PlayerDisconnectGamePacket() : m_PlayerIdx(0) {
    memset(m_MessageText, 0, sizeof(m_MessageText));
}

// MP-revival: v1.6.1 SetMessageText @ 0x1580bc
void PlayerDisconnectGamePacket::SetMessageText(const char* msg) {
    strncpy(m_MessageText, msg, sizeof(m_MessageText));
    m_MessageText[sizeof(m_MessageText) - 1] = 0;
}

// MP-revival: real wire serialisation, RE'd field order
void PlayerDisconnectGamePacket::Serialize(Mortar::ByteWriter& w) const {
    NetworkPacket::WriteHeader(w);
    w.Bytes(m_MessageText, sizeof(m_MessageText));
    w.I32(m_PlayerIdx);
}

// MP-revival: real wire deserialisation (inverse of Serialize above)
void PlayerDisconnectGamePacket::Deserialize(Mortar::ByteReader& r) {
    NetworkPacket::ReadHeader(r);
    r.Bytes(m_MessageText, sizeof(m_MessageText));
    m_PlayerIdx = r.I32();
}
