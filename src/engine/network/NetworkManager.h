#ifndef FN_ENGINE_NETWORK_NETWORK_MANAGER_H
#define FN_ENGINE_NETWORK_NETWORK_MANAGER_H

// Defunct: NetworkManager (GameSpy/GameCenter/OpenFeint/P2P/news) -- no-op stub.
// All methods are no-ops returning safe defaults.
//
// v1.6.1 class metadata:
//   vtable       @0x002cfe68  (0x4c bytes = 17 slots)
//   ctor         @0x00231c40  (0x23c bytes; inits 9 Delegates, 1 std::map,
//                              3 BUTTON_INFO sub-structs, flag fields)
//   dtor         @0x002316a8
//   GetInstance  @0x00231e7c  (over a 0x29c-byte manager static @0x0034fa5c)
// Size: 0x29c = 668 bytes.

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

    // Polymorphic root: vptr @ +0x00; v1.6.1 vtable @0x002cfe68 (17 slots).
    virtual ~NetworkManager() {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::NetworkManager @0x00231c40 area
    void Initialise(int /*flags*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::Update @0x002310c8 (nearly idle)
    void Update(float /*dt*/) {}

    // Defunct: NetworkManager -- no-op stub
    void Draw(float /*dt*/) {}

    // Defunct: NetworkManager -- no-op stub
    void Destroy() {}

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool IsOnline() { return false; }

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool HasCredentials() { return false; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::UserHasEnabledNetwork @0x00231308 (returns 1)
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

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::InitializeP2P @0x002315d0 (0xd8 bytes -- a REAL body, not a stub)
    void InitializeP2P(void* /*cb1*/, void* /*cb2*/, void* /*cb3*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::DisconnectP2P @0x00231090
    bool DisconnectP2P(bool /*b*/) { return false; }

    // Defunct: NetworkManager -- no-op stub (returns 0)
    int GetLocalPlayerNumber() { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::GetPlayerName @0x002313c4
    void GetPlayerName(int /*idx*/, char* out, int /*cap*/) { if (out) out[0] = 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::IsInP2PGame @0x002323b0
    int IsInP2PGame() const { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::OnP2PGameOver @0x002323a8
    void OnP2PGameOver() {}

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

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::IsAnyPeerReadyForMultiplayer @0x00232388
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

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::SetLeaderboardScore @0x002312b4 (returns 0)
    int SetLeaderboardScore(const char* /*board*/, long long /*score*/, void* /*userdata*/, int /*flags*/) {
        return 0;
    }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::RetrieveLeaderboardScore(
    //   char const*, bool, bool, Delegate4<bool,char const*,long long,int,void*>, int, bool,
    //   bool, NetworkProvider) @0x00231868.
    // Binary has TWO overloads differing only in the Delegate4 score type: @0x00231868 takes
    // long long and drives the retrieve path (sets field_0xbc=1 + pM_RetrieveLeaderboardCb);
    // @0x00231914 takes int and sets field_0xbc=0 + pM_SetLeaderboardScoreCb. The port's
    // 3-arg stub signature matches NEITHER; @0x00231868 is cited because the port's
    // leaderboard score type is long long throughout (see SetLeaderboardScore above).
    int RetrieveLeaderboardScore(const char* /*board*/, int /*flags*/, void* /*cb*/) { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::RetrieveLeaderboardScoreNextPage @0x002312bc
    int RetrieveLeaderboardScoreNextPage() { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::RetrieveLeaderboardScorePreviousPage @0x002312c0
    int RetrieveLeaderboardScorePreviousPage() { return 0; }

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool CanPageUpHighscores() { return false; }

    // Defunct: NetworkManager -- no-op stub (returns false)
    bool CanPageDownHighscores() { return false; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1
    // Mortar::NetworkManager::DownloadUserDataFromLeaderboard(char const*, bool, bool,
    //   Delegate3<void,char const*,void*,int>) @0x002312d4 (empty body).
    // The other overload @0x002317c4 is the 2-arg convenience wrapper that forwards
    // (board, false, false, cb) into this one; the port's 4-arg signature matches THIS one.
    int DownloadUserDataFromLeaderboard(const char* /*board*/, bool /*friends*/, bool /*local*/, void* /*cb*/) { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::IsGameCenterOnline @0x002313f0
    bool IsGameCenterOnline() { return false; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::IsGameCenterSupported @0x00231400
    int IsGameCenterSupported() { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::IsGameCenterAttemptingToConnect @0x002313f8
    int IsGameCenterAttemptingToConnect() { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::IsGameCenterInterfaceDisplayed @0x00231408
    int IsGameCenterInterfaceDisplayed() { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::SetGameCenterShouldLoadOnStartup @0x00231410 (passthrough)
    bool SetGameCenterShouldLoadOnStartup(bool b) { return b; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::ConnectGameCenter @0x002313b4
    NetworkManager* ConnectGameCenter() { return this; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::AreGameCenterConnectionAttemptsAllowed @0x002313b8 (returns 1)
    bool AreGameCenterConnectionAttemptsAllowed(bool /*b*/) { return true; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::LaunchDashboard @0x00231270
    void LaunchDashboard(int /*id*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::LaunchDashboardWithLeaderboard @0x00231278
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

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::StartNewsDownload @0x00231320
    void StartNewsDownload() {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::HasNewsBeenDownloaded @0x00231324 (returns 1)
    bool HasNewsBeenDownloaded() { return true; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::HasUnreadNews @0x00231318
    int HasUnreadNews() { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::UpdateNews @0x00231170
    // Returns int (0 = no news being displayed); MainScreen::Update case 0xb
    // (@0x001975c0) breaks out of the state while this returns nonzero.
    int UpdateNews(float /*dt*/) { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::DrawNews @0x00231160
    void DrawNews() {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::StartNewsDisplay @0x0023132c
    void StartNewsDisplay(void* /*tex*/, void* /*font*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::CancelNewsDisplay @0x00231208
    void CancelNewsDisplay() {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::GetCurrentNews @0x00231358
    int GetCurrentNews(char* out, int /*cap*/) { if (out) out[0] = 0; return 0; }

    // Defunct: NetworkManager -- no-op stub (returns nullptr)
    OpenFeintNewsRenderer* GetNewsRenderer() { return 0; }

    // Defunct: NetworkManager -- no-op stub (returns nullptr)
    void* GetNewsRenderInfo() { return 0; }

    // Defunct: NetworkManager -- no-op stub
    void OpenUrl(const char* /*url*/) {}

    // Defunct: NetworkManager -- no-op stub
    void ForceModalDialogsClosed() {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::SetPreferredNetworkProvider @0x00231228
    void SetPreferredNetworkProvider(int /*provider*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::GetPreferredNetworkProvider @0x00231230
    int GetPreferredNetworkProvider() const { return 0; }

    // Defunct: NetworkManager -- no-op stub. NOT a NetworkManager member in the binary:
    // v1.6.1 ::ChangePreferredNetworkProvider is the FREE function @0x001ca9f8 (also
    // declared free at the bottom of this header). Kept as a member for the port's
    // existing call sites; do not treat the member as a binary symbol.
    void ChangePreferredNetworkProvider(long /*v*/) {}

    // Defunct: online-services -- no-op stub. SHAPE DIVERGENCE: IsProviderOnline is NOT a
    // NetworkManager member in v1.6.1 -- it is the FREE function ::IsProviderOnline()
    // @0x0011f534 (declared at the bottom of this header). This member exists only for
    // the port's call sites; it has no binary counterpart and must not be symbol-diffed.
    int IsProviderOnline() { return 0; }

    // Defunct: P2P multiplayer -- no-op stub. SHAPE DIVERGENCE: IsP2POnline is NOT a
    // NetworkManager member in v1.6.1 -- it is the FREE function ::IsP2POnline()
    // @0x0011f524, whose body is NetworkManager::GetInstance()->IsGameCenterOnline(),
    // NOT a hardcoded false (see P2PMessageHandling.h). This member exists only for the
    // port's call sites; it has no binary counterpart and must not be symbol-diffed.
    int IsP2POnline() { return 0; }

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::HasFriendsLoaded @0x002313e8 (returns 1)
    bool HasFriendsLoaded() { return true; }

    // Defunct: P2P multiplayer -- no-op stub. NOT a NetworkManager member in the binary:
    // v1.6.1 ::GlobalP2PMessageHandler is the FREE function @0x0015761c (empty body;
    // also declared free in P2PMessageHandling.h). Member kept for port call sites.
    void GlobalP2PMessageHandler(void* /*msg*/, void* /*packet*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::GlobalP2PErrorHandler
    void GlobalP2PErrorHandler(void* /*err*/) {}

    // Defunct: NetworkManager -- no-op stub (called from ctor body)
    void SetStatusMessageTextDefaults() {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::SetP2PMessageHandlerCallback
    void SetP2PMessageHandlerCallback(void* /*cb*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::SetP2PErrorHandlerCallback
    void SetP2PErrorHandlerCallback(void* /*cb*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::SetP2PVoiceChatOpponentSpeakingCallback
    void SetP2PVoiceChatOpponentSpeakingCallback(void* /*cb*/) {}

    // Defunct: NetworkManager -- no-op stub; v1.6.1 NetworkManager::SetGameCenterInitializationCallback @ 0x0011e288
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

    // Defunct: NetworkManager -- no-op stub; v1.6.1 Mortar::NetworkManager::SpawnThreadController @0x0023125c
    void SpawnThreadController() {}

    void Init() {}
    void UpdateNetworking(float dt) { (void)dt; }

private:
    // v1.6.1 Mortar::NetworkManager::NetworkManager @0x00231c40
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

// Defunct: online leaderboard -- no-op stub; v1.6.1 ::IsProviderOnline @0x0011f534.
// Returns true when the preferred network provider is online.
// Stub returns false so callers take the local/offline path.
bool IsProviderOnline();

// Defunct: online leaderboard -- no-op stub; v1.6.1 binary @ 0x0011f4a0.
// Returns true when the friends list has been loaded from the network provider.
// Stub returns false so callers take the local/offline path.
bool AreFriendsLoaded();

// Defunct: network provider selection -- no-op stubs;
// v1.6.1 ::ChangePreferredNetworkProvider @0x001ca9f8 area
void AskUserToChoosePreferredNetwork();
void ChangePreferredNetworkProvider(long v);
long GetPrefNetwork();
void SetPrefNetwork(long v);

// Defunct: online-services notification -- no-op stub; v1.6.1 CustomNotificationCallback @0x001cf0cc
void CustomNotificationCallback(const char* name, int i1, int i2);

// Defunct: online leaderboard -- no-op stub; v1.6.1 CurrentUserName @0x001370c8
// Fills buf with an empty string (online services not available).
void CurrentUserName(char* buf, int size, Mortar::NetworkProvider provider);

// Defunct: GameCenter callback -- no-op stub; v1.6.1 GPostCallback @0x0011c144
// Called by GameCenter integration layer with a result code.
void GPostCallback(int result);

#endif // FN_ENGINE_NETWORK_NETWORK_MANAGER_H
