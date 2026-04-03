#ifndef FN_HUD_CONTROL_H
#define FN_HUD_CONTROL_H

//
// HUDControl — base class for all HUD elements
// Verified from Ghidra: ctor at 0x144104, size = 0x60
//
// Field layout:
//   +0x00  vtable*
//   +0x04  int field_0x04 = 0
//   +0x08  Vec3 pivot (zeroed from global)
//   +0x14  Vec3 pos (zeroed from global)
//   +0x20  Vec3 size (from global)
//   +0x2c  float m_Timer = 0.0 (rotation/animation)
//   +0x30  byte m_bActive = 1
//   +0x31  byte field_0x31 = 0
//   +0x32  byte m_bNoDestructor = 0 (if set, HUD won't call dtor)
//   +0x33  byte m_bPendingRemoval = 0
//   +0x34  int m_LayerFlags = 1 (bit mask for layered drawing)
//   +0x38  Delegate1<void,HUDControl*> m_RemoveCallback (24 bytes)
//   +0x50  (delegate storage)
//   +0x5c  Colour m_DrawColour (BGRA, from global = white)
//

#include "Vec3.h"
#include "Colour.h"
#include <functional>
#include <cstdint>

struct Renderer;

class HUDControl {
public:
    // +0x08, +0x14 (pivot is rarely used, merged with pos for port)
    Vec3 pos;

    // +0x20
    Vec3 size;

    // +0x2c: rotation angle / animation timer
    float m_Timer;

    // +0x30: non-zero = visible + receives updates
    bool m_bActive;

    // +0x32: if true, HUD won't call destructor on removal
    bool m_bNoDestructor;

    // +0x33: set to true → removed next HUD::Update
    bool m_bPendingRemoval;

    // +0x34: bit mask for layered drawing (default = 1)
    int m_LayerFlags;

    // +0x38: callback fired before removal (port: std::function)
    std::function<void(HUDControl*)> m_RemoveCallback;

    // +0x5c: tint colour (BGRA, default white)
    Colour m_DrawColour;

    // +0x5f conceptually (alpha byte of m_DrawColour in original)
    // For port convenience, separate alpha field
    uint8_t m_Alpha;

    // +0x60 in HUDControl3d: controls whether PreDraw uses global or HUD scale
    // (checked as byte at offset 0x60 in HUD::Draw). For base HUDControl = 0.
    // HUDControl3d sets this to the texture SmartPtr (non-zero when valid).

    HUDControl()
        : m_Timer(0.0f),
          m_bActive(true),          // +0x30 = 1
          m_bNoDestructor(false),   // +0x32 = 0
          m_bPendingRemoval(false), // +0x33 = 0
          m_LayerFlags(1),          // +0x34 = 1
          m_DrawColour(255, 255, 255, 255),  // white (from global)
          m_Alpha(255) {}

    virtual ~HUDControl() {}

    // vtable +0x08: Init (called after construction)
    virtual void Init() {}

    // vtable +0x0c: OnPause
    virtual void OnPause() {}

    // vtable +0x10: Reset
    virtual void Reset() {}

    // vtable +0x14: Update
    virtual void Update(float dt) { (void)dt; }

    // vtable +0x18: Save
    virtual void Save() {}

    // vtable +0x1c: BeginDraw (called once per frame before Draw loop)
    virtual void BeginDraw(float dt) { (void)dt; }

    // vtable +0x20: PreDraw
    virtual void PreDraw(Renderer& r, const Vec3& hudScale) {
        (void)r; (void)hudScale;
    }

    // vtable +0x24: Draw
    virtual void Draw(Renderer& r, const Vec3& hudScale, int layerMask) {
        (void)r; (void)hudScale; (void)layerMask;
    }

    void SetPendingRemoval() { m_bPendingRemoval = true; }

    // Touch input (port extension — original routes through InputManager)
    virtual void OnTouchDown(float x, float y) { (void)x; (void)y; }
    virtual void OnTouchUp(float x, float y) { (void)x; (void)y; }
    virtual void OnTouchMove(float x, float y) { (void)x; (void)y; }
};

#endif
