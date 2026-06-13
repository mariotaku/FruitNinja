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

// Defunct: online leaderboard -- no-op stub; binary @ 0x0011f534.
bool IsProviderOnline() {
    return false;
}

// Defunct: online leaderboard -- no-op stub; binary @ 0x0011f4a0.
bool AreFriendsLoaded() {
    return false;
}

} // namespace Mortar
