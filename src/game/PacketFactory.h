#ifndef FN_GAME_PACKET_FACTORY_H
#define FN_GAME_PACKET_FACTORY_H

// PacketFactory -- allocates P2P packet subclasses by type ID.
// Defunct: P2P packet hierarchy -- stub; binary @ 0x157b20 (PacketFactory).
// Create(NetworkPacket*) switches on the source packet's type id,
// allocates the matching subclass, and returns it (default => nullptr).

namespace Mortar { class NetworkPacket; }

class PacketFactory {
public:
    // Defunct: P2P packet hierarchy -- no-op stub; binary @ 0x157b20
    // Allocates a subclass based on packet->m_typeId. Returns nullptr for unknown ids.
    static Mortar::NetworkPacket* Create(Mortar::NetworkPacket* src);

private:
    PacketFactory();
};

#endif // FN_GAME_PACKET_FACTORY_H
