// Defunct: P2P multiplayer -- no-op stub.
// Binary: most functions in this module are empty {} (Bada build omits P2P runtime).
// GameModeScreen::P2PConnectCallback @ 0x1810dc calls into this module.

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

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 P2PConnect @0x0011c36c
void P2PConnect(bool host) {
    (void)host;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 DisconnectP2P @0x00157634 (empty body)
void DisconnectP2P(bool sendDisconnect) {
    (void)sendDisconnect;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
bool IsP2POnline() {
    return false;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
bool IsP2PSupported() {
    return false;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
bool IsP2PConnecting() {
    return false;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157630 EMPTY {}
void SendP2PPacket(Mortar::NetworkPacket& packet, bool reliable) {
    (void)packet;
    (void)reliable;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
void LaunchP2PMatchMaker() {
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x15761c EMPTY {}
void GlobalP2PMessageHandler(Mortar::P2PMessage msg, Mortar::NetworkPacket* packet) {
    (void)msg;
    (void)packet;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
void SetupP2PMessageHandling() {
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
void P2PInitializationCompleteHandler(bool success, bool isHost) {
    (void)success;
    (void)isHost;
}

// Defunct: P2P multiplayer; v1.6.1 IsMultiplayer @0x0011a094 is literally
// `mov r0,#0; bx lr` -- returning false here is faithful, not a port-side stub.
bool IsMultiplayer() {
    return false;
}

// Defunct: P2P multiplayer; v1.6.1 IsOnlineMultiplayer @0x0011a09c is literally
// `mov r0,#0; bx lr` -- returning false here is faithful, not a port-side stub.
bool IsOnlineMultiplayer() {
    return false;
}

// Defunct: P2P multiplayer; v1.6.1 IsSameScreenMultiplayer @0x0011a0a4 is
// `IsMultiplayer() && ...`, and IsMultiplayer() is a hard 0, so false is faithful.
bool IsSameScreenMultiplayer() {
    return false;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 RetryOnlineMultiplayerGame @0x00157624
void RetryOnlineMultiplayerGame() {
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 AcceptCallback @0x00184bc0
// Binary: handles incoming P2P game invite acceptance.
void AcceptCallback(int /*sessionId*/) {
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 RejectCallback @0x00184bf4
// Binary: handles incoming P2P game invite rejection.
void RejectCallback(int /*sessionId*/) {
}
