#include "LoopbackTransport.h"

#include <cstring>
#include <cstddef>

// Port-only enhancement (feat/mp-revival): no binary counterpart, so no
// // DIFFERS / // ASM-verified markers apply to this file.

namespace Mortar {

void LoopbackTransport::CreatePair(LoopbackTransport*& a, LoopbackTransport*& b) {
    LoopbackChannel* channel = new LoopbackChannel();
    channel->refCount = 2;
    a = new LoopbackTransport(channel, 0);
    b = new LoopbackTransport(channel, 1);
}

LoopbackTransport::LoopbackTransport(LoopbackChannel* channel, int end)
    : m_Channel(channel), m_End(end) {
}

LoopbackTransport::~LoopbackTransport() {
    if (m_Channel != NULL) {
        if (m_End == 0) {
            m_Channel->connectedA = false;
        } else {
            m_Channel->connectedB = false;
        }
        --m_Channel->refCount;
        if (m_Channel->refCount <= 0) {
            delete m_Channel;
        }
        m_Channel = NULL;
    }
}

bool LoopbackTransport::Host() {
    if (m_End == 0) {
        m_Channel->connectedA = true;
        m_Channel->eventsA.push_back(MP_EVT_CONNECTED);
        m_Channel->eventsA.push_back(MP_EVT_NAMES);
    } else {
        m_Channel->connectedB = true;
        m_Channel->eventsB.push_back(MP_EVT_CONNECTED);
        m_Channel->eventsB.push_back(MP_EVT_NAMES);
    }
    return true;
}

bool LoopbackTransport::Join(const char* endpoint) {
    (void)endpoint;
    if (m_End == 0) {
        m_Channel->connectedA = true;
        m_Channel->eventsA.push_back(MP_EVT_CONNECTED);
        m_Channel->eventsA.push_back(MP_EVT_NAMES);
    } else {
        m_Channel->connectedB = true;
        m_Channel->eventsB.push_back(MP_EVT_CONNECTED);
        m_Channel->eventsB.push_back(MP_EVT_NAMES);
    }
    return true;
}

void LoopbackTransport::Disconnect() {
    if (m_End == 0) {
        m_Channel->connectedA = false;
        m_Channel->eventsA.push_back(MP_EVT_DISCONNECTED);
    } else {
        m_Channel->connectedB = false;
        m_Channel->eventsB.push_back(MP_EVT_DISCONNECTED);
    }
}

bool LoopbackTransport::IsConnected() const {
    return m_End == 0 ? m_Channel->connectedA : m_Channel->connectedB;
}

bool LoopbackTransport::IsConnecting() const {
    // Loopback has no handshake latency -- Host()/Join() connect instantly,
    // so there is never an in-between "connecting" state.
    return false;
}

int LoopbackTransport::LocalPlayerNumber() const {
    // MP-revival: host==1, guest==2 (see IMpTransport::LocalPlayerNumber --
    // guest==2 is load-bearing for the StartGamePacket cmd2 RNG reseed gate).
    return m_End == 0 ? 1 : 2;
}

void LoopbackTransport::Send(const uint8_t* data, int len, bool reliable) {
    (void)reliable; // loopback queues are always reliable/ordered
    if (data == NULL || len <= 0) {
        return;
    }
    std::vector<uint8_t> msg(data, data + len);
    std::deque<std::vector<uint8_t> >& outQueue =
        (m_End == 0) ? m_Channel->qAtoB : m_Channel->qBtoA;
    outQueue.push_back(msg);
}

int LoopbackTransport::Poll(uint8_t* out, int cap) {
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
    std::deque<int>& evQueue = (m_End == 0) ? m_Channel->eventsA : m_Channel->eventsB;
    if (evQueue.empty()) {
        return MP_EVT_NONE;
    }
    int e = evQueue.front();
    evQueue.pop_front();
    return e;
}

int LoopbackTransport::DisconnectCode() const {
    return 1; // loopback has no richer classification -- "peer left" always
}

} // namespace Mortar
