#ifndef MORTAR_TOUCH_H
#define MORTAR_TOUCH_H

#include <cstdint>

namespace Mortar {

// Matches original Touch State (28 bytes)
struct TouchState {
    float startX, startY;     // +0x00: touch-down position
    float currentX, currentY; // +0x08: current position
    int pointerId;            // +0x10: OS slot index
    int touchId;              // +0x14: monotonic Mortar ID
    int phase;                // +0x18: -1=justPressed, 0=held, 1=released
};

// Matches original TEvnt (20 bytes)
struct TEvnt {
    uint32_t pointerId;
    uint32_t isDown;    // 1=pressed, 0=released
    float x, y;
    float timestamp;
};

// Matches original Mortar::Touch (468 bytes)
// Double-buffered 8-slot multitouch with ring buffer event queue
class Touch {
public:
    static const int MAX_TOUCHES = 8;
    static const int EVENT_RING_SIZE = 10;

    TouchState m_FrontBuffer[MAX_TOUCHES];  // +0x000: read by game
    TouchState m_BackBuffer[MAX_TOUCHES];   // +0x0E0: written by event handler
    TEvnt m_EventRing[EVENT_RING_SIZE];     // +0x1C0: ring buffer
    int m_EventReadIdx;
    int m_EventWriteIdx;
    int m_EventCount;
    int m_NextTouchId;                      // +0x1D0: monotonic counter

    Touch();

    // Called from SDL event handler (ISR context / main thread)
    // Pushes event to ring buffer
    // Matches __UpdateInternal (0x195690)
    void PushEvent(uint32_t pointerId, bool isDown, float x, float y, float timestamp);

    // Called each frame — drains ring buffer, updates back buffer, swaps to front
    // Matches Touch::Update (0x195570)
    void Update(float dt);

    // Read touch state from front buffer
    const TouchState& GetTouch(int index) const { return m_FrontBuffer[index]; }

    // Check if a touch slot is active
    bool IsTouchActive(int index) const {
        return m_FrontBuffer[index].phase >= -1 && m_FrontBuffer[index].phase <= 0;
    }

private:
    // Find back buffer slot for pointerId, or allocate new
    int FindOrAllocSlot(uint32_t pointerId);

    // Release slot in back buffer
    void ReleaseSlot(uint32_t pointerId);
};

} // namespace Mortar

#endif
