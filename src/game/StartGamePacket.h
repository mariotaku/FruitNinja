#ifndef FN_GAME_START_GAME_PACKET_H
#define FN_GAME_START_GAME_PACKET_H

// StartGamePacket -- P2P multiplayer session-setup sub-command packet.
// Wire shape (type ID, total size 0x1c) kept as the Bada v1.6.1 port
// established it; the iOS 1.5 handshake this ports onto that wire format
// carries a SUB-COMMAND + one value at +0x10/+0x14 rather than a flags
// bitmask + seed:
//   ASM-spec iOS1.5 StartGamePacket layout @0x000389a0 call sites:
//     +0x10  int32  m_Value  (sub-command payload: seed for cmd2, mode for cmd3)
//     +0x14  int32  m_Cmd    (sub-command: 1=ready, 2=seed+go, 3=mode)
// MP-revival: REPURPOSED from the Bada-only m_Flags/m_GameSeed pair (which
// had no cmd/value split) -- see GlobalP2PMessageHandler's StartGamePacket
// case in P2PMessageHandling.cpp for the cmd 1/2/3 handlers.
// Packet type ID stays 103 (the port's own Bada-side id; loopback is
// self-consistent end-to-end so the exact numeric id doesn't need to match
// iOS's).
// Binary: PacketFactory::Create @ 0x157b20 allocates this for id=103 (case 0x67).

#include "engine/network/NetworkPacket.h"
#include <cstdint>

class StartGamePacket : public Mortar::NetworkPacket {
public:
    // MP-revival: sub-command selector. 1=ready, 2=seed+go, 3=mode.
    // ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0 (StartGamePacket cmd dispatch).
    int32_t m_Cmd;     // +0x14

    // MP-revival: sub-command payload -- online seed for cmd2, game mode for cmd3;
    // unused (0) for cmd1.
    int32_t m_Value;   // +0x18

    // Defunct: P2P MP game-start packet -- no-op stub; v1.6.1 binary @ 0x00158dc0
    StartGamePacket();

    // MP-revival: cmd/value ctor -- replaces the old single-int gameSeed ctor.
    StartGamePacket(int32_t cmd, int32_t value);

    virtual ~StartGamePacket() {}

    // MP-revival: wire (de)serialisation.
    // Payload order: m_Cmd (i32), m_Value (i32).
    virtual void Serialize(Mortar::ByteWriter& w) const;
    virtual void Deserialize(Mortar::ByteReader& r);
};

#if defined(__bada__)
static_assert(sizeof(StartGamePacket) == 0x1c,
    "StartGamePacket must be 0x1c bytes on ARM32/Bada");
#endif

#endif // FN_GAME_START_GAME_PACKET_H
