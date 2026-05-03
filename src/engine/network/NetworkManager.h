#ifndef FN_ENGINE_NETWORK_NETWORK_MANAGER_H
#define FN_ENGINE_NETWORK_NETWORK_MANAGER_H

// Analysed: 2026-04-30T00:00
//
// Mortar::NetworkManager — OpenFeint + GameCenter + P2P multiplayer manager.
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
//   SetP2PMessageHandlerCallback  thunk 0x000f3714
//   IsOnlineMultiplayer           (symbol in list_methods)
//   P2PConnect                    (symbol in list_methods)
//   ChangePreferredNetworkProvider (symbol in list_classes)
//   AreGameCenterConnectionAttemptsAllowed (symbol in list_classes)
//
// Placed in Mortar:: namespace; lives under src/engine/network/ per engine layout.

#include <cstdint>

namespace Mortar {

class NetworkManager {
public:
    static NetworkManager* GetInstance() {
        static NetworkManager s_instance;
        return &s_instance;
    }

    void Init() {}
    void Destroy() {}

    // @ 0x000f3714 thunk — no-op for port
    void SetP2PMessageHandlerCallback() {}

    // (symbol in list_methods) — returns false; no online multiplayer in port
    bool IsOnlineMultiplayer() const { return false; }

    // (symbol in list_methods) — no-op for port
    void P2PConnect() {}

    // (symbol in list_classes) — no-op for port
    void ChangePreferredNetworkProvider(long /*provider*/) {}

    // (symbol in list_classes) — returns false; GameCenter not available in port
    bool AreGameCenterConnectionAttemptsAllowed() const { return false; }

    // vtable slot 4 @ (binary addr TBD) — clears P2P sync state between rounds.
    // TODO: implement (binary @ 0x? -- re-analyst pass needed)
    void SyncClear() {}

    // Called from ctor body — no-op stubs
    void DeregisterAllPopupAlertButtons() {}
    void SetStatusMessageTextDefaults() {}

    void UpdateNetworking(float dt) { (void)dt; }

private:
    // ctor @ 0x0018e05c
    NetworkManager() {}
    ~NetworkManager() {}

    // Pad to approximate binary size (668 bytes) for informational reference.
    // Not accessed by port code; online services are not ported.
    uint8_t m_pad[668];
};

} // namespace Mortar

#endif // FN_ENGINE_NETWORK_NETWORK_MANAGER_H
