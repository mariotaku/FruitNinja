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
        }
    };
    static FN_TestSilencer s_TestSilencer;
}

#endif // !__bada__
