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

// MP-revival: transport-level session events, distinct from in-band data
// packets (NetworkPacket subclasses carried by Send()/Poll()). Mirrors the
// iOS 1.5 msgCode values 8/9 that GlobalP2PMessageHandler switches on before
// falling into its data-packet dispatch (see P2PMessageHandling.cpp's
// two-level switch: Mortar::P2PMessage msg first -- P2PMSG_CONNECTED/NAMES
// match these same numeric values -- then packet->m_PacketType for
// P2PMSG_DATA).
// ASM-spec iOS1.5 GlobalP2PMessageHandler @0x000389a0 (case 8 / case 9).
enum MpTransportEvent {
    MP_EVT_NONE           = 0,
    MP_EVT_CONNECTED      = 8,  // session established -- mirrors iOS msgCode 8
    MP_EVT_NAMES          = 9,  // peer names available -- mirrors iOS msgCode 9
    MP_EVT_DISCONNECTED   = 10,
    // MP-revival: a connect ATTEMPT (Host()/Join()) failed or timed out before
    // ever reaching MP_EVT_CONNECTED. Distinct from MP_EVT_DISCONNECTED, which
    // is a drop AFTER a session was established. DisconnectCode() carries the
    // reason (e.g. 2=timeout) using the same code space HandleDisconnection
    // already switches on.
    MP_EVT_CONNECT_FAILED = 11
};

// ASYNC / NON-BLOCKING CONTRACT -- the game loop is single-threaded ~60fps and
// must NEVER block on network I/O. Every method here returns within the
// current frame; nothing here may sleep, spin, or perform blocking socket
// calls. This is why the contract is poll-driven rather than callback- or
// future-based: it plugs into the existing per-frame Update() shape with zero
// threading.
//
// Contract:
//  - Host()/Join(endpoint) are NON-BLOCKING and return QUICKLY: the return
//    value means only "the connect attempt was accepted/started", NOT "we are
//    connected". After either call returns, the transport is in a CONNECTING
//    state: IsConnecting()==true, IsConnected()==false. The actual connection
//    (or failure) completes LATER, asynchronously, surfaced only through
//    PollEvent():
//      * MP_EVT_CONNECTED       -- connect succeeded. IsConnected() becomes
//                                   true, IsConnecting() becomes false.
//      * MP_EVT_CONNECT_FAILED  -- connect failed or timed out.
//                                   IsConnecting() becomes false,
//                                   IsConnected() stays false.
//    Callers MUST poll IsConnecting()/IsConnected()/PollEvent() once per
//    frame to track progress; there is no synchronous "wait until connected"
//    entry point by design.
//  - A session-start side effect (e.g. the msgCode-8 handshake) must only run
//    after an observed MP_EVT_CONNECTED, never optimistically off Host()/
//    Join()'s return value.
//  - Send()/Poll() while not connected (IsConnected()==false, whether IDLE,
//    CONNECTING, or after a failed/dropped connection) are no-ops: Send()
//    silently discards, Poll() returns 0. Never blocks, never crashes.
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

    // Non-blocking: starts a connect attempt and returns immediately. `true`
    // means the attempt was accepted (now CONNECTING), not that a peer is
    // connected yet -- poll IsConnecting()/IsConnected()/PollEvent() to learn
    // the outcome. Become the session owner and wait for a peer to Join().
    virtual bool Host() = 0;

    // Non-blocking: starts a connect attempt to `endpoint` (backend-defined
    // address string) and returns immediately. Same "accepted, not connected"
    // return-value semantics as Host() -- see the class-level contract above.
    virtual bool Join(const char* endpoint) = 0;

    // Tear down the session. Safe to call when not connected.
    virtual void Disconnect() = 0;

    virtual bool IsConnected() const = 0;
    virtual bool IsConnecting() const = 0;

    // 1 for the host, 2 for the joining peer -- maps to the binary's
    // m_PlayerIdx P2P/EntityTracker partition (see docs/engine/
    // online-services-audit.md and the input-path audit, #158).
    // MP-revival: guest==2 is load-bearing -- StartGamePacket cmd2's RNG
    // reseed gate (WaveManager::SetOnlineSeed) fires only when
    // NetworkManager::GetLocalPlayerNumber()==2, mirroring the iOS 1.5
    // session-setup handshake where the host keeps its own seed and only the
    // joining peer reseeds from the host's broadcast seed.
    virtual int LocalPlayerNumber() const = 0;

    // Enqueue one whole message [data, data+len) for delivery to the peer.
    virtual void Send(const uint8_t* data, int len, bool reliable) = 0;

    // Pop the next queued incoming message into `out` (up to `cap` bytes).
    // Returns the message length, or 0 if no message is queued.
    virtual int Poll(uint8_t* out, int cap) = 0;

    // MP-revival: pop the next queued transport EVENT (session-setup
    // handshake signal -- MP_EVT_CONNECTED/MP_EVT_NAMES/MP_EVT_DISCONNECTED/
    // MP_EVT_CONNECT_FAILED), as opposed to a data packet. Returns
    // MP_EVT_NONE when no event is queued. Callers loop PollEvent() until
    // MP_EVT_NONE, same drain pattern as Poll(). Events and data messages are
    // queued/drained independently (separate queues) -- draining one does not
    // affect the other. Never blocks: purely pops from an in-memory queue.
    virtual int PollEvent() = 0;

    // MP-revival: reason code for the most recent MP_EVT_DISCONNECTED or
    // MP_EVT_CONNECT_FAILED event (see HandleDisconnection's `code` param in
    // P2PMessageHandling.h for the code->reason-string mapping). Default 1
    // ("peer left") when the backend has no richer classification.
    virtual int DisconnectCode() const { return 1; }
};

} // namespace Mortar

#endif // FN_ENGINE_NETWORK_IMPTRANSPORT_H
