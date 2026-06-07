#ifndef FN_HUD_NOTIFICATION_CONTROL_H
#define FN_HUD_NOTIFICATION_CONTROL_H

// Analysed: 2026-05-03T00:00
// NotificationControl — HUD popup for achievement unlock notifications.
// Binary @ 0x00152ed0 (ctor) / 0x00152a00 (Update) / 0x001531f8 (Draw).
// sizeof 0x110 = 272 bytes.

#include "HUDControl3d.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include <cstdint>

struct AchievementInfo;

// Binary @ 0x00152ed0 (ctor) / 0x00152a00 (Update) / 0x001531f8 (Draw). sizeof=0x110.
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

    // ---- HUDControl3d lifecycle overrides ----
    // No NotificationControl-specific symbol in binary (only ctor @0x152ed0/0x153060,
    // dtors @0x152dec/152e2c/152e68); these vtable slots inherit base HUDControl3d behaviour.
    // TODO: 0x001529EC -- override body; resolve vs base HUDControl3d Init slot
    void Init() override;
    // TODO: 0x00152DE0 -- override body; resolve vs base HUDControl3d Release slot
    void Release() override;
    // TODO: 0x001529F8 -- override body; resolve vs base HUDControl3d Reset slot
    void Reset() override;
    // TODO: 0x001529FC -- override body; resolve vs base HUDControl3d PreDraw slot
    void PreDraw(float* viewVec);
    // ---- end lifecycle overrides ----
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(NotificationControl) == 272, "NotificationControl size mismatch");
#endif

#endif // FN_HUD_NOTIFICATION_CONTROL_H
