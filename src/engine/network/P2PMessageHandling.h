#ifndef FN_ENGINE_NETWORK_P2P_MESSAGE_HANDLING_H
#define FN_ENGINE_NETWORK_P2P_MESSAGE_HANDLING_H

// Defunct: P2P multiplayer -- free-function module, no class.
// Binary: P2PMessageHandling.cpp @ 0x157640 area.
// SendP2PPacket @ 0x157630 is EMPTY {} in binary -- all no-ops faithful.
// GlobalP2PMessageHandler @ 0x15761c is EMPTY {} in binary.
// GameModeScreen::P2PConnectCallback @ 0x1810dc calls these.

namespace Mortar { class NetworkPacket; }

// Binary enums for P2P message types (opaque; values not yet RE'd)
namespace Mortar {
enum P2PMessage {
    P2PMSG_NONE = 0
};
} // namespace Mortar

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
void P2PConnect(bool host);

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
void DisconnectP2P(bool sendDisconnect);

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640 (returns false)
bool IsP2POnline();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640 (returns false)
bool IsP2PSupported();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640 (returns false)
bool IsP2PConnecting();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157630 EMPTY {}
void SendP2PPacket(Mortar::NetworkPacket& packet, bool reliable);

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
void LaunchP2PMatchMaker();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x15761c EMPTY {}
void GlobalP2PMessageHandler(Mortar::P2PMessage msg, Mortar::NetworkPacket* packet);

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
void SetupP2PMessageHandling();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
void P2PInitializationCompleteHandler(bool success, bool isHost);

#endif // FN_ENGINE_NETWORK_P2P_MESSAGE_HANDLING_H
