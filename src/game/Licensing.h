#ifndef FN_GAME_LICENSING_H
#define FN_GAME_LICENSING_H

// Defunct: DRM -- IsLicensed() always returns true; v1.6.1 binary @ 0x1ca830 literally `return 1`.
// The binary's DRM check is a single unconditional return-1 in the shipping build.
// Game::SetAppLicensed / GetAppLicensedState back a state int in game_work
// (binary @ 0x11fc7c / 0x11fcbc via g_GameData+0x18C).

// Defunct: upsell store handoff -- UPSELL_PLACES enum; v1.6.1 GotoFruitNinjaPage @0x001cbd5c
// Binary calls GotoFruitNinjaPage(1, -1) from GameModeScreen::BuyNow.
// TODO: v1.6.1 (GameModeScreen::BuyNow) -- the previously-cited @0x0013e10c is WRONG
// for v1.6.1 (decompiles to PSPParticleManager::LoadFile); re-verify BuyNow's real
// v1.6.1 address. Same stale address also appears in GameModeScreen.h/.cpp.
enum UPSELL_PLACES {
    UPSELL_PLACE_0 = 0,
    UPSELL_PLACE_1 = 1,
    UPSELL_PLACE_2 = 2,
    // v1.6.1 TwitterPressed @0x00169f70 / FacebookPressed @0x00169f40: the 2nd
    // GotoFruitNinjaPage arg indexes GotoFruitNinjaPage's local saveTotalWords[]/
    // leaderboards[] tables, not a small "upsell place" -- values RE-confirmed
    // via decompile of the two call sites.
    UPSELL_PLACE_TWITTER = 0xc,
    UPSELL_PLACE_FACEBOOK = 0xd
};

// Defunct: DRM -- no-op stub; v1.6.1 IsLicensed @0x001ca830 (always true)
bool IsLicensed();

// Defunct: IAP -- no-op stubs
void BadaPurchaseApp();
void BuyAOZ();
void BuyMonsterDash();
// v1.6.1 GotoFruitNinjaPage @0x001cbd5c signature: void __cdecl(float param_1, int param_2) --
// param_1 = timeout-extension seconds (gameLinkTimeOut), param_2 = place/table index.
// (float, place) order confirmed against the binary's own decompiled prototype.
void GotoFruitNinjaPage(float param, UPSELL_PLACES place);
// Leaf browser launcher. v1.6.1 OpenBrowser @0x001eee64 builds "url:<url>" and starts
// the Bada APPCONTROL_BROWSER app-control. SDL implementation lives in LicensingSDL.cpp
// (this header stays portable).
void OpenBrowser(const char* url);
void DownloadUDC(const char* url, void* buf, int size);
void RemindLaterCallback();

#endif // FN_GAME_LICENSING_H
