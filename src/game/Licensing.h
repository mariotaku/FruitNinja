#ifndef FN_GAME_LICENSING_H
#define FN_GAME_LICENSING_H

// Defunct: DRM -- IsLicensed() always returns true; v1.6.1 binary @ 0x1ca830 literally `return 1`.
// The binary's DRM check is a single unconditional return-1 in the shipping build.
// Game::SetAppLicensed / GetAppLicensedState back a state int in game_work
// (binary @ 0x11fc7c / 0x11fcbc via g_GameData+0x18C).

// Defunct: DRM -- no-op stub; v1.6.1 binary @ 0x1ca830 (IsLicensed always true)
bool IsLicensed();

#endif // FN_GAME_LICENSING_H
