#include "screens/UpsellScreen.h"

// Defunct: UpsellScreen monetization -- stub; v1.6.1 UpsellScreen::UpsellScreen @0x001c443c.
UpsellScreen::UpsellScreen(Mortar::Delegate0<void> onDone, int mode)
    : field1_0x7c(0)
    , field2_0x80(0)
    , m_OnDismiss_delegate(onDone)
    , field4_0xa8(0.0f)
    , field269_0x1b4(0)
    , field270_0x1b8(0)
    , field271_0x1bc_mode(mode)
    , field272_0x1c0(0.9f)
    , field273_0x1c4(2.4f)
    , field274_0x1c8(1.0f)
    , field275_0x1cc(180.0f)
    , field276_0x1d0(11.0f)
    , field277_0x1d4(0.0f)
    , field278_0x1d8(-1.0f)
    , field279_0x1dc(-1.0f)
    , field280_0x1e0(0)
    , field281_0x1e4(0)
    , field282_0x1e8(0)
{
}

// Defunct: UpsellScreen monetization -- no-op stub; v1.6.1 UpsellScreen::MakeMainUpsellScreen @ 0x001c7870
UpsellScreen* UpsellScreen::MakeMainUpsellScreen(Mortar::Delegate0<void> onDone) {
    return new UpsellScreen(onDone, 0);
}

// Defunct: UpsellScreen monetization -- no-op stub; v1.6.1 UpsellScreen::MakeModeUpsellScreen @ 0x001c7168
UpsellScreen* UpsellScreen::MakeModeUpsellScreen(Mortar::Delegate0<void> onDone, int mode) {
    return new UpsellScreen(onDone, mode);
}
