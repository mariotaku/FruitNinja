#ifndef FN_GAME_LICENSING_H
#define FN_GAME_LICENSING_H

// Defunct: DRM -- IsLicensed() always returns true; v1.6.1 binary @ 0x1ca830 literally `return 1`.
// The binary's DRM check is a single unconditional return-1 in the shipping build.
// Game::SetAppLicensed / GetAppLicensedState back a state int in game_work
// (binary @ 0x11fc7c / 0x11fcbc via g_GameData+0x18C).

// Defunct: upsell store handoff -- UPSELL_PLACES enum; v1.6.1 GotoFruitNinjaPage @0x001ce2b0
// Binary calls GotoFruitNinjaPage(1, -1) from GameModeScreen::BuyNow @ 0x0013e10c.
enum UPSELL_PLACES {
    UPSELL_PLACE_0 = 0,
    UPSELL_PLACE_1 = 1,
    UPSELL_PLACE_2 = 2
};

// Defunct: DRM -- no-op stub; v1.6.1 IsLicensed @0x001ca830 (always true)
bool IsLicensed();

// Defunct: IAP -- no-op stubs
void BadaPurchaseApp();
void BuyAOZ();
void BuyMonsterDash();
void GotoFruitNinjaPage(UPSELL_PLACES place, float param);
void OpenBrowser(const char* url);
void DownloadUDC(const char* url, void* buf, int size);
void RemindLaterCallback();

#endif // FN_GAME_LICENSING_H
