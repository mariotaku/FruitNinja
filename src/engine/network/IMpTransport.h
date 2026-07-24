#ifndef FN_ENGINE_NETWORK_IMPTRANSPORT_H
#define FN_ENGINE_NETWORK_IMPTRANSPORT_H

#include <stdint.h>

// Port-only enhancement (feat/mp-revival): no binary counterpart, so no
// // DIFFERS / // ASM-verified markers apply to this file.
//
// Transport seam for online-MP revival. The original v1.6.1 P2P networking
// stack sat on a dead Bada backend (see network/NetworkManager.cpp,
// network/P2PMessageHandling.cpp -- stubbed per the online-services-audit).
// This interface isolates "how bytes get from one player to another" behind
// a small abstract API so the game-side MP logic can be ported as real code
// against ANY concrete transport (in-process loopback for tests, a future
// real socket/relay backend for real play) without depending on Bada's P2P.

namespace Mortar {

// Contract:
//  - Host()/Join() are async-starting: they may return true immediately
//    (queued/connecting) or once actually connected, depending on the
//    concrete backend. Poll IsConnecting()/IsConnected() to track progress.
//  - Send() enqueues exactly ONE whole message (message-framed, not a byte
//    stream) for delivery to the remote peer. `reliable` selects a
//    reliable/ordered vs. best-effort delivery mode where the backend
//    supports the distinction; implementations that only offer one mode may
//    ignore the flag.
//  - Poll() delivers at most one whole message per call: it copies the next
//    queued incoming message into `out` (up to `cap` bytes) and returns its
//    length, or 0 if no message is queued. Callers loop Poll() until it
//    returns 0 to drain all pending messages in a tick.
class IMpTransport {
public:
    virtual ~IMpTransport() {}

    // Become the session owner and wait for a peer to Join().
    virtual bool Host() = 0;

    // Connect to a host at `endpoint` (backend-defined address string).
    virtual bool Join(const char* endpoint) = 0;

    // Tear down the session. Safe to call when not connected.
    virtual void Disconnect() = 0;

    virtual bool IsConnected() const = 0;
    virtual bool IsConnecting() const = 0;

    // 0 for the host, 1 for the joining peer -- maps to the binary's
    // m_PlayerIdx P2P/EntityTracker partition (see docs/engine/
    // online-services-audit.md and the input-path audit, #158).
    virtual int LocalPlayerNumber() const = 0;

    // Enqueue one whole message [data, data+len) for delivery to the peer.
    virtual void Send(const uint8_t* data, int len, bool reliable) = 0;

    // Pop the next queued incoming message into `out` (up to `cap` bytes).
    // Returns the message length, or 0 if no message is queued.
    virtual int Poll(uint8_t* out, int cap) = 0;
};

} // namespace Mortar

#endif // FN_ENGINE_NETWORK_IMPTRANSPORT_H
