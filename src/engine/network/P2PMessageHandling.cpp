// Defunct: P2P multiplayer -- no-op stub.
// Binary: most functions in this module are empty {} (Bada build omits P2P runtime).
// GameModeScreen::P2PConnectCallback @ 0x1810dc calls into this module.

#include "P2PMessageHandling.h"
#include "engine/network/MpTransport.h"
#include "engine/network/IMpTransport.h"
#include "engine/network/ByteBuffer.h"
#include "engine/network/NetworkPacket.h"
#include "game/PointsPacket.h"
#include "game/FruitSlicedPacket.h"
#include "game/StartGamePacket.h"
#include "game/WaveSyncPacket.h"
#include "game/PlayerDisconnectGamePacket.h"
#include "game/WaveManager.h"
#include "engine/network/NetworkManager.h"

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

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 P2PConnect @0x0010c36c
void P2PConnect(bool host) {
    (void)host;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 DisconnectP2P @0x001053b4
void DisconnectP2P(bool sendDisconnect) {
    (void)sendDisconnect;
}

// MP-revival: real body -- reflects the active transport's connected state.
// DIFFERS: revived -- no binary body, retail stub @0x157640
bool IsP2POnline() {
    Mortar::IMpTransport* t = Mortar::GetMpTransport();
    return t && t->IsConnected();
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
bool IsP2PSupported() {
    return false;
}

// MP-revival: real body -- reflects the active transport's connecting state.
// DIFFERS: revived -- no binary body, retail stub @0x157640
bool IsP2PConnecting() {
    Mortar::IMpTransport* t = Mortar::GetMpTransport();
    return t && t->IsConnecting();
}

// MP-revival: real body -- serialises `packet` and forwards it to the active
// transport. Retail's version of this function is EMPTY {} (P2P runtime was
// compiled out); this replaces the no-op with real wire I/O.
// DIFFERS: revived -- no binary body, retail stub @0x157630
void SendP2PPacket(Mortar::NetworkPacket& packet, bool reliable) {
    Mortar::IMpTransport* t = Mortar::GetMpTransport();
    if (t == 0) {
        return;
    }
    uint8_t buf[512];
    Mortar::ByteWriter w(buf, sizeof buf);
    packet.Serialize(w);
    t->Send(buf, w.Written(), reliable);
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
void LaunchP2PMatchMaker() {
}

// MP-revival: real dispatch -- routes an inbound packet (already
// Deserialize'd by the caller, see NetworkManager::Update's pump) to its
// handler by packet->m_PacketType. `msg` carries no dispatch info in the
// port (P2PMessage has only P2PMSG_NONE -- the binary's real enum values
// were never recovered); the type id on the packet itself is authoritative.
// DIFFERS: revived -- no binary body, retail stub @0x15761c EMPTY {}
void GlobalP2PMessageHandler(Mortar::P2PMessage msg, Mortar::NetworkPacket* packet) {
    (void)msg;
    if (packet == 0) {
        return;
    }
    switch (packet->m_PacketType) {
        case 100: { // PointsPacket
            PointsPacket* p = static_cast<PointsPacket*>(packet);
            Mortar::NetworkManager::GetInstance()->SetOpponentScore(p->m_Points);
            break;
        }
        case 101: { // FruitSlicedPacket
            FruitSlicedPacket* p = static_cast<FruitSlicedPacket*>(packet);
            Mortar::NetworkManager::GetInstance()->SetLastPeerSlice(
                p->m_FruitId, p->m_SliceX, p->m_SliceY, p->m_SliceAngle, p->m_PlayerIdx);
            // TODO: v1.6.1 0x156f80 (FruitSlicedPacket::Serialize call site) --
            // apply peer slice via EntityTracker once the entity-partition
            // port (m_PlayerIdx) lands. Stage 1 only records the last value.
            break;
        }
        case 102: { // WaveSyncPacket
            WaveSyncPacket* p = static_cast<WaveSyncPacket*>(packet);
            WaveManager::GetInstance()->RecievedSync(static_cast<int>(p->m_WaveIdx), p->m_Score);
            break;
        }
        case 103: { // StartGamePacket
            StartGamePacket* p = static_cast<StartGamePacket*>(packet);
            WaveManager::GetInstance()->SetOnlineSeed(static_cast<uint32_t>(p->m_GameSeed));
            break;
        }
        case 104: { // PlayerDisconnectGamePacket
            Mortar::NetworkManager::GetInstance()->OnP2PGameOver();
            break;
        }
        default:
            break;
    }
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
void SetupP2PMessageHandling() {
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 binary @ 0x157640
void P2PInitializationCompleteHandler(bool success, bool isHost) {
    (void)success;
    (void)isHost;
}

// MP-revival: real body -- true whenever an active transport is connected
// (online MP is the only revived mode; same-screen MP stays unsupported).
// DIFFERS: revived -- no binary body, retail stub @0x00105ea0
bool IsMultiplayer() {
    Mortar::IMpTransport* t = Mortar::GetMpTransport();
    return t && t->IsConnected();
}

// MP-revival: real body -- reflects the active transport's connected state.
// DIFFERS: revived -- no binary body, retail stub @0x00105ea4
bool IsOnlineMultiplayer() {
    Mortar::IMpTransport* t = Mortar::GetMpTransport();
    return t && t->IsConnected();
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 IsSameScreenMultiplayer @0x00105ea8
// Same-screen split MP was never a v1.6.1 feature (see project policy on
// m_PlayerIdx being the P2P/EntityTracker partition, not a screen-half split);
// stays false even with the transport revived.
bool IsSameScreenMultiplayer() {
    return false;
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 RetryOnlineMultiplayerGame @0x001053e4
void RetryOnlineMultiplayerGame() {
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 AcceptCallback @0x001053ec
// Binary: handles incoming P2P game invite acceptance.
void AcceptCallback(int /*sessionId*/) {
}

// Defunct: P2P multiplayer -- no-op stub; v1.6.1 RejectCallback @0x001053f4
// Binary: handles incoming P2P game invite rejection.
void RejectCallback(int /*sessionId*/) {
}
