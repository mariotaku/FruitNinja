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
    enum NotifType : uint8_t { Type_Numeric = 1, Type_Named = 2 };

    // Binary @ 0x00152ed0
    NotificationControl(const char* name, int points,
                        Mortar::SmartPtr<Mortar::Texture>* icon, uint8_t type);
    ~NotificationControl() override;

    // Binary @ 0x00152a00
    void Update(float dt) override;

    // Binary @ 0x001531f8
    void Draw(const Vec3& hudScale, int layerMask) override;

    // +0x74: achievement icon texture (overlaps HUDControl3d::m_SecondaryTex slot in binary).
    // Port: stored as a SmartPtr here alongside the GLuint m_Texture in super.
    // m_Texture (super +0x74 GLuint) is unused for this control; icon drawn separately.
    Mortar::SmartPtr<Mortar::Texture> m_AchIcon;   // effectively at +0x7C in port (sizeof SmartPtr = 8)

    float   m_TextScale;                   // +0x7C in binary (port offset differs)
    float   m_StateTimer;                  // +0x80
    int     m_Points;                      // +0x84
    char    m_DisplayName[128];            // +0x88
    char    m_PointsText[4];              // +0x108
    uint8_t m_NotifType;                   // +0x10C
    uint8_t _pad[3];                       // +0x10D..+0x10F

public:

public:

public:

public:

public:
};

#endif // FN_HUD_NOTIFICATION_CONTROL_H
