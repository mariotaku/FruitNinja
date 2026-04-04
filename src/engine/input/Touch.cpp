#include "input/Touch.h"
#include <cstring>

namespace Mortar {

Touch::Touch()
    : m_EventReadIdx(0)
    , m_EventWriteIdx(0)
    , m_EventCount(0)
    , m_NextTouchId(1)
{
    memset(m_FrontBuffer, 0, sizeof(m_FrontBuffer));
    memset(m_BackBuffer, 0, sizeof(m_BackBuffer));
    memset(m_EventRing, 0, sizeof(m_EventRing));

    for (int i = 0; i < MAX_TOUCHES; i++) {
        m_FrontBuffer[i].phase = 1; // released
        m_FrontBuffer[i].pointerId = -1;
        m_BackBuffer[i].phase = 1;
        m_BackBuffer[i].pointerId = -1;
    }
}

void Touch::PushEvent(uint32_t pointerId, bool isDown, float x, float y, float timestamp) {
    if (m_EventCount >= EVENT_RING_SIZE) return; // ring full, drop event

    TEvnt& evt = m_EventRing[m_EventWriteIdx];
    evt.pointerId = pointerId;
    evt.isDown = isDown ? 1 : 0;
    evt.x = x;
    evt.y = y;
    evt.timestamp = timestamp;

    m_EventWriteIdx = (m_EventWriteIdx + 1) % EVENT_RING_SIZE;
    m_EventCount++;
}

int Touch::FindOrAllocSlot(uint32_t pointerId) {
    // Find existing slot
    for (int i = 0; i < MAX_TOUCHES; i++) {
        if (m_BackBuffer[i].pointerId == (int)pointerId && m_BackBuffer[i].phase <= 0) {
            return i;
        }
    }
    // Allocate new slot
    for (int i = 0; i < MAX_TOUCHES; i++) {
        if (m_BackBuffer[i].phase == 1 || m_BackBuffer[i].pointerId == -1) {
            return i;
        }
    }
    return -1;
}

void Touch::ReleaseSlot(uint32_t pointerId) {
    for (int i = 0; i < MAX_TOUCHES; i++) {
        if (m_BackBuffer[i].pointerId == (int)pointerId) {
            m_BackBuffer[i].phase = 1;
            return;
        }
    }
}

void Touch::Update(float dt) {
    (void)dt;

    // Drain ring buffer → update back buffer
    while (m_EventCount > 0) {
        TEvnt& evt = m_EventRing[m_EventReadIdx];

        if (evt.isDown) {
            int slot = FindOrAllocSlot(evt.pointerId);
            if (slot >= 0) {
                TouchState& s = m_BackBuffer[slot];
                if (s.phase == 1 || s.pointerId == -1) {
                    // New touch
                    s.startX = evt.x;
                    s.startY = evt.y;
                    s.phase = -1; // just pressed
                    s.touchId = m_NextTouchId++;
                }
                s.currentX = evt.x;
                s.currentY = evt.y;
                s.pointerId = (int)evt.pointerId;
            }
        } else {
            ReleaseSlot(evt.pointerId);
        }

        m_EventReadIdx = (m_EventReadIdx + 1) % EVENT_RING_SIZE;
        m_EventCount--;
    }

    // Advance justPressed → held
    for (int i = 0; i < MAX_TOUCHES; i++) {
        if (m_BackBuffer[i].phase == -1) {
            m_BackBuffer[i].phase = 0; // held
        }
    }

    // Swap: copy back buffer to front buffer
    memcpy(m_FrontBuffer, m_BackBuffer, sizeof(m_FrontBuffer));
}

} // namespace Mortar
