#ifndef FN_HUD_NOTIFICATION_CONTROL_H
#define FN_HUD_NOTIFICATION_CONTROL_H

// Analysed: 2026-05-03T00:00
// NotificationControl — HUD popup for achievement unlock notifications.
// Binary @ 0x001a4428 (ctor) / 0x00152a00 (Update) / 0x001531f8 (Draw).
// sizeof 0x114 = 276 bytes. operator new(0x114) @ 0x00118104.

#include "HUDControl3d.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include "engine/render/BakedStringBox.h"
#include <cstdint>

struct AchievementInfo;

// Binary @ 0x001a4428 (ctor) / 0x00152a00 (Update) / 0x001531f8 (Draw). sizeof=0x114.
class NotificationControl : public HUDControl3d {
public:
    // Binary enum name: NotificationType (not NotifType).
    enum NotificationType { Type_Numeric = 1, Type_Named = 2 };

    // Binary @ 0x00152ed0 — takes SmartPtr by value and NotificationType enum.
    NotificationControl(const char* name, int points,
                        Mortar::SmartPtr<Mortar::Texture> icon,
                        NotificationType type);
    ~NotificationControl() override;

    // Binary @ 0x00152a00
    void Update(float dt) override;

    // Binary @ 0x001531f8
    void Draw(const Vec3& hudScale, int layerMask) override;

    // Binary ctor stores the icon SmartPtr into the base HUDControl3d::m_Texture slot (+0x74).
    // No subclass texture member; use m_Texture (inherited) for the icon.

    float   m_TextScale;     // +0x7C
    float   m_StateTimer;    // +0x80
    int     m_Points;        // +0x84
    char    m_DisplayName[128]; // +0x88
    char    m_PointsText[4]; // +0x108
    uint8_t m_NotifType;     // +0x10C
    uint8_t _pad[3];         // +0x10D..+0x10F
    // +0x110: owned BakedStringBox* (ctor operator-new(0x18), ctor @0x001a3c60, init @0x0011551c;
    //   str r7,[r4,#0x110] @0x001a4640). Ghidra name: m_pBakedString.
    // TODO: v1.6.1 0x001a4428 (NotificationControl::NotificationControl) — confirm +0x110 sub-object type (size 0x18 doesn't match BakedStringBox(0xc8)); may be a different BakedString variant.
    Mortar::BakedStringBox* m_pBakedString;  // +0x110

    // ---- HUDControl3d lifecycle overrides ----
    // Distinct NotificationControl vtable @ 0x1e9b80 (object vptr). Slots Init/Release
    // are real per-class thunks; Reset/PreDraw are no-op leaf overrides.
    // Binary @ 0x001529EC -- Init() delegates to virtual Reset().
    void Init() override;
    // Binary @ 0x00152DE0 -- Release() nulls the inherited m_Texture SmartPtr.
    void Release() override;
    // Binary @ 0x001529F8 -- no-op.
    void Reset() override;
    // Binary @ 0x001529FC -- no-op.
    void PreDraw(float* viewVec);
    // ---- end lifecycle overrides ----
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(NotificationControl) == 276, "NotificationControl size mismatch");
#endif

#endif // FN_HUD_NOTIFICATION_CONTROL_H
