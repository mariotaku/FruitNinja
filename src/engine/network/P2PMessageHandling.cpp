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
#include "game/GameMode.h"
#include "game/GameTaskState.h"
#include "game/BombHit.h"
#include "engine/network/NetworkManager.h"
#include "entities/Entity.h"
#include "entities/Fruit.h"
#include "engine/math/MathUtil.h"
#include "hud/HUD.h"
#include "hud/ZenVersusControl.h"

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

// DIFFERS: Bada v1.6.1 stripped, revived from iOS 1.6.1 CreateMultiplayerControls @0x0002ac84
// News a ZenVersusControl for the just-started online-versus session and adds
// it to the HUD. Guarded by a static instance pointer so a second
// session-start event (e.g. RetryOnlineMultiplayerGame) doesn't stack a
// duplicate control; ClearMultiplayerControls (below) releases the guard on
// disconnect so a fresh session after a full teardown gets its own instance.
static ZenVersusControl* s_pZenVersusControl = 0;
void CreateMultiplayerControls() {
    if (s_pZenVersusControl != 0) {
        return;
    }
    if (game_work.mHud == 0) {
        return;
    }
    s_pZenVersusControl = new ZenVersusControl();
    game_work.mHud->AddControl(s_pZenVersusControl, false);
}

// Port-only helper (no binary counterpart) -- removes and deletes the
// session's ZenVersusControl, if any, and clears the CreateMultiplayerControls
// guard so the next session-start gets a fresh instance. HUD::RemoveControl
// unlinks the control from the draw/update list but does NOT delete it (see
// HUD.cpp), so the delete here is explicit.
static void ClearMultiplayerControls() {
    if (s_pZenVersusControl != 0) {
        if (game_work.mHud != 0) {
            game_work.mHud->RemoveControl(s_pZenVersusControl);
        }
        delete s_pZenVersusControl;
        s_pZenVersusControl = 0;
    }
}

// MP-revival: msgCode 8 (CONNECTED / session-start) handler.
// ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0 (case 8).
// Resets per-session gameplay state so a freshly-established connection
// starts from a clean slate, same as iOS 1.5's case-8 body.
static void HandleP2PConnected() {
    // ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0 (case 8): clears the
    // fixed MP player-name buffers before the NAMES event (msgCode 9,
    // HandleP2PNames below) fills them in. See GameWork::ResetPlayerNames.
    game_work.ResetPlayerNames();

    InstantLevelDestroy(); // real port function -- see game/BombHit.h

    // ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0 (case 8): sets the
    // session to online-versus mode. The port's existing GAME_MODE mapping
    // (GameModeScreen::CasinoModeCallback, GameMode.h) already uses
    // GAME_MODE_COMBO(1) as the "online MP" gameMode value -- reuse that
    // established binary-faithful mapping rather than inventing a new mode
    // index (the spec's literal iOS mode value 4 does not correspond to any
    // mode this port's GAME_MODE enum defines).
    game_work.gameMode = GAME_MODE_COMBO;

    // TODO: iOS1.5 GlobalP2PMessageHandler @0x000389a0 (case 8) -- iOS clears
    // FruitSaveData's "blueWins"/"redWins" versus-mode win totals here.
    // FruitSaveData.h has no blueWins/redWins fields in this port (no RE'd
    // offset for them); nothing to clear yet.

    WaveManager::GetInstance()->Reset(true);

    if (game_work.mHud) {
        game_work.mHud->SetToMultiplayerState();
    }

    CreateMultiplayerTutorialControl();
    CreateMultiplayerControls();

    // Mark the session as an active online-MP match. m_bMPRetryPending is the
    // port's existing "MP session active" gate (read by TimeControl's
    // suppress check and cleared by QuitToMenu/GameOverScreen -- see
    // GameWork.h +0x174); reuse it rather than inventing a new flag.
    game_work.m_bMPRetryPending = 1;
}

