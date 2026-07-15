// Defunct: IAP, upsell, licensing, DRM -- no-op stubs.
// Bada in-app purchase and store handoff functions; permanently defunct in port.

#include "Licensing.h"

// Defunct: DRM -- no-op stub; v1.6.1 IsLicensed @0x001ca830 (binary literally `return 1`)
bool IsLicensed() {
    return true;
}

// Defunct: IAP -- no-op stub; v1.6.1 BadaPurchaseApp @0x001eebe4
void BadaPurchaseApp() {
}

// Defunct: IAP -- no-op stub; v1.6.1 BuyAOZ @0x0017a288
void BuyAOZ() {
}

// Defunct: IAP -- no-op stub; v1.6.1 BuyMonsterDash @0x0017a27c
void BuyMonsterDash() {
}

// Defunct: upsell/achievement bookkeeping -- no-op stub; v1.6.1 GotoFruitNinjaPage @0x001cbd5c
// Binary: calls GotoFruitNinjaPage(1,-1) from GameModeScreen::BuyNow (address TODO, see Licensing.h);
// GotoFruitNinjaPage(0xd, -1.0) from FacebookPressed @0x00169f40;
// GotoFruitNinjaPage(0xc, -1.0) from TwitterPressed @0x00169f70.
void GotoFruitNinjaPage(UPSELL_PLACES /*place*/, float /*timeoutExt*/) {
}

// OpenBrowser is implemented in LicensingSDL.cpp (needs SDL_OpenURL; this TU stays portable).

// Defunct: upsell -- no-op stub; v1.6.1 DownloadUDC @0x00195d60
void DownloadUDC(const char* /*url*/, void* /*buf*/, int /*size*/) {
}

// Defunct: upsell -- no-op stub; v1.6.1 RemindLaterCallback @0x00184fb4
void RemindLaterCallback() {
}
