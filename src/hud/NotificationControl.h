#ifndef FN_HUD_NOTIFICATION_CONTROL_H
#define FN_HUD_NOTIFICATION_CONTROL_H

// NotificationControl — HUD popup for achievement unlock notifications.
// v1.6.1 NotificationControl::{ctor} @ 0x001a4428 / Update @ 0x001a3c7c / Draw @ 0x001a4860.
// sizeof 0x114 = 276 bytes. operator new(0x114) @ 0x00118104.

#include "HUDControl3d.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include "engine/render/BakedStringBox.h"
#include <cstdint>

struct AchievementInfo;

// v1.6.1 NotificationControl::{ctor} @ 0x001a4428 / Update @ 0x001a3c7c / Draw @ 0x001a4860. sizeof=0x114.
class NotificationControl : public HUDControl3d {
public:
    // Binary enum name: NotificationType (not NotifType).
    enum NotificationType { Type_Numeric = 1, Type_Named = 2 };

    // v1.6.1 NotificationControl::s_banner / s_unlockBanner (mangled
    // _ZN19NotificationControl8s_bannerE / _ZN19NotificationControl14s_unlockBannerE).
    // Loaded once by AchievementManager::LoadAchievementInfo @0x00118198; Draw()
    // reads these via IsValid()/Get() to gate the banner quad.
    static Mortar::SmartPtr<Mortar::Texture> s_banner;       // "achievment_banner.tex" (sic)
    static Mortar::SmartPtr<Mortar::Texture> s_unlockBanner; // "hud_unlocked_dialog.tex"

    // v1.6.1 NotificationControl::NotificationControl @0x001a4428 — takes SmartPtr by value and NotificationType enum.
    NotificationControl(const char* name, int points,
                        Mortar::SmartPtr<Mortar::Texture> icon,
                        NotificationType type);
    ~NotificationControl() override;

    // v1.6.1 NotificationControl::Update @0x001a3c7c
    // Animates the slide-in/settle/slide-out state machine. Type_Named also
    // spawns three fire-and-forget "confettif" PSPParticleManager emitters
    // (at t crossing 0.125/0.25/0.375s) and burns one extra g_Random draw per
    // spawn tick -- both affect the global RNG stream.
    void Update(float dt) override;

    // v1.6.1 NotificationControl::Draw @0x001a4860
    void Draw(float* hudScaleRaw) override;

    // Binary ctor stores the icon SmartPtr into the base HUDControl3d::m_Texture slot (+0x74).
    // No subclass texture member; use m_Texture (inherited) for the icon.

    float   m_TextScale;     // +0x7C
    float   m_StateTimer;    // +0x80
    int     m_Points;        // +0x84
    char    m_DisplayName[128]; // +0x88
    char    m_PointsText[4]; // +0x108
    uint8_t m_NotifType;     // +0x10C
    uint8_t _pad[3];         // +0x10D..+0x10F
    // +0x110: owned BakedStringBox* — v1.6.1 NotificationControl::NotificationControl
    // @0x001a4178/0x001a4428: operator_new(0xc8)=200B matches BakedStringBox's own
    // sizeof(200) exactly (ctor @0x001a3c60 == BakedStringBox ctor thunk; init
    // @0x0011551c; str r7,[r4,#0x110] @0x001a4640). 0x18=24 in the ctor call is the
    // BakedStringBox `height` constructor argument, not an allocation size.
    // Owns the TTF-rendered name-text label drawn in Draw(); freed in ~NotificationControl().
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
    void PreDraw(float* viewVec) override;
    // ---- end lifecycle overrides ----
};

#if defined(__bada__)
static_assert(sizeof(NotificationControl) == 276, "NotificationControl size mismatch");
#endif

#endif // FN_HUD_NOTIFICATION_CONTROL_H
