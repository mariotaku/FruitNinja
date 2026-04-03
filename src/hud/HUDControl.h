#ifndef FN_HUD_CONTROL_H
#define FN_HUD_CONTROL_H

#include "Vec3.h"
#include "Colour.h"
#include <functional>

struct Renderer;

// Matches original HUDControl base class (~0x60 bytes)
// Virtual methods replace the original vtable
class HUDControl {
public:
    // +0x08
    Vec3 pos;
    // +0x20 (half-extents)
    Vec3 size;
    // +0x2c
    float m_Timer;         // rotation timer or animation state
    // +0x30
    bool m_bActive;        // visible + receives updates
    // +0x32
    bool m_bNoDestructor;  // if true, HUD won't call dtor on removal
    // +0x33
    bool m_bPendingRemoval;
    // +0x34
    int m_LayerMask;       // bit mask for layered drawing
    // +0x38
    std::function<void(HUDControl*)> m_RemoveCallback;
    // +0x5c
    Colour m_DrawColour;
    // +0x5f
    uint8_t m_Alpha;

    HUDControl()
        : m_Timer(0), m_bActive(true), m_bNoDestructor(false),
          m_bPendingRemoval(false), m_LayerMask(1),
          m_DrawColour(255, 255, 255, 255), m_Alpha(255) {}

    virtual ~HUDControl() {}

    // vtable +0x14: Update
    virtual void Update(float dt) { (void)dt; }
    // vtable +0x20: PreDraw
    virtual void PreDraw(Renderer& r, const Vec3& hudScale) { (void)r; (void)hudScale; }
    // vtable +0x24: Draw
    virtual void Draw(Renderer& r, const Vec3& hudScale, int layerMask) {
        (void)r; (void)hudScale; (void)layerMask;
    }
    // vtable +0x28: BeginDraw (called once per frame before Draw loop)
    virtual void BeginDraw(float dt) { (void)dt; }
    // vtable +0x40: Init (called after construction)
    virtual void Init() {}

    void SetPendingRemoval() { m_bPendingRemoval = true; }
};

#endif
