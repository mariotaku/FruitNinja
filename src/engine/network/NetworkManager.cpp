// Analysed: 2026-04-30T00:00
// Mortar::NetworkManager — online services stub. All methods are no-ops.
// See src/engine/network/NetworkManager.h for binary addresses and rationale.

#include "NetworkManager.h"
#include "engine/network/MpTransport.h"
#include "engine/network/IMpTransport.h"
#include "engine/network/ByteBuffer.h"
#include "engine/network/NetworkPacket.h"
#include "engine/network/P2PMessageHandling.h"
#include "game/PacketFactory.h"

// MP-revival: port-only tracking state for the inbound packet dispatch in
// Mortar::GlobalP2PMessageHandler (P2PMessageHandling.cpp). File-static (not
// NetworkManager members) so the binary-faithful 668-byte layout (see
// NetworkManager.h static_assert) is untouched.
namespace {

int  g_OpponentScore = 0;
bool g_OpponentDisconnected = false;

long     g_LastPeerFruitId = 0;
uint16_t g_LastPeerSliceX = 0;
uint16_t g_LastPeerSliceY = 0;
float    g_LastPeerSliceAngle = 0.0f;
long     g_LastPeerSlicePlayerIdx = 0;

// Reads the wire-format NetworkPacket header (see NetworkPacket::WriteHeader)
// out of a raw inbound buffer to learn the packet type before allocating the
// concrete subclass via PacketFactory. Header layout on the wire is 4
// sequential int32s with no vptr: m_PacketSize, m_Reserved08, m_PacketType,
// m_Reserved10 -- so the type is the 3rd u32, byte offset 8.
int PeekPacketType(const uint8_t* buf, int n) {
    if (n < 12) {
        return -1;
    }
    Mortar::ByteReader r(buf, n);
    r.I32(); // m_PacketSize
    r.I32(); // m_Reserved08
    return r.I32(); // m_PacketType
}

} // namespace

namespace Mortar {

// Defunct: online-services -- no-op stub; binary addr unknown.
// Binary queries a GOT flag byte for the active provider (0=OpenFeint, 1=GameCenter).
// Port always returns 0 (OpenFeint path) since both branches are defunct anyway.
int GetSocialNetworkProvider() {
    return 0;
}

// Defunct: social publish -- no-op stub; v1.6.1 Mortar::DefaultPublishTextCallback
void DefaultPublishTextCallback(int result) {
    (void)result;
}

// Defunct: push notifications -- no-op stub; v1.6.1 Mortar::DefaultNotificationCallback
void DefaultNotificationCallback(const char* name, int i1, int i2) {
    (void)name;
    (void)i1;
    (void)i2;
}

// Defunct: leaderboard retrieval -- no-op stub; v1.6.1 Mortar::DefaultRetrieveScoreCallback
void DefaultRetrieveScoreCallback(const char* board, int score, int rank, void* userdata) {
    (void)board;
    (void)score;
    (void)rank;
    (void)userdata;
}

// Defunct: online user data -- no-op stub; v1.6.1 Mortar::DefaultDownloadUserDataCallback
void DefaultDownloadUserDataCallback(const char* board, void* data, int size) {
    (void)board;
    (void)data;
    (void)size;
}

// Defunct: social network registration -- no-op stub; v1.6.1 Mortar::RegisterSocial
void RegisterSocial() {
}

// Defunct: NetworkManager/social popup UI -- no-op stub; v1.6.1 Mortar::DefaultButtonCallback @0x00231084
void DefaultButtonCallback() {
}

// MP-revival: real body -- drains the active transport's EVENT queue first
// (session-setup handshake signals: CONNECTED/NAMES/DISCONNECTED -- see
// IMpTransport::PollEvent), then its inbound DATA queue. Peeks each data
// message's wire header to learn its type, allocates the matching
// NetworkPacket subclass via PacketFactory, deserialises the full payload,
// and dispatches it through Mortar::GlobalP2PMessageHandler tagged
// P2PMSG_DATA. Drains both queues fully each tick (Poll()/PollEvent() return
// 0/MP_EVT_NONE when empty).
// DIFFERS: revived -- no binary body, retail stub @0x2310c8.
void NetworkManager::Update(float /*dt*/) {
    IMpTransport* t = GetMpTransport();
    if (t == 0) {
        return;
    }

    int ev;
    while ((ev = t->PollEvent()) != MP_EVT_NONE) {
        switch (ev) {
            case MP_EVT_CONNECTED:
                // Qualified call: see the data-pump note below for why this
                // must be the free-function GlobalP2PMessageHandler, not
                // NetworkManager's own defunct member of the same name.
                ::GlobalP2PMessageHandler(Mortar::P2PMSG_CONNECTED, 0);
                break;
            case MP_EVT_NAMES:
                ::GlobalP2PMessageHandler(Mortar::P2PMSG_NAMES, 0);
                break;
            case MP_EVT_DISCONNECTED:
                HandleDisconnection(t->DisconnectCode());
                break;
            default:
                break;
        }
    }

    uint8_t buf[512];
    int n;
    while ((n = t->Poll(buf, sizeof buf)) > 0) {
        int typeId = PeekPacketType(buf, n);
        if (typeId < 0) {
            continue;
        }
        // Build a header-only scratch packet so PacketFactory::Create can
        // read m_PacketType off it (Create takes a NetworkPacket*, not a
        // raw buffer).
        NetworkPacket typePeek;
        typePeek.m_PacketType = typeId;
        NetworkPacket* pkt = PacketFactory::Create(&typePeek);
        if (pkt == 0) {
            continue;
        }
        ByteReader r(buf, n);
        pkt->Deserialize(r);
        // Qualified call: NetworkManager has its own (defunct, void*-taking)
        // GlobalP2PMessageHandler member (see NetworkManager.h) that would
        // otherwise shadow this free function during unqualified lookup from
        // inside a NetworkManager member function.
        ::GlobalP2PMessageHandler(Mortar::P2PMSG_DATA, pkt);
        delete pkt;
    }
}

// MP-revival: real body -- reflects the active transport's connected state.
// DIFFERS: revived -- no binary body, retail stub (returned false)
bool NetworkManager::IsOnline() {
    IMpTransport* t = GetMpTransport();
    return t && t->IsConnected();
}

// MP-revival: real body -- forwards to the active transport, or 0 offline.
// DIFFERS: revived -- no binary body, retail stub (returned 0)
int NetworkManager::GetLocalPlayerNumber() {
    IMpTransport* t = GetMpTransport();
    return t ? t->LocalPlayerNumber() : 0;
}

// MP-revival: real body -- 1 when the active transport is connected.
// DIFFERS: revived -- no binary body, retail stub @0x0018e6b8 (returned 0)
int NetworkManager::IsInP2PGame() const {
    IMpTransport* t = GetMpTransport();
    return (t && t->IsConnected()) ? 1 : 0;
}

// MP-revival: no-op-but-real hook -- records that the opponent has left the
// session. DIFFERS: revived -- no binary body, retail stub @0x0018e6b0
void NetworkManager::OnP2PGameOver() {
    g_OpponentDisconnected = true;
}

bool NetworkManager::OnMultiplayerDisconnect() const {
    return g_OpponentDisconnected;
}

void NetworkManager::SetOpponentScore(int points) {
    g_OpponentScore = points;
}

int NetworkManager::GetOpponentScore() const {
    return g_OpponentScore;
}

void NetworkManager::SetLastPeerSlice(long fruitId, uint16_t sliceX, uint16_t sliceY, float sliceAngle, long playerIdx) {
    g_LastPeerFruitId = fruitId;
    g_LastPeerSliceX = sliceX;
    g_LastPeerSliceY = sliceY;
    g_LastPeerSliceAngle = sliceAngle;
    g_LastPeerSlicePlayerIdx = playerIdx;
}

long NetworkManager::GetLastPeerFruitId() const {
    return g_LastPeerFruitId;
}

uint16_t NetworkManager::GetLastPeerSliceX() const {
    return g_LastPeerSliceX;
}

uint16_t NetworkManager::GetLastPeerSliceY() const {
    return g_LastPeerSliceY;
}

float NetworkManager::GetLastPeerSliceAngle() const {
    return g_LastPeerSliceAngle;
}

long NetworkManager::GetLastPeerSlicePlayerIdx() const {
    return g_LastPeerSlicePlayerIdx;
}

} // namespace Mortar

