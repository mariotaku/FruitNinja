#include "LoopbackTransport.h"

#include <cstring>
#include <cstddef>

// Port-only enhancement (feat/mp-revival): no binary counterpart, so no
// // DIFFERS / // ASM-verified markers apply to this file.

namespace Mortar {

// Out-of-line definition for the in-class static const int -- needed if ever
// odr-used (e.g. bound by reference); harmless otherwise. GCC 4.4 is stricter
// about this than modern compilers.
const int LoopbackTransport::LOOPBACK_DEFAULT_CONNECT_DELAY;

void LoopbackTransport::CreatePair(LoopbackTransport*& a, LoopbackTransport*& b) {
    LoopbackChannel* channel = new LoopbackChannel();
    channel->refCount = 2;
    a = new LoopbackTransport(channel, 0);
    b = new LoopbackTransport(channel, 1);
}

LoopbackTransport::LoopbackTransport(LoopbackChannel* channel, int end)
    : m_Channel(channel), m_End(end), m_DelayTicksOverride(-1) {
}

LoopbackTransport::~LoopbackTransport() {
    if (m_Channel != NULL) {
        if (m_End == 0) {
            m_Channel->stateA = LOOPBACK_IDLE;
            m_Channel->startedA = false;
        } else {
            m_Channel->stateB = LOOPBACK_IDLE;
            m_Channel->startedB = false;
        }
        --m_Channel->refCount;
        if (m_Channel->refCount <= 0) {
            delete m_Channel;
        }
        m_Channel = NULL;
    }
}

bool LoopbackTransport::StartConnect() {
    // Non-blocking: mark CONNECTING and return immediately. Resolution
    // (CONNECTED/CONNECT_FAILED) happens later inside PollEvent() once the
    // per-end delay counter runs out -- see class comment in the header.
    int delay = (m_DelayTicksOverride >= 0) ? m_DelayTicksOverride : LOOPBACK_DEFAULT_CONNECT_DELAY;
    if (m_End == 0) {
        m_Channel->stateA = LOOPBACK_CONNECTING;
        m_Channel->startedA = true;
        m_Channel->ticksRemainingA = delay;
    } else {
        m_Channel->stateB = LOOPBACK_CONNECTING;
        m_Channel->startedB = true;
        m_Channel->ticksRemainingB = delay;
    }
    return true;
}

bool LoopbackTransport::Host() {
    return StartConnect();
}

bool LoopbackTransport::Join(const char* endpoint) {
    (void)endpoint;
    return StartConnect();
}

void LoopbackTransport::Disconnect() {
    if (m_End == 0) {
        bool wasConnected = (m_Channel->stateA == LOOPBACK_CONNECTED);
        m_Channel->stateA = LOOPBACK_IDLE;
        m_Channel->startedA = false;
        if (wasConnected) {
            m_Channel->eventsA.push_back(MP_EVT_DISCONNECTED);
        }
    } else {
        bool wasConnected = (m_Channel->stateB == LOOPBACK_CONNECTED);
        m_Channel->stateB = LOOPBACK_IDLE;
        m_Channel->startedB = false;
        if (wasConnected) {
            m_Channel->eventsB.push_back(MP_EVT_DISCONNECTED);
        }
    }
}

bool LoopbackTransport::IsConnected() const {
    LoopbackConnectState s = (m_End == 0) ? m_Channel->stateA : m_Channel->stateB;
    return s == LOOPBACK_CONNECTED;
}

bool LoopbackTransport::IsConnecting() const {
    LoopbackConnectState s = (m_End == 0) ? m_Channel->stateA : m_Channel->stateB;
    return s == LOOPBACK_CONNECTING;
}

void LoopbackTransport::SetConnectDelayTicks(int ticks) {
    m_DelayTicksOverride = ticks;
}

void LoopbackTransport::SetConnectShouldFail(int code) {
    if (m_End == 0) {
        m_Channel->injectFailA = code;
    } else {
        m_Channel->injectFailB = code;
    }
}

int LoopbackTransport::LocalPlayerNumber() const {
    // MP-revival: host==1, guest==2 (see IMpTransport::LocalPlayerNumber --
    // guest==2 is load-bearing for the StartGamePacket cmd2 RNG reseed gate).
    return m_End == 0 ? 1 : 2;
}

void LoopbackTransport::Send(const uint8_t* data, int len, bool reliable) {
    (void)reliable; // loopback queues are always reliable/ordered
    // Contract: Send() while not connected is a silent no-op (never blocks,
    // never crashes) -- see IMpTransport.h.
    if (!IsConnected() || data == NULL || len <= 0) {
        return;
    }
    std::vector<uint8_t> msg(data, data + len);
    std::deque<std::vector<uint8_t> >& outQueue =
        (m_End == 0) ? m_Channel->qAtoB : m_Channel->qBtoA;
    outQueue.push_back(msg);
}

int LoopbackTransport::Poll(uint8_t* out, int cap) {
    // Contract: Poll() while not connected returns 0 (no data pump before/
    // during a handshake) -- see IMpTransport.h.
    if (!IsConnected()) {
        return 0;
    }
    std::deque<std::vector<uint8_t> >& inQueue =
        (m_End == 0) ? m_Channel->qBtoA : m_Channel->qAtoB;
    if (inQueue.empty()) {
        return 0;
    }
    std::vector<uint8_t>& msg = inQueue.front();
    int copyLen = (int)msg.size();
    if (copyLen > cap) {
        copyLen = cap;
    }
    if (copyLen > 0 && out != NULL) {
        memcpy(out, &msg[0], copyLen);
    }
    inQueue.pop_front();
    return copyLen;
}

int LoopbackTransport::PollEvent() {
    // MP-revival: this is where an in-flight CONNECTING attempt is ticked
    // and, once both ends have started and the delay has elapsed, resolved
    // to CONNECTED (+ NAMES) or CONNECT_FAILED (if injected). Modelling the
    // tick here (rather than a separate Update()) keeps the transport purely
    // poll-driven, matching a real backend where PollEvent() is the only
    // per-frame touchpoint.
    LoopbackConnectState& state = (m_End == 0) ? m_Channel->stateA : m_Channel->stateB;
    bool otherStarted = (m_End == 0) ? m_Channel->startedB : m_Channel->startedA;
    int& ticksRemaining = (m_End == 0) ? m_Channel->ticksRemainingA : m_Channel->ticksRemainingB;
    int& injectFail = (m_End == 0) ? m_Channel->injectFailA : m_Channel->injectFailB;
    int& disconnectCode = (m_End == 0) ? m_Channel->disconnectCodeA : m_Channel->disconnectCodeB;
    std::deque<int>& evQueue = (m_End == 0) ? m_Channel->eventsA : m_Channel->eventsB;

    if (state == LOOPBACK_CONNECTING && otherStarted) {
        if (ticksRemaining > 0) {
            --ticksRemaining;
        }
        if (ticksRemaining <= 0) {
            if (injectFail >= 0) {
                disconnectCode = injectFail;
                injectFail = -1;
                state = LOOPBACK_FAILED;
                evQueue.push_back(MP_EVT_CONNECT_FAILED);
            } else {
                state = LOOPBACK_CONNECTED;
                evQueue.push_back(MP_EVT_CONNECTED);
                evQueue.push_back(MP_EVT_NAMES);
            }
        }
    }

    if (evQueue.empty()) {
        return MP_EVT_NONE;
    }
    int e = evQueue.front();
    evQueue.pop_front();
    return e;
}

int LoopbackTransport::DisconnectCode() const {
    return (m_End == 0) ? m_Channel->disconnectCodeA : m_Channel->disconnectCodeB;
}

} // namespace Mortar
