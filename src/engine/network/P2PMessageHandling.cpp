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
#include "game/GameOver.h"
#include "game/GameWork.h"
#include "game/GameTaskState.h"
#include "engine/network/NetworkManager.h"
#include "entities/Entity.h"
#include "entities/Fruit.h"
#include "engine/math/MathUtil.h"

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

// MP-revival: real body -- transport-level disconnect. iOS 1.5 has no
// disconnect *packet*; the peer connection drop itself is the event, which
// HandleDisconnection reacts to (clears MP flags, alerts, returns to menu).
// `sendDisconnect` is kept for binary call-shape parity but unused: the
// revived transport has no reliable "notify peer" send-then-drop primitive.
// DIFFERS: revived -- no binary body, retail stub @0x001053b4
void DisconnectP2P(bool sendDisconnect) {
    (void)sendDisconnect;
    Mortar::IMpTransport* t = Mortar::GetMpTransport();
    if (t) {
        t->Disconnect();
    }
    HandleDisconnection(1); // code 1 = local/peer quit
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
//
// ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0: iOS 1.5 (the build
// that shipped working MP) APPLIES each received packet to live gameplay
// state instead of merely recording it. The Bada v1.6.1 retail body at
// 0x15761c is an empty stub (P2P runtime compiled out); this keeps the
// Bada wire format (packet type IDs 100-104, packet field layouts) but
// runs the iOS 1.5 algorithm against them.
// DIFFERS: revived -- no binary body, retail stub @0x15761c EMPTY {}
void GlobalP2PMessageHandler(Mortar::P2PMessage msg, Mortar::NetworkPacket* packet) {
    (void)msg;
    if (packet == 0) {
        return;
    }
    switch (packet->m_PacketType) {
        // ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0 (data case 100: PointsPacket)
        case 100: { // PointsPacket
            PointsPacket* p = static_cast<PointsPacket*>(packet);
            Mortar::NetworkManager::GetInstance()->SetOpponentScore(p->m_Points);
            // playerIdx=2: peer-credit partition (see AddToCurrentScore.h);
            // trackAll=false (peer's own save-file already tracked this),
            // p2pBroadcast=false (do NOT re-broadcast a packet we just received).
            AddToCurrentScore(p->m_Points, 2, false, false);
            // TODO: iOS1.5 GlobalP2PMessageHandler @0x000389a0 -- iOS decodes a
            // combo position out of the PointsPacket's trailing fields (>-500
            // sentinel) and pops a MissControl::MakeCombo at that pos. The
            // Bada wire layout's m_reserved18/1c/1c/20 purpose was never
            // recovered (PointsPacket.h), so the position/comboCount/entityType
            // triple can't be reconstructed from this build's packet alone.
            // Skipping the popup; score credit above is unaffected.
            break;
        }
        // ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0 (data case 101: FruitSlicedPacket)
        case 101: { // FruitSlicedPacket
            FruitSlicedPacket* p = static_cast<FruitSlicedPacket*>(packet);
            Mortar::NetworkManager::GetInstance()->SetLastPeerSlice(
                p->m_FruitId, p->m_SliceX, p->m_SliceY, p->m_SliceAngle, p->m_PlayerIdx);

            // TODO: iOS1.5 GlobalP2PMessageHandler @0x000389a0 -- iOS guards
            // stale-wave slices by comparing the packet's wave index against
            // the local wave. FruitSlicedPacket carries no wave index on the
            // Bada wire format (FruitSlicedPacket.h fields are FruitId/X/Y/
            // Angle/PlayerIdx only), so there is nothing to compare against
            // here; applying unconditionally for now.
            Mortar::Entity* e = ET_GetEntity(0, (uint16_t)p->m_FruitId);
            if (e != 0 && e->entityType == 0 /* Fruit */) {
                Fruit* fruit = static_cast<Fruit*>(e);
                if (!fruit->Sliced()) {
                    // Convert the packet's float slice angle to the brad16
                    // convention Fruit::CollisionResponse itself derives a
                    // slice direction from (see Fruit::GetSliceDir /
                    // CollisionResponse's atan2f(bladeVel.x,bladeVel.y) conversion):
                    // dir = (SinIdx(a), CosIdx(a), 0), a in [0,65536) = [0,2*pi).
                    uint16_t a = (uint16_t)(int)(p->m_SliceAngle * (65536.0f / 6.2831853f));
                    _Vector3<float> dir(SinIdx(a), CosIdx(a), 0.0f);
                    // Magnitude: no wire field carries the peer's blade speed
                    // beyond the angle: use CollisionResponse's own normal-hit
                    // floor (SLICE_CLAMP_MIN_NRM==4.0f pre-scale) so the applied
                    // slice reads as an ordinary (non-critical) hit; ClampMin/Max
                    // inside CollisionResponse will still enforce its own bounds.
                    dir = dir * 4.0f;
                    // CollisionResponse's own scoring path (Fruit.cpp @1409)
                    // calls AddToCurrentScore(score, (int)m_PlayerIdx, true, false)
                    // using the FRUIT's OWN m_PlayerIdx, not a hardcoded peer
                    // partition. TODO: iOS1.5 GlobalP2PMessageHandler
                    // @0x000389a0 calls AddToCurrentScore(peerScore, 2, false,
                    // false) explicitly after applying the slice -- but the
                    // Bada FruitSlicedPacket wire format carries no score field
                    // (FruitSlicedPacket.h: FruitId/X/Y/Angle/PlayerIdx only),
                    // so there is no peerScore value to credit here without
                    // fabricating one. Relying on CollisionResponse's internal
                    // scoring for now (correct only if this fruit's
                    // m_PlayerIdx was already set to the peer partition via
                    // SetForPlayer, e.g. by SpawnFruit at spawn time).
                    fruit->CollisionResponse(0, 0, 0, &dir);
                }
            }
            break;
        }
        // ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0 (data case 102: WaveSyncPacket)
        case 102: { // WaveSyncPacket
            WaveSyncPacket* p = static_cast<WaveSyncPacket*>(packet);
            WaveManager::GetInstance()->RecievedSync(static_cast<int>(p->m_WaveIdx), p->m_Score);
            break;
        }
        // ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0 (data case 103: StartGamePacket)
        case 103: { // StartGamePacket
            StartGamePacket* p = static_cast<StartGamePacket*>(packet);
            WaveManager::GetInstance()->SetOnlineSeed(static_cast<uint32_t>(p->m_GameSeed));
            // Mark the session started: this is the flag WaveManager::Update
            // actually gates the online-MP wave-tick dt on (see GameWork.h
            // +0x1A1 comment) -- +0x199 (m_bP2PReady) is a separate checkpoint.
            game_work.m_bP2POpponentReady = 1;
            break;
        }
        // 104 PlayerDisconnectGamePacket: REMOVED from the data dispatch.
        // iOS 1.5 has no disconnect *packet* -- a peer disconnect is a
        // transport-level event, handled by HandleDisconnection (called from
        // DisconnectP2P), not a data case here. PlayerDisconnectGamePacket
        // stays a Bada-relic class (stub-don't-skip) but is no longer routed.
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

// MP-revival: real body -- iOS 1.5's transport-drop handler. Clears every
// game_work MP session flag, then (only if a game was actually online) tears
// down back to the main menu and pops an alert with a reason string keyed by
// `code`. Localized-string lookup is out of scope (see call spec); plain
// ASCII literals stand in for the iOS string-table lookups.
// ASM-spec iOS1.5 HandleDisconnection @0x00039524
void HandleDisconnection(int code) {
    game_work.m_bP2PReady           = 0;
    game_work.m_bP2PConnecting      = 0;
    game_work.m_bP2POpponentReady   = 0;
    game_work.m_bGameCenterConnecting = 0;
    game_work.m_reserved1a2 = 0;
    game_work.m_reserved1a3 = 0;
    game_work.m_reserved1a4 = 0;
    game_work.m_reserved1a5 = 0;
    game_work.m_reserved1a6 = 0;

    const char* reason;
    switch (code) {
        case 1:  reason = "The other player has left the game."; break;
        case 2:  reason = "The connection to the other player timed out."; break;
        case 6:  reason = "The connection to the other player was lost."; break;
        default: reason = "You have been disconnected."; break;
    }

    if (IsOnlineMultiplayer()) {
        CleanupAndReturnToMainMenu();
        Mortar::NetworkManager::GetInstance()->PopupAlert("Disconnected", reason);
    }
}
