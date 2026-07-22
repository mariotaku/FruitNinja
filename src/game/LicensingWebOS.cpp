#ifndef __bada__

#include "Licensing.h"
#include <dlfcn.h>

// Port specific (webOS): original = Bada APPCONTROL_BROWSER via OpenBrowser
// @0x001eee64 -- opens the social-network URL (see Social.h). We must NOT hard-
// call SDL_OpenURL: old webOS TVs' system SDL2 doesn't export it (webosbrew-elf-
// verify flagged it as an unresolved import that would fail to load there).
// Instead resolve it at RUNTIME via dlsym -- this keeps the symbol out of the
// import table (verify stays clean across all firmwares), opens the browser on
// modern webOS where SDL-webOS provides SDL_OpenURL (which itself performs the
// luna://com.webos.applicationManager browser app-control), and degrades to a
// no-op on ancient TVs that lack it.
// TODO: for guaranteed browser launch on the oldest TVs, call the luna app-
// control directly (needs libhelpers/luna-service2 + a mainloop -- deferred as
// heavyweight for a single social link).
void OpenBrowser(const char* url) {
    if (url == 0 || url[0] == '\0') {
        return;
    }
    typedef void (*OpenUrlFn)(const char*);
    OpenUrlFn fn = (OpenUrlFn)dlsym(RTLD_DEFAULT, "SDL_OpenURL");
    if (fn) {
        fn(url);
    }
}

#endif // !__bada__
