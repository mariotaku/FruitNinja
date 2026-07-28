#ifndef FN_ENGINE_NETWORK_P2P_MESSAGE_HANDLING_H
#define FN_ENGINE_NETWORK_P2P_MESSAGE_HANDLING_H

// Defunct: P2P multiplayer -- free-function module, no class.
//
// The whole module stays a no-op stub per the "stub, never skip" policy, but the
// binary is NOT uniformly empty and the markers below say which is which:
//   EMPTY {} in v1.6.1: LaunchP2PMatchMaker @0x00157628, SetupP2PMessageHandling
//     @0x0015762c, GlobalP2PMessageHandler @0x0015761c, SendP2PPacket @0x00157630,
//     DisconnectP2P @0x00157634, IsMultiplayer @0x0011a094,
//     IsOnlineMultiplayer @0x0011a09c.
//   REAL BODIES in v1.6.1 that the port deliberately stubs: IsP2POnline @0x0011f524,
//     IsP2PSupported @0x0011f560, IsP2PConnecting @0x0011a1e0,
//     P2PInitializationCompleteHandler @0x0011c2b4, P2PConnect @0x0011c36c.
// 0x00157640 is _GLOBAL__I_P2PMessageHandling.cpp (the TU static-init ctor, 0x4e0
// bytes) -- it is NOT any of these functions; do not cite it as one.

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

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::P2PConnect @0x0011c36c.
// NOT a stub in the binary: it has a real body. Port no-ops it (defunct P2P).
void P2PConnect(bool host);

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::DisconnectP2P @0x00157634 (EMPTY {})
void DisconnectP2P(bool sendDisconnect);

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::IsP2POnline @0x0011f524.
// NOT a stub in the binary: it forwards to NetworkManager::IsGameCenterSupported /
// IsGameCenterOnline. Port returns false because the whole online stack is defunct.
bool IsP2POnline();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::IsP2PSupported @0x0011f560.
// NOT a stub in the binary: it forwards to NetworkManager::IsGameCenterSupported().
bool IsP2PSupported();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::IsP2PConnecting @0x0011a1e0.
// NOT a stub in the binary: it returns the LIVE global game_work.bP2PConnecting.
// Port hardcodes false because P2P never connects here.
bool IsP2PConnecting();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::SendP2PPacket @0x00157630 (EMPTY {})
void SendP2PPacket(Mortar::NetworkPacket& packet, bool reliable);

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::LaunchP2PMatchMaker @0x00157628 (EMPTY {})
void LaunchP2PMatchMaker();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::GlobalP2PMessageHandler @0x0015761c (EMPTY {})
void GlobalP2PMessageHandler(Mortar::P2PMessage msg, Mortar::NetworkPacket* packet);

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::SetupP2PMessageHandling @0x0015762c (EMPTY {})
void SetupP2PMessageHandling();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1
// ::P2PInitializationCompleteHandler(bool,bool) @0x0011c2b4.
// NOT a stub in the binary: 0xb8 bytes of real body. Port no-ops it because the
// P2P session it drives never exists.
void P2PInitializationCompleteHandler(bool success, bool isHost);

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::IsMultiplayer @0x0011a094 (EMPTY, always false)
bool IsMultiplayer();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::IsOnlineMultiplayer @0x0011a09c (EMPTY, always false)
bool IsOnlineMultiplayer();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::IsSameScreenMultiplayer @0x0011a0a4 (always false)
bool IsSameScreenMultiplayer();

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::RetryOnlineMultiplayerGame @0x00157624
void RetryOnlineMultiplayerGame();

// Defunct: P2P invite accept/reject -- real v1.6.1 impl stripped (no symbol); stub kept for call-graph shape.
void AcceptCallback(int sessionId);

// Defunct: P2P invite accept/reject -- real v1.6.1 impl stripped (no symbol); stub kept for call-graph shape.
void RejectCallback(int sessionId);

#endif // FN_ENGINE_NETWORK_P2P_MESSAGE_HANDLING_H