// Defunct: online leaderboard -- no-op stub; v1.6.1 IsProviderOnline @0x0011f534.
bool IsProviderOnline() {
    return false;
}

// Defunct: online leaderboard -- no-op stub; v1.6.1 AreFriendsLoaded @0x0011f4a0
// is `mov r0,#1; bx lr` (constant true). Match it byte-for-byte. Behaviourally
// inert: the sole caller (FruitFactLeaderboard) gates on !IsProviderOnline()
// first (also stubbed false), so the local/offline path is still forced.
bool AreFriendsLoaded() {
    return true;
}

// Defunct: network provider selection -- no-op stub; v1.6.1 AskUserToChoosePreferredNetwork @0x001ca8f0
void AskUserToChoosePreferredNetwork() {
}

// Defunct: network provider selection -- no-op stub; v1.6.1 ChangePreferredNetworkProvider @0x001ca9f8
void ChangePreferredNetworkProvider(long /*v*/) {
}

// Defunct: network provider selection -- no-op stub; v1.6.1 GetPrefNetwork @0x001ca884
long GetPrefNetwork() {
    return 0;
}

// Defunct: network provider selection -- no-op stub; v1.6.1 SetPrefNetwork @0x001ca9e0
void SetPrefNetwork(long /*v*/) {
}

// Defunct: online-services notification -- no-op stub; v1.6.1 CustomNotificationCallback @0x001cf0cc
// Sets m_bUpdatesSuspended=1 on notification-shown (see GameWork.h +0x195).
// Stub does nothing; online notifications are never shown.
void CustomNotificationCallback(const char* /*name*/, int /*i1*/, int /*i2*/) {
}

// Defunct: online leaderboard -- no-op stub; v1.6.1 CurrentUserName @0x001370c8
void CurrentUserName(char* buf, int size, Mortar::NetworkProvider /*provider*/) {
    if (buf && size > 0) {
        buf[0] = '\0';
    }
}

// Defunct: GameCenter callback -- no-op stub; v1.6.1 GPostCallback @0x0010c144
void GPostCallback(int /*result*/) {
}
