// PS3 entry point (PSL1GHT).
// Scaffolding: pad init + fixed-step placeholder loop.
// RSX / display / audio / full game glue still TODO.

#ifdef FRUIT_PLATFORM_PS3

#include "platform/ps3/InputTranslatorPS3.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/process.h>
#include <sysutil/sysutil.h>

// PSL1GHT pad already pulled by the translator; keep a local instance here
// until the real game loop owns it.
static InputTranslatorPS3 g_input;

static void sysutil_callback(u64 status, u64 param, void* userdata)
{
    (void)param;
    (void)userdata;
    if (status == SYSUTIL_EXIT_GAME) {
        // Clean shutdown path when the user quits from XMB.
        g_input.ReleaseAllFingers();
        sysProcessExit(0);
    }
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    printf("Fruit Ninja PS3 -- input scaffold\n");

    sysUtilRegisterCallback(0, sysutil_callback, NULL);

    g_input.Init();
    // Motion mode ON by default (same spirit as desktop / Magic Remote).
    g_input.SetMotionMode(true);

    // Placeholder loop: poll pad, drain touch, sleep ~16 ms.
    // Replace with FixedStepDriver + RSX present once display is wired.
    for (int frame = 0; frame < 60 * 5; ++frame) {  // ~5 s then exit for now
        sysUtilCheckCallback();

        g_input.Poll();
        g_input.DispatchForSimTick();

        // TODO: game step + RSX flip
        usleep(16000);
    }

    g_input.ReleaseAllFingers();
    printf("Fruit Ninja PS3 -- scaffold exit\n");
    return 0;
}

#else
// Should never be compiled without FRUIT_PLATFORM_PS3.
int main() { return 1; }
#endif
