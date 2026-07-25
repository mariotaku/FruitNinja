#ifndef FN_ENGINE_NETWORK_LOOPBACKTRANSPORT_H
#define FN_ENGINE_NETWORK_LOOPBACKTRANSPORT_H

#include "IMpTransport.h"

#include <deque>
#include <vector>

// Port-only enhancement (feat/mp-revival): no binary counterpart, so no
// // DIFFERS / // ASM-verified markers apply to this file.
//
// In-process loopback implementation of IMpTransport: pairs two ends over a
// shared pair of message queues (A->B, B->A), entirely in memory. No
// sockets, no threads -- both ends are ticked by the same process (a unit
// test, or a local hot-seat/dev session). Useful as the seam's reference
// implementation and as the transport for MP unit tests.

namespace Mortar {

// Shared backing store for one loopback pair. Owned jointly by the two
// LoopbackTransport ends created via CreatePair(); freed when both ends are
// destroyed (see LoopbackTransport::~LoopbackTransport).
struct LoopbackChannel {
    std::deque<std::vector<uint8_t> > qAtoB;
    std::deque<std::vector<uint8_t> > qBtoA;
    // MP-revival: per-end transport event queues (session-setup handshake
    // signals -- MP_EVT_CONNECTED/MP_EVT_NAMES/MP_EVT_DISCONNECTED), separate
    // from the data-message queues above. eventsA is drained by end 0 (host),
    // eventsB by end 1 (guest).
    std::deque<int> eventsA;
    std::deque<int> eventsB;
    bool connectedA;
    bool connectedB;
    int refCount;

    LoopbackChannel() : connectedA(false), connectedB(false), refCount(0) {}
};

// One end (0 or 1) of an in-process loopback transport pair.
// Host()/Join() resolve instantly (no real handshake exists for a
// same-process loopback): calling either marks this end connected and
// returns true immediately.
//
// MP-revival: Host()/Join() also QUEUE the session-setup event sequence for
// that end -- MP_EVT_CONNECTED then MP_EVT_NAMES, popped one at a time via
// successive PollEvent() calls (mirrors iOS 1.5 msgCodes 8/9; see
// IMpTransport.h). Disconnect() queues MP_EVT_DISCONNECTED.
class LoopbackTransport : public IMpTransport {
public:
    // Creates a shared channel and returns its two ends. `a` is end 0
    // (LocalPlayerNumber() == 1, the "host" side), `b` is end 1
    // (LocalPlayerNumber() == 2, the "guest"/joining side). Caller owns the
    // returned pointers and must delete both; the shared LoopbackChannel is
    // refcounted and freed when the second end is destroyed.
    static void CreatePair(LoopbackTransport*& a, LoopbackTransport*& b);

    virtual ~LoopbackTransport();

    virtual bool Host();
    virtual bool Join(const char* endpoint);
    virtual void Disconnect();

    virtual bool IsConnected() const;
    virtual bool IsConnecting() const;

    // 1 for end 0 (host), 2 for end 1 (guest) -- see IMpTransport::LocalPlayerNumber.
    virtual int LocalPlayerNumber() const;

    virtual void Send(const uint8_t* data, int len, bool reliable);
    virtual int Poll(uint8_t* out, int cap);

    // MP-revival: pops this end's next queued session event, or MP_EVT_NONE.
    virtual int PollEvent();
    virtual int DisconnectCode() const;

private:
    LoopbackTransport(LoopbackChannel* channel, int end);

    LoopbackChannel* m_Channel;
    int m_End; // 0 or 1
};

} // namespace Mortar

#endif // FN_ENGINE_NETWORK_LOOPBACKTRANSPORT_H
