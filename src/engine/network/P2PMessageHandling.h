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

// Defunct: P2P default handler callbacks -- no-op stubs.
// Installed by NetworkManager as the default handlers before game code overrides them.

// Defunct: P2P error -- no-op stub; v1.6.1 Mortar::DefaultP2PErrorHandler
void DefaultP2PErrorHandler(P2PMessage msg);

// Defunct: P2P init -- no-op stub; v1.6.1 Mortar::DefaultP2PInitializationHandler
void DefaultP2PInitializationHandler(bool success, bool isHost);

// Defunct: P2P message -- no-op stub; v1.6.1 Mortar::DefaultP2PMessageHandler
void DefaultP2PMessageHandler(P2PMessage msg, NetworkPacket* packet);

// Defunct: P2P voice chat -- no-op stub; v1.6.1 Mortar::DefaultP2PVoiceChatOpponentSpeakingCallback
void DefaultP2PVoiceChatOpponentSpeakingCallback(bool isSpeaking);

} // namespace Mortar

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 P2PConnect @0x0010c36c
void P2PConnect(bool host);

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 DisconnectP2P @0x0010a0e0
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

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 IsMultiplayer @0x0011a094 (always false)
bool IsMultiplayer();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 IsOnlineMultiplayer @0x0011a09c (always false)
bool IsOnlineMultiplayer();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 IsSameScreenMultiplayer @0x0011a0a4 (always false)
bool IsSameScreenMultiplayer();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 RetryOnlineMultiplayerGame @0x00157624
void RetryOnlineMultiplayerGame();

// Defunct: P2P invite accept/reject -- real v1.6.1 impl stripped (no symbol); stub kept for call-graph shape.
void AcceptCallback(int sessionId);

// Defunct: P2P invite accept/reject -- real v1.6.1 impl stripped (no symbol); stub kept for call-graph shape.
void RejectCallback(int sessionId);

// MP-revival: real body -- iOS 1.5's transport-level disconnect handler.
// Clears game_work's MP session flags, then (if a game was actually in
// progress online) tears down back to the main menu and pops an alert with a
// reason string keyed by `code`. Called from DisconnectP2P (a transport drop
// or explicit local hangup) rather than from any data packet -- iOS has no
// disconnect *packet*, unlike the Bada port's now-removed data-case 104.
// ASM-spec iOS1.5 HandleDisconnection @0x00039524
void HandleDisconnection(int code);

#endif // FN_ENGINE_NETWORK_P2P_MESSAGE_HANDLING_H
