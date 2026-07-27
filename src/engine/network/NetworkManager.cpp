// Analysed: 2026-04-30T00:00
// Mortar::NetworkManager — online services stub. All methods are no-ops.
// See src/engine/network/NetworkManager.h for binary addresses and rationale.

#include "NetworkManager.h"

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

// Defunct: GameCenter callback -- no-op stub; v1.6.1 GPostCallback @0x0011c144
void GPostCallback(int /*result*/) {
}
