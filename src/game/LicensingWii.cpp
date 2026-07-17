#ifndef __bada__

#include "Licensing.h"

// Defunct: DRM/browser handoff -- no-op stub; v1.6.1 OpenBrowser @0x001eee64.
// Wii has no browser app-control to hand off to (unlike SDL's SDL_OpenURL --
// see LicensingSDL.cpp); the upsell/browser links this backs are already
// dead features on this platform, so the call is a silent no-op.
void OpenBrowser(const char* url) {
    (void)url;
}

#endif // !__bada__
