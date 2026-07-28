// Defunct: P2P multiplayer -- no-op stub.
// Several of these ARE empty {} in the binary, but five have real bodies that the
// port intentionally stubs (IsP2POnline, IsP2PSupported, IsP2PConnecting,
// P2PInitializationCompleteHandler, P2PConnect). Per-function markers below say
// which; see P2PMessageHandling.h for the full split.

#include "P2PMessageHandling.h"

namespace Mortar {

// Defunct: P2P error -- no-op stub; v1.6.1 Mortar::DefaultP2PErrorHandler
void DefaultP2PErrorHandler(P2PMessage msg) {
    (void)msg;
}

// Defunct: P2P init -- no-op stub; v1.6.1 Mortar::DefaultP2PInitializationHandler
void DefaultP2PInitializationHandler(bool success, bool isHost) {
    (void)success;
    (void)isHost;
}

// Defunct: P2P message -- no-op stub; v1.6.1 Mortar::DefaultP2PMessageHandler
void DefaultP2PMessageHandler(P2PMessage msg, NetworkPacket* packet) {
    (void)msg;
    (void)packet;
}

// Defunct: P2P voice chat -- no-op stub; v1.6.1 Mortar::DefaultP2PVoiceChatOpponentSpeakingCallback
void DefaultP2PVoiceChatOpponentSpeakingCallback(bool isSpeaking) {
    (void)isSpeaking;
}

} // namespace Mortar

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::P2PConnect @0x0011c36c.
// NOT a stub in the binary: it has a real body. Port no-ops it (defunct P2P).
void P2PConnect(bool host) {
    (void)host;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::DisconnectP2P @0x00157634 (EMPTY {})
void DisconnectP2P(bool sendDisconnect) {
    (void)sendDisconnect;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::IsP2POnline @0x0011f524.
// NOT a stub in the binary: it forwards to NetworkManager::IsGameCenterSupported /
// IsGameCenterOnline. Port returns false because the whole online stack is defunct.
bool IsP2POnline() {
    return false;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::IsP2PSupported @0x0011f560.
// NOT a stub in the binary: it forwards to NetworkManager::IsGameCenterSupported().
bool IsP2PSupported() {
    return false;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::IsP2PConnecting @0x0011a1e0.
// NOT a stub in the binary: it returns the LIVE global game_work.bP2PConnecting.
// Port hardcodes false because P2P never connects here.
bool IsP2PConnecting() {
    return false;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::SendP2PPacket @0x00157630 (EMPTY {})
void SendP2PPacket(Mortar::NetworkPacket& packet, bool reliable) {
    (void)packet;
    (void)reliable;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::LaunchP2PMatchMaker @0x00157628 (EMPTY {})
void LaunchP2PMatchMaker() {
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::GlobalP2PMessageHandler @0x0015761c (EMPTY {})
void GlobalP2PMessageHandler(Mortar::P2PMessage msg, Mortar::NetworkPacket* packet) {
    (void)msg;
    (void)packet;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::SetupP2PMessageHandling @0x0015762c (EMPTY {})
void SetupP2PMessageHandling() {
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1
// ::P2PInitializationCompleteHandler(bool,bool) @0x0011c2b4.
// NOT a stub in the binary: 0xb8 bytes of real body. Port no-ops it because the
// P2P session it drives never exists.
void P2PInitializationCompleteHandler(bool success, bool isHost) {
    (void)success;
    (void)isHost;
}

// Defunct: P2P multiplayer; v1.6.1 ::IsMultiplayer @0x0011a094 is literally
// `mov r0,#0; bx lr` -- returning false here is faithful, not a port-side stub.
bool IsMultiplayer() {
    return false;
}

// Defunct: P2P multiplayer; v1.6.1 ::IsOnlineMultiplayer @0x0011a09c is literally
// `mov r0,#0; bx lr` -- returning false here is faithful, not a port-side stub.
bool IsOnlineMultiplayer() {
    return false;
}

// Defunct: P2P multiplayer; v1.6.1 ::IsSameScreenMultiplayer @0x0011a0a4 is
// `IsMultiplayer() && ...`, and IsMultiplayer() is a hard 0, so false is faithful.
bool IsSameScreenMultiplayer() {
    return false;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::RetryOnlineMultiplayerGame @0x00157624
void RetryOnlineMultiplayerGame() {
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::AcceptCallback @0x00184bc0
// Binary: handles incoming P2P game invite acceptance.
void AcceptCallback(int /*sessionId*/) {
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 ::RejectCallback @0x00184bf4
// Binary: handles incoming P2P game invite rejection.
void RejectCallback(int /*sessionId*/) {
}
