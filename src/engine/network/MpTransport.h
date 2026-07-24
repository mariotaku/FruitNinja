#ifndef FN_ENGINE_NETWORK_MPTRANSPORT_H
#define FN_ENGINE_NETWORK_MPTRANSPORT_H

// Port-only enhancement (feat/mp-revival): no binary counterpart, so no
// // DIFFERS / // ASM-verified markers apply to this file.
//
// Global accessor for the active IMpTransport end. A test harness or a
// future matchmaker installs the concrete transport (LoopbackTransport, or a
// real socket/relay backend later) via SetMpTransport(); the rest of the MP
// code (SendP2PPacket, NetworkManager::Update's inbound pump, the online
// predicates in P2PMessageHandling.cpp) reads it back via GetMpTransport().
// Null means "offline" -- every caller must null-check before use.
//
// No ownership: this module never new/deletes the transport. Whoever installs
// it (test fixture, matchmaker) is responsible for its lifetime and must call
// SetMpTransport(NULL) before destroying it.

namespace Mortar {

class IMpTransport;

void SetMpTransport(IMpTransport* t);
IMpTransport* GetMpTransport();

} // namespace Mortar

#endif // FN_ENGINE_NETWORK_MPTRANSPORT_H
