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

} // namespace Mortar

// Defunct: online leaderboard -- no-op stub; v1.6.1 IsProviderOnline @0x0011f534.
bool IsProviderOnline() {
    return false;
}

// Defunct: online leaderboard -- no-op stub; v1.6.1 AreFriendsLoaded @0x0011f4a0.
bool AreFriendsLoaded() {
    return false;
}

// Defunct: network provider selection -- no-op stub; v1.6.1 AskUserToChoosePreferredNetwork @0x00169354
void AskUserToChoosePreferredNetwork() {
}

// Defunct: network provider selection -- no-op stub; v1.6.1 ChangePreferredNetworkProvider @0x00169354
void ChangePreferredNetworkProvider(long /*v*/) {
}

// Defunct: network provider selection -- no-op stub; v1.6.1 GetPrefNetwork @0x0016916c
long GetPrefNetwork() {
    return 0;
}

// Defunct: network provider selection -- no-op stub; v1.6.1 SetPrefNetwork @0x0016917c
void SetPrefNetwork(long /*v*/) {
}

// Defunct: online-services notification -- no-op stub; v1.6.1 CustomNotificationCallback @0x001cf0cc
// Sets m_bUpdatesSuspended=1 on notification-shown (see GameWork.h +0x195).
// Stub does nothing; online notifications are never shown.
void CustomNotificationCallback(const char* /*name*/, int /*i1*/, int /*i2*/) {
}
