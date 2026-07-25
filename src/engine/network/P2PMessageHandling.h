#ifndef FN_ENGINE_NETWORK_P2P_MESSAGE_HANDLING_H
#define FN_ENGINE_NETWORK_P2P_MESSAGE_HANDLING_H

#include "hud/TutorialControl.h"

// Defunct: P2P multiplayer -- free-function module, no class.
// Binary: P2PMessageHandling.cpp @ 0x157640 area.
// SendP2PPacket @ 0x157630 is EMPTY {} in binary -- all no-ops faithful.
// GlobalP2PMessageHandler @ 0x15761c is EMPTY {} in binary.
// GameModeScreen::P2PConnectCallback @ 0x1810dc calls these.

namespace Mortar { class NetworkPacket; }

// Binary enums for P2P message types.
// MP-revival: P2PMSG_DATA/CONNECTED/NAMES values match the iOS 1.5 msgCode
// constants GlobalP2PMessageHandler switches on @0x000389a0 -- msg 7 carries
// an in-band data packet (dispatch continues on packet->m_PacketType, see
// GlobalP2PMessageHandler's two-level switch), msg 8/9 are transport SESSION
// events (see IMpTransport.h's MpTransportEvent, which mirrors these same
// numeric values -- NetworkManager::Update's event pump translates
// MP_EVT_CONNECTED/MP_EVT_NAMES into these P2PMessage values before calling
// GlobalP2PMessageHandler).
namespace Mortar {
enum P2PMessage {
    P2PMSG_NONE      = 0,
    P2PMSG_DATA      = 7, // in-band NetworkPacket -- dispatch by packet->m_PacketType
    P2PMSG_CONNECTED = 8, // session established -- iOS msgCode 8
    P2PMSG_NAMES     = 9  // peer names available -- iOS msgCode 9
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

// MP-revival: real body -- true in the port build (IMpTransport is wired up),
// false under __bada__ (retail fidelity). See P2PMessageHandling.cpp for the
// full rationale. DIFFERS: revived -- retail stub @0x157640 (always false)
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

// MP-revival: real body -- re-arms the ready handshake for a Retry-after-death
// on an online-MP game: resets the MP-active/ready flags, sends our own
// StartGamePacket(cmd=1 ready) to the peer, then either arms a ready-timeout
// (peer hasn't acked yet) or proceeds immediately (peer already sent theirs).
// ASM-spec iOS1.5 RetryOnlineMultiplayerGame @0x00035bd4.
// DIFFERS: revived -- no binary body, retail stub @0x001053e4.
void RetryOnlineMultiplayerGame();

// Defunct: P2P invite accept/reject -- real v1.6.1 impl stripped (no symbol); stub kept for call-graph shape.
void AcceptCallback(int sessionId);

// Defunct: P2P invite accept/reject -- real v1.6.1 impl stripped (no symbol); stub kept for call-graph shape.
void RejectCallback(int sessionId);

// MP-revival: online-MP variant of TutorialControl. iOS 1.5 allocates this
// into game_work+0x16c (m_TutorialControl) on session start (StartGamePacket
// msgCode 8 / CONNECTED) instead of the plain TutorialControl the SP game
// uses -- adds a single "GO" latch the cmd2 (seed+go) handler sets once the
// host broadcasts the online seed, gating the actual match start distinct
// from the ready handshake (cmd1).
// TODO: iOS1.5 MultiplayerTutorialControl @0x00035bd4 area -- only the GO
// latch (+0xac, the field this port's cmd2 handler needs) is ported; the
// class's own tutorial-overlay visuals/behaviour differences vs. base
// TutorialControl were not RE'd and are not reproduced here (base behaviour
// inherited unchanged).
class MultiplayerTutorialControl : public TutorialControl {
public:
    MultiplayerTutorialControl() : TutorialControl(), m_bGo(false) {}

    // +0xac (relative to TutorialControl's 0xa0-byte binary layout): set once
    // the host's StartGamePacket(cmd=2 seed+go) has been received/sent,
    // i.e. the match is actually starting (distinct from m_bP2PPeerReady,
    // the pre-match ready handshake latch).
    bool m_bGo; // +0xac
};

// MP-revival: real body -- iOS 1.5's transport-level disconnect handler.
// Clears game_work's MP session flags, then (if a game was actually in
// progress online) tears down back to the main menu and pops an alert with a
// reason string keyed by `code`. Called from DisconnectP2P (a transport drop
// or explicit local hangup) rather than from any data packet -- iOS has no
// disconnect *packet*, unlike the Bada port's now-removed data-case 104.
// ASM-spec iOS1.5 HandleDisconnection @0x00039524
void HandleDisconnection(int code);

// MP-revival: allocates a MultiplayerTutorialControl into
// game_work.m_TutorialControl (replacing whatever TutorialControl* is
// already installed, mirroring the base-class pointer slot's binary layout)
// and adds it to the HUD. Shared by the StartGamePacket cmd1 (ready, both
// sides now ready) and RetryOnlineMultiplayerGame (peer already ready) paths.
// ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0 / RetryOnlineMultiplayerGame @0x00035bd4.
void CreateMultiplayerTutorialControl();

// DIFFERS: Bada v1.6.1 stripped, revived from iOS 1.6.1 CreateMultiplayerControls @0x0002ac84
// For an online-versus session, news a ZenVersusControl and HUD::AddControl's
// it -- guarded by a static so repeat session-start events (retry, etc.)
// don't stack duplicate controls. Null-safe: no-ops if game_work.mHud is
// unset. Called from HandleP2PConnected (session-start / msgCode 8 CONNECTED)
// alongside the existing CreateMultiplayerTutorialControl() call.
void CreateMultiplayerControls();

#endif // FN_ENGINE_NETWORK_P2P_MESSAGE_HANDLING_H
