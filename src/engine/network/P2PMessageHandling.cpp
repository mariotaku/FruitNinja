// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
// All functions in this module are empty {} in the binary (Bada build omits P2P).
// GameModeScreen::P2PConnectCallback @ 0x1810dc calls into this module.

#include "P2PMessageHandling.h"

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
void P2PConnect(bool host) {
    (void)host;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
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
