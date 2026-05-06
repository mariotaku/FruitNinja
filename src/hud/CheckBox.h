#ifndef FN_HUD_CHECKBOX_H
#define FN_HUD_CHECKBOX_H

//
// CheckBox : HUDControl3d (sizeof 0x88 on ARM32)
// Binary @ 0x00134CE0 (char* ctor), 0x00134D98 (LocalizedString ctor),
//          0x00134E70 (Draw), 0x00134B28 (Update), 0x00134AEC (UpdateTouchPosition)
//          0x00135010 (LoadContent), 0x0013508C (UnloadContent)
//
// Not in namespace Mortar — symbol-diff confirms bare CheckBox:: prefix.
//

#include "HUDControl3d.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include <cstdint>

// LocalizedString — port stub for the binary's BakedString-based localised
// string type used by the CheckBox(Vec3, Vec3, LocalizedString) overload.
// Full RE is not yet complete; this minimal wrapper lets the overload compile.
// Binary @ 0x00134D98 (LocalizedString ctor delegates to char* ctor with
// possible binary-side bug — see TODO below).
struct LocalizedString {
    const char* str;
    explicit LocalizedString(const char* s) : str(s) {}
    operator const char*() const { return str; }
};

class CheckBox : public HUDControl3d {
public:
    // Binary @ 0x00134CE0
    CheckBox(Vec3 pos, Vec3 size, const char* label);

    // Binary @ 0x00134D98
    // ASM-verified pending: 0x00134d98 -- m_pLabel offset/type. asm-verify clean as of R4 W4.
    CheckBox(Vec3 pos, Vec3 size, LocalizedString loc);

    virtual ~CheckBox();

    // Vtable overrides — match HUDControl vtable slot order.
    virtual void   Init()    override;                              // Binary @ 0x00134AE4 (no-op)
    virtual void   Release() override;                              // Binary @ 0x00134AE8 (no-op)
    virtual void   PreDraw(const Vec3& hudScale) override;          // Binary @ 0x00134B20
    virtual void   Update(float dt) override;                       // Binary @ 0x00134B28
    virtual void   Draw(const Vec3& hudScale, int layerMask) override;  // Binary @ 0x00134E70
    virtual int    GetType() override { return 5; }                  // Binary @ 0x001354D8

    // Non-virtual helpers
    void UpdateTouchPosition();   // Binary @ 0x00134AEC
    void UpdateFromGameWork();    // Binary @ 0x00134B24 — empty in binary too

    // Statics
    static void LoadContent();    // Binary @ 0x00135010
    static void UnloadContent();  // Binary @ 0x0013508C


private:
    uint8_t     m_bChecked;   // +0x7C (default 1 = CHECKED)
    uint8_t     _pad7D;       // +0x7D
    uint8_t     _pad7E;       // +0x7E
    uint8_t     _pad7F;       // +0x7F
    const char* m_pLabel;     // +0x80
    int         m_TouchSlot;  // +0x84 (-1 = none)
    float       m_TouchX;     // +0x88
    float       m_TouchY;     // +0x8C
    float       m_TouchPhase; // +0x90

    static Mortar::SmartPtr<Mortar::Texture> s_checked;
    static Mortar::SmartPtr<Mortar::Texture> s_unchecked;
};

#endif // FN_HUD_CHECKBOX_H
