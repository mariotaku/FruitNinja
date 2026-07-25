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

// Per-end async connect state. Mirrors what a real socket transport would
// track while a connect()/accept() is in flight.
enum LoopbackConnectState {
    LOOPBACK_IDLE,        // Host()/Join() not yet called
    LOOPBACK_CONNECTING,  // Host()/Join() called, delay counter running
    LOOPBACK_CONNECTED,   // MP_EVT_CONNECTED delivered
    LOOPBACK_FAILED       // MP_EVT_CONNECT_FAILED delivered (terminal until Disconnect()/re-Host/Join)
};

// Shared backing store for one loopback pair. Owned jointly by the two
// LoopbackTransport ends created via CreatePair(); freed when both ends are
// destroyed (see LoopbackTransport::~LoopbackTransport).
struct LoopbackChannel {
    std::deque<std::vector<uint8_t> > qAtoB;
    std::deque<std::vector<uint8_t> > qBtoA;
    // MP-revival: per-end transport event queues (session-setup handshake
    // signals -- MP_EVT_CONNECTED/MP_EVT_NAMES/MP_EVT_DISCONNECTED/
    // MP_EVT_CONNECT_FAILED), separate from the data-message queues above.
    // eventsA is drained by end 0 (host), eventsB by end 1 (guest).
    std::deque<int> eventsA;
    std::deque<int> eventsB;

    // MP-revival: async connect state per end. `startedA`/`startedB` mark
    // that Host()/Join() has been called on that end (independent of the
    // other end) -- the pending connect only resolves to CONNECTED once BOTH
    // ends have started, modelling "the guest only connects once the host is
    // listening" without an instant same-tick resolution.
    LoopbackConnectState stateA;
    LoopbackConnectState stateB;
    bool startedA;
    bool startedB;
    // Ticks remaining before the in-flight connect attempt resolves (counted
    // down by Update-driven PollEvent()/Tick() calls -- see connectDelay).
    int ticksRemainingA;
    int ticksRemainingB;
    // MP-revival: failure-injection hook for tests -- when >=0, the NEXT
    // connect attempt on the matching end resolves to MP_EVT_CONNECT_FAILED
    // (with this value as DisconnectCode()) instead of MP_EVT_CONNECTED.
    // -1 = no injected failure (default: connects normally).
    int injectFailA;
    int injectFailB;

    int disconnectCodeA;
    int disconnectCodeB;

    int refCount;

    LoopbackChannel()
        : stateA(LOOPBACK_IDLE), stateB(LOOPBACK_IDLE),
          startedA(false), startedB(false),
          ticksRemainingA(0), ticksRemainingB(0),
          injectFailA(-1), injectFailB(-1),
          disconnectCodeA(1), disconnectCodeB(1),
          refCount(0) {}
};

// One end (0 or 1) of an in-process loopback transport pair.
//
// MP-revival: models an ASYNC, non-instant connect (see IMpTransport.h's
// contract) so tests exercise the same CONNECTING -> CONNECTED/FAILED timing
// a real socket transport would have, without any actual sockets or threads.
//
// Host()/Join() return quickly (true = "attempt accepted") and put this end
// into IsConnecting()==true, IsConnected()==false. The connect attempt only
// resolves once BOTH ends of the pair have called Host()/Join() (so a lone
// Host() with no Join() yet stays CONNECTING forever, like a real listen()
// with no peer) AND a small tick-delay (GetConnectDelayTicks(), default
// LOOPBACK_DEFAULT_CONNECT_DELAY) has elapsed on each end since it started.
// Every PollEvent() call ticks the delay counter down by one; once it hits 0
// this end's queue receives MP_EVT_CONNECTED then MP_EVT_NAMES (or, if a
// failure was injected via SetConnectShouldFail(), MP_EVT_CONNECT_FAILED
// alone). This makes the resolution timing test-controllable while staying
// purely poll-driven (no timers, no threads).
class LoopbackTransport : public IMpTransport {
public:
    // Ticks (PollEvent() calls) an in-flight connect takes to resolve once
    // both ends have started. Small by default so tests stay fast.
    static const int LOOPBACK_DEFAULT_CONNECT_DELAY = 2;

    // Creates a shared channel and returns its two ends. `a` is end 0
    // (LocalPlayerNumber() == 1, the "host" side), `b` is end 1
    // (LocalPlayerNumber() == 2, the "guest"/joining side). Caller owns the
    // returned pointers and must delete both; the shared LoopbackChannel is
    // refcounted and freed when the second end is destroyed.
    static void CreatePair(LoopbackTransport*& a, LoopbackTransport*& b);

    virtual ~LoopbackTransport();

    // Non-blocking: marks this end CONNECTING and returns true immediately
    // (see IMpTransport::Host contract). Actual MP_EVT_CONNECTED/
    // MP_EVT_CONNECT_FAILED delivery happens later via PollEvent().
    virtual bool Host();
    // Non-blocking: marks this end CONNECTING and returns true immediately
    // (see IMpTransport::Join contract).
    virtual bool Join(const char* endpoint);
    virtual void Disconnect();

    virtual bool IsConnected() const;
    virtual bool IsConnecting() const;

    // 1 for end 0 (host), 2 for end 1 (guest) -- see IMpTransport::LocalPlayerNumber.
    virtual int LocalPlayerNumber() const;

    virtual void Send(const uint8_t* data, int len, bool reliable);
    virtual int Poll(uint8_t* out, int cap);

    // MP-revival: pops this end's next queued session event, or MP_EVT_NONE.
    // Also the sole place the async connect-delay counter is ticked -- see
    // class comment. Never blocks: pure in-memory queue/counter logic.
    virtual int PollEvent();
    virtual int DisconnectCode() const;

    // Test-only: sets how many PollEvent() ticks a subsequent Host()/Join()
    // on this end takes to resolve. Overrides LOOPBACK_DEFAULT_CONNECT_DELAY
    // for this end only.
    void SetConnectDelayTicks(int ticks);

    // MP-revival: failure-injection hook for tests. When set, the NEXT
    // Host()/Join() call on this end resolves (after the usual delay) to
    // MP_EVT_CONNECT_FAILED with DisconnectCode()==code, instead of
    // MP_EVT_CONNECTED -- modelling a real transport's connect-timeout /
    // refused-connection path. Cleared after it fires once.
    void SetConnectShouldFail(int code);

private:
    LoopbackTransport(LoopbackChannel* channel, int end);

    // Shared implementation for Host()/Join() -- both start an async connect
    // attempt identically on a loopback (no distinct listen/dial behavior).
    bool StartConnect();

    LoopbackChannel* m_Channel;
    int m_End; // 0 or 1
    int m_DelayTicksOverride; // -1 = use LOOPBACK_DEFAULT_CONNECT_DELAY
};

} // namespace Mortar

#endif // FN_ENGINE_NETWORK_LOOPBACKTRANSPORT_H
