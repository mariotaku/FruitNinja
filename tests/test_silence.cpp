#ifndef __bada__

#include <SDL.h>
#include <cstdio>
#include <cstdlib>

static void FN_TestLogSink(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    (void)userdata;
    (void)category;
    if (priority >= SDL_LOG_PRIORITY_ERROR) {
        fprintf(stderr, "%s\n", message);
    }
}

namespace {
    struct FN_TestSilencer {
        FN_TestSilencer() {
            if (getenv("FN_TEST_VERBOSE") == NULL) {
                SDL_LogSetOutputFunction(&FN_TestLogSink, NULL);
            }
            // Route audio to the dummy backend so tests never play real sound.
            // Runs before main() -> before SDL_Init, so SDL picks it up.
            // FN_TEST_AUDIO=1 restores the real audio device.
            if (getenv("FN_TEST_AUDIO") == NULL) {
                SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
            }
        }
    };
    static FN_TestSilencer s_TestSilencer;
}

#endif // !__bada__
