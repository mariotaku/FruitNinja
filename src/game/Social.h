#ifndef FN_GAME_SOCIAL_H
#define FN_GAME_SOCIAL_H

// Free-function social-sharing callbacks. v1.6.1 binds these via a FREE-function
// Delegate0<void> (MakeFree), not a bound member delegate -- see DojoScreen.cpp's
// BSButton SetCallback() call sites.
//
// FacebookPressed/TwitterPressed are the only two with a live leaf: they call the
// (still-defunct) GotoFruitNinjaPage() achievement-bookkeeping stub, then the real
// OpenBrowser() leaf (SDL_OpenURL under desktop, see LicensingSDL.cpp) so tapping
// the button actually opens the social page. The rest remain fully no-op stubs.

// ASM-spec v1.6.1 FacebookPressed @0x00169f40: GotoFruitNinjaPage(-1.0, 0xd) then
// OpenBrowser("http://www.facebook.com/halfbrick").
void FacebookPressed();

// ASM-spec v1.6.1 TwitterPressed @0x00169f70: GotoFruitNinjaPage(-1.0, 0xc) then
// OpenBrowser("http://www.twitter.com/halfbrick").
void TwitterPressed();

// Defunct: social sharing -- no-op stub; v1.6.1 RegisterSocialNetworks @0x0011efc0
void RegisterSocialNetworks();

// Defunct: social sharing -- no-op stub; v1.6.1 GetSocialNetworkLocale @0x001ca374
const char* GetSocialNetworkLocale();

// Defunct: social sharing -- no-op stub; v1.6.1 FindFriends @0x00175dcc
void FindFriends();

// Defunct: social sharing -- no-op stub; v1.6.1 GetProviderFruit @0x001ca8b4
const char* GetProviderFruit();

// Defunct: social sharing -- no-op stub; v1.6.1 GetProviderString @0x001ca894
const char* GetProviderString();

#endif // FN_GAME_SOCIAL_H
