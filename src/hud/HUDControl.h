#ifndef FN_HUD_CONTROL_H
#define FN_HUD_CONTROL_H

//
// HUDControl — base class for all HUD elements
// Verified from Ghidra: ctor at 0x144104, size = 0x60
// See docs/structs/hud.md for full layout and vtable.
//

#include "math/Vec3.h"
#include "math/Colour.h"
#include <functional>
#include <cstdint>

struct Renderer;

class HUDControl {
public:
    // +0x04
    int field_0x04;

    // +0x08: position in centered coords
    Vec3 pos;

    // +0x14: pivot point (rarely used)
    Vec3 pivot;

    // +0x20: size (half-extents)
    Vec3 size;

    // +0x2c: rotation angle / animation timer
    float m_Timer;

    // +0x30: non-zero = visible + receives updates
    uint8_t m_bActive;

    // +0x31
    uint8_t field_0x31;

    // +0x32: if true, HUD won't call destructor on removal
    uint8_t m_bNoDestructor;

    // +0x33: set to true → removed next HUD::Update
    uint8_t m_bPendingRemoval;

    // +0x34: bit mask for layered drawing (default = 1)
    int m_LayerFlags;

    // +0x38: callback fired before removal (port: std::function, original: Delegate1 24 bytes)
    std::function<void(HUDControl*)> m_RemoveCallback;

    // +0x5c: tint colour (BGRA, default white)
    Colour m_DrawColour;

    HUDControl()
        : field_0x04(0),
          m_Timer(0.0f),
          m_bActive(1),
          field_0x31(0),
          m_bNoDestructor(0),
          m_bPendingRemoval(0),
          m_LayerFlags(1),
          m_DrawColour(255, 255, 255, 255) {}

    virtual ~HUDControl() {}

    // Vtable matches docs/structs/hud.md:
    // +0x00/+0x04: dtors (handled by C++ vtable)
    // +0x08: Init
    // +0x0c: Release
    // +0x10: Reset
    // +0x14: BeginDraw
    // +0x18: PreDraw
    // +0x1c: Draw
    // +0x20: PreDrawOrder (wrapper → PreDraw)
    // +0x24: DrawOrder (wrapper → Draw)
    // +0x28: Update
    // +0x2c: SetToMultiplayerState
    // +0x30: GetType
    // +0x34: Skip
    // +0x38: Save

    virtual void Init() {}
    virtual void Release() {}
    virtual void Reset() {}
    virtual void BeginDraw(float dt) { (void)dt; }
    virtual void PreDraw(const Vec3& hudScale) { (void)hudScale; }
    virtual void Draw(const Vec3& hudScale, int layerMask) { (void)hudScale; (void)layerMask; }
    virtual void PreDrawOrder(const Vec3& hudScale, int layerMask) { PreDraw(hudScale); (void)layerMask; }
    virtual void DrawOrder(const Vec3& hudScale, int layerMask) { Draw(hudScale, layerMask); }
    virtual void Update(float dt) { (void)dt; }
    virtual void SetToMultiplayerState() {}
    virtual int GetType() { return 0; }
    virtual void Skip() {}
    virtual void Save() {}

    void SetPendingRemoval() { m_bPendingRemoval = 1; }
};

#endif
