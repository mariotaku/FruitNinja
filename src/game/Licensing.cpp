// Defunct: IAP, upsell, licensing, DRM -- no-op stubs.
// Bada in-app purchase and store handoff functions; permanently defunct in port.

#include "Licensing.h"

// Defunct: DRM -- no-op stub; v1.6.1 IsLicensed @0x001ca830 (binary literally `return 1`)
bool IsLicensed() {
    return true;
}

// Defunct: IAP -- no-op stub; v1.6.1 BadaPurchaseApp @0x001ca7f0
void BadaPurchaseApp() {
}

// Defunct: IAP -- no-op stub; v1.6.1 BuyAOZ @0x001ca854
void BuyAOZ() {
}

// Defunct: IAP -- no-op stub; v1.6.1 BuyMonsterDash @0x001ca870
void BuyMonsterDash() {
}

// Defunct: upsell -- no-op stub; v1.6.1 GotoFruitNinjaPage @0x001ce2b0
// Binary: calls GotoFruitNinjaPage(1,-1) from GameModeScreen::BuyNow @ 0x0013e10c
void GotoFruitNinjaPage(UPSELL_PLACES /*place*/, float /*param*/) {
}

// Defunct: upsell -- no-op stub; v1.6.1 OpenBrowser @0x001ca890
void OpenBrowser(const char* /*url*/) {
}

// Defunct: upsell -- no-op stub; v1.6.1 DownloadUDC @0x001ca8a8
void DownloadUDC(const char* /*url*/, void* /*buf*/, int /*size*/) {
}

// Defunct: upsell -- no-op stub; v1.6.1 RemindLaterCallback @0x001ca8c8
void RemindLaterCallback() {
}
