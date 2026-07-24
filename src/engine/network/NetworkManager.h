#ifndef FN_ENGINE_NETWORK_NETWORK_MANAGER_H
#define FN_ENGINE_NETWORK_NETWORK_MANAGER_H

// Defunct: NetworkManager (GameSpy/GameCenter/OpenFeint/P2P/news) -- no-op stub.
// Binary: ctor @ 0x231c40 (v1.6.1), GetInstance @ 0x231e7c.
// All methods are no-ops returning safe defaults.
// Size: 668 bytes. Ctor inits fields up to 0x29B: 9 Delegates, 1 std::map,
//       3 BUTTON_INFO sub-structs, flag fields.
//
// Binary addresses (v1.0 build numbers retained for symbol-diff; v1.6.1 in comments):
//   ctor (real)    0x0018e05c
//   ctor (alias)   0x0018e25c
//   ctor thunk     0x00100518
//   dtor (regular) 0x0018da94
//   dtor (deleting)0x0018dba4
//   GetInstance    0x0018e210

#include <cstdint>

namespace Mortar {

class NetworkPacket;
class OpenFeintNewsRenderer;

// Network provider selection enum (GameSpy = 0, GameCenter = 1)
// Defunct: online-services
enum NetworkProvider {
    NETWORK_PROVIDER_GAMECENTER = 0,
    NETWORK_PROVIDER_OPENFEINT  = 1
};

// Status message IDs (opaque; used as keys in internal std::map)
// Defunct: online-services
enum NetworkManagerStatusMessageID {
    NM_STATUS_MSG_DEFAULT = 0
};

class NetworkManager {
public:
    static NetworkManager* GetInstance() {
        static NetworkManager s_instance;
        return &s_instance;
    }

