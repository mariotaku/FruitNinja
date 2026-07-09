#ifndef __bada__

#include "Licensing.h"
#include <SDL.h>

// Port specific: original = Bada APPCONTROL_BROWSER via OpenBrowser @0x001eee64
// (builds "url:<url>", starts the browser app-control). SDL_OpenURL is the
// desktop/mobile equivalent app-control launch; SDL_OpenURL expects a real URL
// so the Bada "url:" prefix is dropped here rather than passed through.
void OpenBrowser(const char* url) {
    if (url == 0 || url[0] == '\0') {
        return;
    }
    SDL_OpenURL(url);
}

#endif // !__bada__
