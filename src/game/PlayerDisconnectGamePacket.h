#ifndef FN_GAME_PLAYER_DISCONNECT_GAME_PACKET_H
#define FN_GAME_PLAYER_DISCONNECT_GAME_PACKET_H

// PlayerDisconnectGamePacket -- P2P multiplayer player-disconnect broadcast packet.
// MP-revival: real fields, wired for the revived transport (see NetworkPacket.h).
//
// Packet type ID = 104, total size = 0x58 (88 bytes).
// Mortar::NetworkPacket base occupies +0x00..+0x13 (20 bytes).
// PlayerDisconnectGamePacket own fields start at +0x14 (68 bytes of payload).
// Binary: PacketFactory::Create @ 0x157b20 allocates this for id=104.
//
// RE v1.6.1 ctor @0x001580e8: NetworkPacket(type=0x68, size=0x58); then
// SetMessageText @0x001580bc does strncpy(payload+0x10, msg, 0x40) followed by
// payload[0x4f]=0, and stores the ctor's int param at payload+0x50.
//
// Offset caveat: SetMessageText's payload+0x10/+0x4f/+0x50 offsets are relative
// to a pointer that is NOT the struct base -- taking them at face value against
// a 68-byte payload with a 16-byte lead-in overruns to 84 bytes (struct size
// 0x68/104), contradicting the ctor's own NetworkPacket(_, 0x58) call and this
// class's long-standing sizeof==0x58 static_assert. The size-consistent fit
// (confirmed authoritative: ctor passes 0x58, static_assert already green) is
// message immediately at payload+0x00 (struct+0x14), 64 bytes, then the int
// at payload+0x40 (struct+0x54), 4 bytes = 68 bytes exactly.
#include "engine/network/NetworkPacket.h"
#include <cstdint>

class PlayerDisconnectGamePacket : public Mortar::NetworkPacket {
public:
    char m_MessageText[64];  // +0x14..+0x53 -- disconnect reason, NUL-terminated
    int32_t m_PlayerIdx;     // +0x54..+0x57 -- disconnecting player index

    // MP-revival: real ctor -- v1.6.1 binary @ 0x1580e8 (id=104, size=0x58)
    PlayerDisconnectGamePacket();

    virtual ~PlayerDisconnectGamePacket() {}

    // MP-revival: v1.6.1 SetMessageText @ 0x1580bc -- strncpy into m_MessageText
    // (63 chars max copied via strncpy(dst,msg,0x40), then dst[63] forced to 0).
    void SetMessageText(const char* msg);

    // MP-revival: wire (de)serialisation, in RE'd field order.
    virtual void Serialize(Mortar::ByteWriter& w) const;
    virtual void Deserialize(Mortar::ByteReader& r);
};

#if defined(__bada__)
static_assert(sizeof(PlayerDisconnectGamePacket) == 0x58,
    "PlayerDisconnectGamePacket must be 0x58 bytes on ARM32/Bada");
#endif

#endif // FN_GAME_PLAYER_DISCONNECT_GAME_PACKET_H