    // Polymorphic root: vptr @ +0x00; binary vtable @ 0x001eb210.
    virtual ~NetworkManager() {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x231c40 area
    void Initialise(int /*flags*/) {}

    // MP-revival: real body -- pumps the active transport's inbound queue,
    // dispatching each received packet through Mortar::GlobalP2PMessageHandler
    // (P2PMessageHandling.cpp).
    // DIFFERS: revived -- no binary body, retail stub @0x2310c8 (nearly idle)
    void Update(float dt);

    // Defunct: NetworkManager -- no-op stub
    void Draw(float /*dt*/) {}

    // Defunct: NetworkManager -- no-op stub
    void Destroy() {}

    // MP-revival: real body -- reflects the active transport's connected state.
    // DIFFERS: revived -- no binary body, retail stub (returned false)
    bool IsOnline();

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool HasCredentials() { return false; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d6cc (returns 1)
    bool UserHasEnabledNetwork() { return true; }

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool HasFacebookCredential() { return false; }

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool HasTwitterCredential() { return false; }

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool HasUserAllowedNotification() { return false; }

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool HasRememberedNotificationChoice() { return false; }

    // Defunct: NetworkManager -- no-op stub
    void SendPacket(NetworkPacket* /*packet*/, bool /*reliable*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d9d8
    void InitializeP2P(void* /*cb1*/, void* /*cb2*/, void* /*cb3*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d6f4
    bool DisconnectP2P(bool /*b*/) { return false; }

    // MP-revival: real body -- forwards to the active transport, or 0 offline.
    // DIFFERS: revived -- no binary body, retail stub (returned 0)
    int GetLocalPlayerNumber();

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d924
    void GetPlayerName(int /*idx*/, char* out, int /*cap*/) { if (out) out[0] = 0; }

    // MP-revival: real body -- 1 when the active transport is connected.
    // DIFFERS: revived -- no binary body, retail stub @0x0018e6b8 (returned 0)
    int IsInP2PGame() const;

    // MP-revival: no-op-but-real hook -- records that the opponent has left
    // the session. Called by GlobalP2PMessageHandler on PlayerDisconnectGamePacket.
    // DIFFERS: revived -- no binary body, retail stub @0x0018e6b0
    void OnP2PGameOver();

    // MP-revival: sets when GlobalP2PMessageHandler observes the peer has
    // disconnected. Port-only accessor, no binary counterpart.
    bool OnMultiplayerDisconnect() const;

    // MP-revival: port-only accessors backing the inbound packet dispatch in
    // Mortar::GlobalP2PMessageHandler (P2PMessageHandling.cpp). Storage is
    // file-static in NetworkManager.cpp (not a member) to avoid perturbing
    // the binary-faithful 668-byte layout.
    void SetOpponentScore(int points);
    int GetOpponentScore() const;

    // Last-received FruitSlicedPacket fields (Stage 1: recorded only, not yet
    // applied to gameplay -- see GlobalP2PMessageHandler's TODO in
    // P2PMessageHandling.cpp for the follow-up EntityTracker apply).
    void SetLastPeerSlice(long fruitId, uint16_t sliceX, uint16_t sliceY, float sliceAngle, long playerIdx);
    long GetLastPeerFruitId() const;
    uint16_t GetLastPeerSliceX() const;
    uint16_t GetLastPeerSliceY() const;
    float GetLastPeerSliceAngle() const;
    long GetLastPeerSlicePlayerIdx() const;

    // Defunct: NetworkManager -- no-op stub
    void StartHostingMultiplayerGame() {}

    // Defunct: NetworkManager -- no-op stub
    void StopHostingMultiplayerGame() {}

    // Defunct: NetworkManager -- no-op stub
    void StartMultiplayerGameSession() {}

    // Defunct: NetworkManager -- no-op stub
    void AcceptMultiplayerHostsGame() {}

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool HasNetworkPeersConnected() { return false; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018e698
    int IsAnyPeerReadyForMultiplayer() { return 0; }

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool IsHostWaitingOnUsForMultiplayer() { return false; }

    // Defunct: NetworkManager -- no-op stub
    void SetNetworkPlayAvailability(bool /*avail*/) {}

    // Defunct: NetworkManager -- no-op stub
    void RevokePendingMultiplayerGameInvites(long /*val*/) {}

    // Defunct: NetworkManager -- no-op stub
    void UnlockAchievement(const char* /*name*/) {}

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool IsAchievementUnlocked(const char* /*name*/) { return false; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d698 (returns 0)
    int SetLeaderboardScore(const char* /*board*/, long long /*score*/, void* /*userdata*/, int /*flags*/) {
        return 0;
    }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d7e4
    int RetrieveLeaderboardScore(const char* /*board*/, int /*flags*/, void* /*cb*/) { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d69c
    int RetrieveLeaderboardScoreNextPage() { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d6a0
    int RetrieveLeaderboardScorePreviousPage() { return 0; }

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool CanPageUpHighscores() { return false; }

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool CanPageDownHighscores() { return false; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d6ac
    int DownloadUserDataFromLeaderboard(const char* /*board*/, bool /*friends*/, bool /*local*/, void* /*cb*/) { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d6fc
    bool IsGameCenterOnline() { return false; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d704
    int IsGameCenterSupported() { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d700
    int IsGameCenterAttemptingToConnect() { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d708
    int IsGameCenterInterfaceDisplayed() { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d70c (passthrough)
    bool SetGameCenterShouldLoadOnStartup(bool b) { return b; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d6e4
    NetworkManager* ConnectGameCenter() { return this; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d6e8 (returns 1)
    bool AreGameCenterConnectionAttemptsAllowed(bool /*b*/) { return true; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d668
    void LaunchDashboard(int /*id*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d66c
    void LaunchDashboardWithLeaderboard(const char* /*board*/) {}

    // Defunct: NetworkManager::OpenMatchmaker -- no-op stub; called from
    // v1.6.1 MainScreen::Update @0x001975f4 as OpenMatchmaker(0,-1,2,2).
    void OpenMatchmaker(int /*a*/, int /*b*/, int /*c*/, int /*d*/) {}

    // Defunct: NetworkManager -- no-op stub
    void PublishText(const char* /*network*/, const char* /*msg*/, const char* /*url*/) {}

    // Defunct: NetworkManager -- no-op stub
    void PublishTextWithCallback(const char* /*network*/, const char* /*msg*/, const char* /*url*/, void* /*cb*/) {}

    // Defunct: NetworkManager -- no-op stub
    void SetPublishTextCallback(void* /*cb*/) {}

    // Defunct: NetworkManager -- no-op stub
    void InvalidatePublishTextCallback() {}

    // Defunct: NetworkManager -- no-op stub
    void StartVoiceChatSession() {}

    // Defunct: NetworkManager -- no-op stub
    void StopVoiceChatSession() {}

    // Defunct: NetworkManager -- no-op stub
    void StartVoiceChatSpeaking() {}

    // Defunct: NetworkManager -- no-op stub
    void StopVoiceChatSpeaking() {}

    // Defunct: NetworkManager -- no-op stub
    void SetVoiceChatMuted(bool /*muted*/) {}

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool HasVoiceChatParticipants() { return false; }

    // Defunct: NetworkManager -- no-op stub (returns 0)
    int GetVoiceChatConnectionError() { return 0; }

    // Defunct: NetworkManager -- no-op stub
    void PopupAlert(const char* /*title*/, ...) {}

    // Defunct: NetworkManager -- no-op stub (called from ctor body)
    void RegisterPopupAlertButton(void* /*info*/) {}

    // Defunct: NetworkManager -- no-op stub (called from ctor body)
    void DeregisterPopupAlertButton(void* /*info*/) {}

    // Defunct: NetworkManager -- no-op stub (called from ctor body)
    void DeregisterAllPopupAlertButtons() {}

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool IsPopupAlertDisplayed() const { return false; }

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool IsShowingModalDialog() { return false; }

    // Defunct: NetworkManager -- no-op stub
    void NotifyModalTouchDown(unsigned int /*touchId*/, float /*x*/, float /*y*/) {}

    // Defunct: NetworkManager -- no-op stub
    void NotifyModalTouchEnded(unsigned int /*touchId*/, float /*x*/, float /*y*/) {}

    // Defunct: NetworkManager -- no-op stub
    void EnableNotifications() {}

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool NotificationsAllowed() { return false; }

    // Defunct: NetworkManager -- no-op stub
    void DisallowNotifications() {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d6d8
    void StartNewsDownload() {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d6dc (returns 1)
    bool HasNewsBeenDownloaded() { return true; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d6d4
    int HasUnreadNews() { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d9bc
    // Returns int (0 = no news being displayed); MainScreen::Update case 0xb
    // (@0x001975c0) breaks out of the state while this returns nonzero.
    int UpdateNews(float /*dt*/) { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018dd2c
    void DrawNews() {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d968
    void StartNewsDisplay(void* /*tex*/, void* /*font*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018dcb4
    void CancelNewsDisplay() {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d948
    int GetCurrentNews(char* out, int /*cap*/) { if (out) out[0] = 0; return 0; }

    // Defunct: NetworkManager -- no-op stub (returns nullptr)
    OpenFeintNewsRenderer* GetNewsRenderer() { return 0; }

    // Defunct: NetworkManager -- no-op stub (returns nullptr)
    void* GetNewsRenderInfo() { return 0; }

    // Defunct: NetworkManager -- no-op stub
    void OpenUrl(const char* /*url*/) {}

    // Defunct: NetworkManager -- no-op stub
    void ForceModalDialogsClosed() {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d62c
    void SetPreferredNetworkProvider(int /*provider*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d634
    int GetPreferredNetworkProvider() const { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x00169354
    void ChangePreferredNetworkProvider(long /*v*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d6b0 (returns 0)
    int IsProviderOnline() { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d6b4 (returns 0)
    int IsP2POnline() { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d6f8 (returns 1)
    bool HasFriendsLoaded() { return true; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x00169280
    void GlobalP2PMessageHandler(void* /*msg*/, void* /*packet*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0016b444
    void GlobalP2PErrorHandler(void* /*err*/) {}

    // Defunct: NetworkManager -- no-op stub (called from ctor body)
    void SetStatusMessageTextDefaults() {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018e800
    void SetP2PMessageHandlerCallback(void* /*cb*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018e85c
    void SetP2PErrorHandlerCallback(void* /*cb*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018e8b8
    void SetP2PVoiceChatOpponentSpeakingCallback(void* /*cb*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 NetworkManager::SetGameCenterInitializationCallback @ 0x001044fc
    void SetGameCenterInitializationCallback(void* /*cb*/) {}

    // Defunct: NetworkManager -- no-op stub (symbol in list_methods)
    bool IsOnlineMultiplayer() const { return false; }

    // Defunct: NetworkManager -- no-op stub (symbol in list_methods)
    void P2PConnect() {}

    // Defunct: P2P multiplayer wave-sync -- no-op stub.
    // Binary: WaveManager broadcast call site @ 0x00122af8.
    void SyncWaveState() {}

    // Defunct: NetworkManager -- no-op stub (vtable slot 4)
    void SyncClear() {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 binary @ 0x0018d658
    void SpawnThreadController() {}

    void Init() {}
    void UpdateNetworking(float dt) { (void)dt; }

private:
    // ctor @ 0x0018e05c
    NetworkManager() {}

    // Pad to binary size 668 bytes on ARM32/Bada.
    // vptr occupies 4 bytes at +0x00; remaining 664 bytes cover the 9 delegates,
    // std::map, 3 BUTTON_INFO sub-objects, and 4 trailing flag bytes.
    // Not accessed by port code; online services are not ported.
    uint8_t m_pad[664];
};

#if defined(__bada__)
static_assert(sizeof(NetworkManager) == 668,
    "Mortar::NetworkManager must be 668 bytes on ARM32/Bada");
#endif

// Defunct: online-services -- returns 0 (OpenFeint provider);
// binary queries a GOT flag to determine active provider (OF=0, GC=1).
// no-op stub; binary addr unknown.
int GetSocialNetworkProvider();

// Defunct: social publish -- no-op stub; v1.6.1 Mortar::DefaultPublishTextCallback
// Default callback for NetworkManager::PublishTextWithCallback.
void DefaultPublishTextCallback(int result);

// Defunct: push notifications -- no-op stub; v1.6.1 Mortar::DefaultNotificationCallback
// Default handler for incoming push notifications.
void DefaultNotificationCallback(const char* name, int i1, int i2);

// Defunct: leaderboard retrieval -- no-op stub; v1.6.1 Mortar::DefaultRetrieveScoreCallback
// Default callback for NetworkManager::RetrieveLeaderboardScore.
void DefaultRetrieveScoreCallback(const char* board, int score, int rank, void* userdata);

// Defunct: online user data -- no-op stub; v1.6.1 Mortar::DefaultDownloadUserDataCallback
// Default callback for NetworkManager::DownloadUserDataFromLeaderboard.
void DefaultDownloadUserDataCallback(const char* board, void* data, int size);

// Defunct: social network registration -- no-op stub; v1.6.1 Mortar::RegisterSocial
// Registers social network handlers with the network layer on startup.
void RegisterSocial();

// Defunct: NetworkManager/social popup UI -- no-op stub; v1.6.1 Mortar::DefaultButtonCallback @0x00231084
// Reset callback stored in popup alert button delegates when DeregisterPopupAlertButton is called.
void DefaultButtonCallback();

} // namespace Mortar

// Defunct: online leaderboard -- no-op stub; v1.6.1 binary @ 0x0011f534.
// Returns true when the preferred network provider is online.
// Stub returns false so callers take the local/offline path.
bool IsProviderOnline();

// Defunct: online leaderboard -- no-op stub; v1.6.1 binary @ 0x0011f4a0.
// Returns true when the friends list has been loaded from the network provider.
// Stub returns false so callers take the local/offline path.
bool AreFriendsLoaded();

// Defunct: network provider selection -- no-op stubs; v1.6.1 @0x00169354 area
void AskUserToChoosePreferredNetwork();
void ChangePreferredNetworkProvider(long v);
long GetPrefNetwork();
void SetPrefNetwork(long v);

// Defunct: online-services notification -- no-op stub; v1.6.1 CustomNotificationCallback @0x001cf0cc
void CustomNotificationCallback(const char* name, int i1, int i2);

// Defunct: online leaderboard -- no-op stub; v1.6.1 CurrentUserName @0x001370c8
// Fills buf with an empty string (online services not available).
void CurrentUserName(char* buf, int size, Mortar::NetworkProvider provider);

// Defunct: GameCenter callback -- no-op stub; v1.6.1 GPostCallback @0x0010c144
// Called by GameCenter integration layer with a result code.
void GPostCallback(int result);

#endif // FN_ENGINE_NETWORK_NETWORK_MANAGER_H
