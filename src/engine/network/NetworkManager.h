#ifndef FN_ENGINE_NETWORK_NETWORK_MANAGER_H
#define FN_ENGINE_NETWORK_NETWORK_MANAGER_H

// Analysed: 2026-04-30T00:00
//
// Mortar::NetworkManager -- OpenFeint + GameCenter + P2P multiplayer manager.
// Skipped for port per project policy (online services defunct).
// Size: 668 bytes. Ctor inits fields up to 0x29B: 9 Delegates, 1 std::map,
//       3 BUTTON_INFO sub-structs, flag fields.
//
// Binary addresses:
//   ctor (real)    0x0018e05c
//   ctor (alias)   0x0018e25c
//   ctor thunk     0x00100518
//   dtor (regular) 0x0018da94
//   dtor (deleting)0x0018dba4
//   GetInstance    0x0018e210

#include <cstdint>

namespace Mortar {

class NetworkManager {
public:
    static NetworkManager* GetInstance() {
        static NetworkManager s_instance;
        return &s_instance;
    }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d668
    int LaunchDashboard(int) { return 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d66c
    int LaunchDashboardWithLeaderboard(int, int) { return 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d658
    void SpawnThreadController() {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018e698
    int IsAnyPeerReadyForMultiplayer() { return 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d6ac
    int DownloadUserDataFromLeaderboard(const char*, bool, bool, void*) { return 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018dd2c
    void DrawNews() {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018dcb4
    void CancelNewsDisplay() {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d6d4
    int HasUnreadNews() { return 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d6d8
    void StartNewsDownload() {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d968
    void StartNewsDisplay(void*, void*) {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d9bc
    void UpdateNews(float /*dt*/) {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d948
    int GetCurrentNews(char* out, int /*cap*/) { if (out) out[0] = 0; return 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d924
    void GetPlayerName(int /*idx*/, char* out, int /*cap*/) { if (out) out[0] = 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d634
    int GetPreferredNetworkProvider() const { return 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d62c
    void SetPreferredNetworkProvider(int /*provider*/) {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x00169354
    void ChangePreferredNetworkProvider(long /*v*/) {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d6b0 (returns 0)
    int IsProviderOnline() { return 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d6b4 (returns 0)
    int IsP2POnline() { return 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d704
    int IsGameCenterSupported() { return 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d6fc
    bool IsGameCenterOnline() { return false; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d700
    int IsGameCenterAttemptingToConnect() { return 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d708
    int IsGameCenterInterfaceDisplayed() { return 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d70c (passthrough)
    bool SetGameCenterShouldLoadOnStartup(bool b) { return b; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d6e4
    NetworkManager* ConnectGameCenter() { return this; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d6e8 (returns 1, not false)
    bool AreGameCenterConnectionAttemptsAllowed(bool /*b*/) { return true; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d6dc (returns 1)
    bool HasNewsBeenDownloaded() { return true; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d6cc (returns 1)
    bool UserHasEnabledNetwork() { return true; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d6f8 (returns 1)
    bool HasFriendsLoaded() { return true; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d698 (returns 0)
    int SetLeaderboardScore(const char* /*board*/, long long /*score*/, void* /*userdata*/, int /*flags*/) {
        return 0;
    }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018e6b8
    int IsInP2PGame() const { return 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018e6b0
    void OnP2PGameOver() {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x00169280
    void GlobalP2PMessageHandler(void*, void*) {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0016b444
    void GlobalP2PErrorHandler(void*) {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d7e4
    int RetrieveLeaderboardScore(const char*, int, void*) { return 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d69c
    int RetrieveLeaderboardScoreNextPage() { return 0; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d6a0
    int RetrieveLeaderboardScorePreviousPage() { return 0; }

    // Defunct: NetworkManager -- no-op stub (called from ctor body)
    void DeregisterAllPopupAlertButtons() {}

    // Defunct: NetworkManager -- no-op stub (called from ctor body)
    void SetStatusMessageTextDefaults() {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018e800
    void SetP2PMessageHandlerCallback(void*) {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018e85c
    void SetP2PErrorHandlerCallback(void*) {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018e8b8
    void SetP2PVoiceChatOpponentSpeakingCallback(void*) {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d9d8
    void InitializeP2P(void*, void*, void*) {}

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0018d6f4
    bool DisconnectP2P(bool /*b*/) { return false; }

    // Defunct: NetworkManager -- no-op stub; binary @ 0x0010c688
    void SetGameCenterInitializationCallback(void*) {}

    // Defunct: NetworkManager -- no-op stub (symbol in list_methods)
    bool IsOnlineMultiplayer() const { return false; }

    // Defunct: NetworkManager -- no-op stub (symbol in list_methods)
    void P2PConnect() {}

    // Defunct: P2P multiplayer wave-sync -- no-op stub.
    // Binary: WaveManager broadcast call site @ 0x00122af8.
    void SyncWaveState() {}

    // Defunct: NetworkManager -- no-op stub (vtable slot 4)
    void SyncClear() {}

    void Init() {}
    void Destroy() {}
    void UpdateNetworking(float dt) { (void)dt; }

private:
    // ctor @ 0x0018e05c
    NetworkManager() {}
    ~NetworkManager() {}

    // Pad to approximate binary size (668 bytes) for informational reference.
    // Not accessed by port code; online services are not ported.
    uint8_t m_pad[668];
};

// Defunct: online-services -- returns 0 (OpenFeint provider);
// binary queries a GOT flag to determine active provider (OF=0, GC=1).
// no-op stub; binary addr unknown.
int GetSocialNetworkProvider();

} // namespace Mortar

#endif // FN_ENGINE_NETWORK_NETWORK_MANAGER_H
