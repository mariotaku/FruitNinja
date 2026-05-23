#include "screens/UpsellScreen.h"

// Defunct: UpsellScreen monetization -- no-op stub; binary @ 0x00166d20
UpsellScreen* UpsellScreen::MakeMainUpsellScreen(Mortar::Delegate0<void> onDone) {
    return new UpsellScreen(onDone, 0);
}

// Defunct: UpsellScreen monetization -- no-op stub; binary @ 0x00166708
UpsellScreen* UpsellScreen::MakeModeUpsellScreen(Mortar::Delegate0<void> onDone, int mode) {
    return new UpsellScreen(onDone, mode);
}