// MP-revival: msgCode 9 (NAMES) handler.
// ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0 (case 9).
static void HandleP2PNames() {
    char buf0[256];
    char buf1[256];
    Mortar::NetworkManager::GetInstance()->GetPlayerName(0, buf0, sizeof buf0);
    Mortar::NetworkManager::GetInstance()->GetPlayerName(1, buf1, sizeof buf1);

    // ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0 (case 9): copies the
    // resolved names into game_work's per-player name buffers (see
    // GameWork::SetPlayerName). iOS additionally uppercases and truncates to
    // 10 chars + "..." if longer; NetworkManager::GetPlayerName is itself a
    // defunct stub returning an empty string here (no GameCenter backend), so
    // that truncation path never observably fires in this build -- omitted
    // pending a real name-resolution backend. Local names are set separately
    // (SetPlayerName) rather than through this network path.
    if (buf0[0] != '\0') game_work.SetPlayerName(0, buf0);
    if (buf1[0] != '\0') game_work.SetPlayerName(1, buf1);
}

// MP-revival: msgCode 7 (DATA) sub-dispatch -- the original single-level
// packet-type switch, unchanged from the pre-two-level-dispatch version.
// ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0 (data cases 100-103).
static void HandleP2PData(Mortar::NetworkPacket* packet) {
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
        // MP-revival: StartGamePacket now carries a SUB-COMMAND (m_Cmd) instead
        // of a flags/seed pair -- see StartGamePacket.h for the cmd/value rework.
        case 103: { // StartGamePacket
            StartGamePacket* p = static_cast<StartGamePacket*>(packet);
            switch (p->m_Cmd) {
                // cmd 1: ready handshake.
                case 1: {
                    if (!game_work.m_bP2PReady) {
                        // We haven't sent ours yet in this exchange -- record
                        // that the PEER is ready and wait for our own ready
                        // send (RetryOnlineMultiplayerGame / session-start
                        // path drives that side).
                        game_work.m_bP2PPeerReady = 1;
                    } else {
                        // We were already waiting on the peer -- this ready
                        // closes the handshake: clear both latches and start
                        // the multiplayer tutorial/HUD state.
                        game_work.m_bP2PReady = 0;
                        game_work.m_bP2PPeerReady = 0;
                        CreateMultiplayerTutorialControl();
                    }
                    break;
                }
                // cmd 2: seed + go. Only the GUEST (LocalPlayerNumber()==2)
                // reseeds -- the host keeps its own seed (see WaveManager.h
                // SetOnlineSeed comment / IMpTransport.h LocalPlayerNumber doc).
                case 2: {
                    if (Mortar::NetworkManager::GetInstance()->GetLocalPlayerNumber() == 2) {
                        WaveManager::GetInstance()->SetOnlineSeed(static_cast<uint32_t>(p->m_Value));
                    }
                    // "GO" flag: match is actually starting now. Set on the
                    // MultiplayerTutorialControl installed at session-start
                    // (HandleP2PConnected/CreateMultiplayerTutorialControl)
                    // if present; also set the WaveManager-visible
                    // m_bP2POpponentReady checkpoint (the flag
                    // WaveManager::Update actually gates the online-MP
                    // wave-tick dt on -- see GameWork.h +0x1A1 comment).
                    if (game_work.m_TutorialControl != 0) {
                        static_cast<MultiplayerTutorialControl*>(game_work.m_TutorialControl)->m_bGo = true;
                    }
                    game_work.m_bP2POpponentReady = 1;
                    break;
                }
                // cmd 3: mode. Stores the peer-broadcast game mode into the
                // session's mode field (mirrors HandleP2PConnected's
                // game_work.gameMode assignment for the session-start path).
                case 3: {
                    game_work.gameMode = static_cast<uint8_t>(p->m_Value);
                    break;
                }
                default:
                    break;
            }
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

// MP-revival: TWO-LEVEL dispatch -- `msg` (transport event vs. in-band data,
// see P2PMessage/MpTransportEvent) is checked FIRST, then (for msg==DATA)
// the packet's own m_PacketType selects the concrete handler. Earlier port
// revisions dispatched on packet->m_PacketType alone because P2PMessage had
// only P2PMSG_NONE; now that NetworkManager::Update's event pump (see
// NetworkManager.cpp) produces real CONNECTED/NAMES/DATA values, `msg` is
// authoritative for the top-level routing.
//
// ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0: iOS 1.5 (the build
// that shipped working MP) APPLIES each received packet to live gameplay
// state instead of merely recording it. The Bada v1.6.1 retail body at
// 0x15761c is an empty stub (P2P runtime compiled out); this keeps the
// Bada wire format (packet type IDs 100-104, packet field layouts) but
// runs the iOS 1.5 algorithm against them.
// DIFFERS: revived -- no binary body, retail stub @0x15761c EMPTY {}
void GlobalP2PMessageHandler(Mortar::P2PMessage msg, Mortar::NetworkPacket* packet) {
    switch (msg) {
        case Mortar::P2PMSG_CONNECTED:
            HandleP2PConnected();
            break;
        case Mortar::P2PMSG_NAMES:
            HandleP2PNames();
            break;
        case Mortar::P2PMSG_DATA:
        case Mortar::P2PMSG_NONE: // back-compat: callers that don't yet tag DATA
        default:
            if (packet != 0) {
                HandleP2PData(packet);
            }
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

// MP-revival: real body -- re-arms the ready handshake for a Retry-after-death
// on an online-MP game. ASM-spec iOS1.5 RetryOnlineMultiplayerGame @0x00035bd4:
//   game_work +0x170 = 1 (MP-active)   -> port: m_bMPRetryPending = 1
//   game_work +0x195 = 0 (our-ready)   -> port: m_bP2PReady = 0
//   game_work +0x196 = 1 (retry armed) -> port: no distinct "retry armed" byte
//     exists in the port's GameWork layout beyond m_bMPRetryPending itself
//     (which this function already sets to 1); folded into that one flag.
//   send StartGamePacket(cmd=1 ready) reliable
//   if peer not yet ready (+0x197==0): arm ready-timeout (+0x19c = 12.0f)
//   else: CreateMultiplayerTutorialControl()
// DIFFERS: revived -- no binary body, retail stub @0x001053e4.
void RetryOnlineMultiplayerGame() {
    game_work.m_bMPRetryPending = 1;
    game_work.m_bP2PReady = 0;

    StartGamePacket ready(1, 0);
    SendP2PPacket(ready, true);

    if (!game_work.m_bP2PPeerReady) {
        game_work.m_P2PReadyTimeout = 12.0f;
    } else {
        CreateMultiplayerTutorialControl();
    }
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
    // MP-revival: session-setup handshake latches (port-only fields, see
    // GameWork.h) -- clear alongside the other MP session flags above.
    game_work.m_bMPRetryPending = 0;
    game_work.m_bP2PPeerReady   = 0;
    game_work.m_P2PReadyTimeout = 0.0f;

    const char* reason;
    switch (code) {
        case 1:  reason = "The other player has left the game."; break;
        case 2:  reason = "The connection to the other player timed out."; break;
        case 6:  reason = "The connection to the other player was lost."; break;
        default: reason = "You have been disconnected."; break;
    }

    ClearMultiplayerControls();

    if (IsOnlineMultiplayer()) {
        CleanupAndReturnToMainMenu();
        Mortar::NetworkManager::GetInstance()->PopupAlert("Disconnected", reason);
    }
}

// MP-revival: allocates a MultiplayerTutorialControl into
// game_work.m_TutorialControl and adds it to the HUD, replacing whatever
// TutorialControl* was already installed (matches the GameInit.cpp /
// GameInitialise.cpp pattern for the plain TutorialControl -- see
// `new TutorialControl()` call sites there -- but installs the online-MP
// subclass instead).
// ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0 / RetryOnlineMultiplayerGame @0x00035bd4.
void CreateMultiplayerTutorialControl() {
    MultiplayerTutorialControl* ctrl = new MultiplayerTutorialControl();
    game_work.m_TutorialControl = ctrl;
    if (game_work.mHud) {
        game_work.mHud->AddControl(ctrl, false);
    }
}
