// scene_slash.cpp -- standalone isolated slash/blade scene.
// Boots the engine (GameInitialise + GameInit), then renders ONLY the blade
// trail driven by real mouse/touch input on a plain cleared background.
// The menu, HUD, fruit, and WaveManager are NOT drawn -- the scene runs its
// own minimal per-frame loop.
//
// Port specific: standalone isolated entity scene
//
// Controls:
//   Mouse drag      -- draw blade trail (SDL synthesizes FINGER* from MOUSE*)
//   ESC             -- quit
//   R               -- reset the blade trail (SlashEntity::Reset)
//   F12             -- screenshot (BMP via GameSDL handler)
//   --screenshot    -- headless one-shot: inject synthetic swipe, dump PPM,
//                      exit. Output: tmp/test/screenshots/scene_slash.png
//   --headless      -- hidden window, no screenshot
//   (bare launch)   -- visible window, mouse drag drives the blade

#include "../test_harness.h"
#include "entities/SlashEntity.h"
#include "game/GameWork.h"
#include "game/GameTaskState.h"
#include "game/FruitCamera.h"
#include "render/DisplayManager.h"
#include "core/SystemManager.h"
#include "platform/InputTranslatorSDL.h"
#include "input/InputManager.h"
#include "render/gl_funcs.h"
#include "Game.h"
#include <cstdio>
#include <cstring>

struct SceneSlashData {
    bool resetRequested;
    int  frameCount;
};

// Inject a single SDL_FINGERDOWN event to start a touch.
// Port specific: standalone isolated entity scene -- headless swipe injection
static void InjectFingerDown(SDL_TouchID tid, float nx, float ny) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type             = SDL_FINGERDOWN;
    ev.tfinger.touchId  = tid;
    ev.tfinger.fingerId = 1;
    ev.tfinger.x        = nx;
    ev.tfinger.y        = ny;
    ev.tfinger.pressure = 1.0f;
    SDL_PushEvent(&ev);
}

// Inject a single SDL_FINGERMOTION event to extend the swipe.
// Port specific: standalone isolated entity scene -- headless swipe injection
static void InjectFingerMotion(SDL_TouchID tid, float nx, float ny, float dx, float dy) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type             = SDL_FINGERMOTION;
    ev.tfinger.touchId  = tid;
    ev.tfinger.fingerId = 1;
    ev.tfinger.x        = nx;
    ev.tfinger.y        = ny;
    ev.tfinger.dx       = dx;
    ev.tfinger.dy       = dy;
    ev.tfinger.pressure = 1.0f;
    SDL_PushEvent(&ev);
}

// Inject a single SDL_FINGERUP event to end a touch.
// Port specific: standalone isolated entity scene -- headless swipe injection
static void InjectFingerUp(SDL_TouchID tid, float nx, float ny) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type             = SDL_FINGERUP;
    ev.tfinger.touchId  = tid;
    ev.tfinger.fingerId = 1;
    ev.tfinger.x        = nx;
    ev.tfinger.y        = ny;
    ev.tfinger.pressure = 0.0f;
    SDL_PushEvent(&ev);
}

// Per-frame entity-only tick for the slash scene.
// Updates: dt from SystemManager, dispatches SDL events to InputTranslator
// (so finger events reach SlashEntity), updates slash entities, draws them.
// Returns false to quit.
static bool SceneFrameTick(SceneSlashData* d, Game& game, SDL_Window* window) {
    // Poll SDL events: drain into pending state (no dispatch per-frame).
    // Port specific: matches the #173 drain/dispatch split -- DrainSDLEvent
    // accumulates touch input, DispatchForSimTick dispatches once per sim tick.
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return false;
        if (ev.type == SDL_KEYDOWN) {
            if (ev.key.keysym.sym == SDLK_ESCAPE) return false;
            if (ev.key.keysym.sym == SDLK_r) d->resetRequested = true;
        } else {
            // Forward all other events (FINGER*, MOUSE*) to InputTranslator
            // so SlashEntity touch callbacks fire on the next DispatchForSimTick.
            if (game.inputTranslator) {
                game.inputTranslator->DrainSDLEvent(ev,
                    static_cast<SDL_Window*>(window));
            }
        }
    }
    // Dispatch accumulated touch state to InputManager for this sim tick (#173).
    if (game.inputTranslator) game.inputTranslator->DispatchForSimTick();

    // DispatchForSimTick only drains the SDL edges into Mortar::Touch's ring.
    // The action events that reach the blades are raised one layer up, by
    // InputManager::Update @0x00243838 -> InputDeviceBada::Update @0x00242f40
    // -> Touch::SendIndividualTouchCallbacks @0x00242bc4 -> the per-finger
    // callbacks in GameTaskInput.cpp. GameUpdate @0x001cf5f4 makes that call
    // once per frame; this scene hand-rolls its frame loop, so it must too.
    if (Mortar::InputManager::GetInstance()) {
        Mortar::InputManager::GetInstance()->Update(0.0f);
    }

    float dt = 0.0f;
    SystemManager::GetInstance().Update(&dt);

    if (d->resetRequested) {
        d->resetRequested = false;
        for (int i = 0; i < 16; ++i) {
            if (g_pSlashEntities[i]) {
                g_pSlashEntities[i]->Reset();
            }
        }
        printf("[scene_slash] blade Reset()\n");
    }

    // Update all slash entities (PreUpdate + Update + PostUpdate).
    // Mirrors the active-branch in GameUpdate: PreUpdate once, then
    // Update+PostUpdate per entity.
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) {
            g_pSlashEntities[i]->PreUpdate(dt);
            break;
        }
    }
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) {
            g_pSlashEntities[i]->Update(dt);
            g_pSlashEntities[i]->PostUpdate(dt);
        }
    }

    // Periodic status log.
    if (d->frameCount % 30 == 0) {
        bool active = g_pSlashEntity ? g_pSlashEntity->IsBladeActive() : false;
        printf("[scene_slash] f=%d blade active=%s\n",
               d->frameCount, active ? "yes" : "no");
    }
    ++d->frameCount;

    // === Minimal isolated draw ===
    // Port specific: standalone isolated entity scene
    // Projection: blade uses the same centered ortho as fruits --
    // SetupOrtho(160, -160, -240, 240, 2000, -6000). Blade vertices are
    // in game touch space (-240..+240 x, -160..+160 y); no separate 2D ortho
    // is needed. GameDraw calls SetupPerspective(FruitCamera::PT_STANDARD, false) once at
    // the top of the frame and DrawSlice inherits that same projection.
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();
    dm.BeginFrame();
    // Override the black clear to a neutral dark-wood grey so the blade trail
    // is clearly visible against a non-black background.
    glClearColor(0.13f, 0.10f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // SetupPerspective uploads the centered ortho: SetupOrtho(160,-160,-240,240,2000,-6000).
    // Both 3D entities (fruit, bomb) and the 2D blade trail share this space.
    // The blade vertices injected via touch coords (-192..+192 x, 0 y) are directly
    // in this coordinate space and will appear as a horizontal streak.
    if (game_work.m_FruitCamera)
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_STANDARD, true);

    // Blade trail: depth test OFF (matches GameDraw order for DrawSlice).
    // Binary @ 0x0016b888: SetDepthBuffer(0) before the per-finger DrawSlice loop.
    dm.SetDepthBuffer(false);
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) g_pSlashEntities[i]->DrawSlice();
    }

    SDL_GL_SwapWindow(window);
    return true;
}

int main(int argc, char* argv[]) {
    // Port specific: standalone isolated entity scene

    fn::TestHarness h(argc, argv, "scene_slash");
    // Default to visible window; --screenshot or --headless suppress it.
    h.SetInteractiveDefault(true);
    // No burn-in: we call GameInit directly below.
    h.SetInitFrames(0);

    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    // GameInit creates the SlashEntity[16] array, registers input callbacks
    // (GameTaskInitInput), and wires touch->slash dispatch.
    // The initComplete guard inside GameInit prevents double-init.
    // Entered through the task dispatcher (NOT a bare GameInit(0)) so
    // GameTaskExit dispatches GameExit at teardown -- see EnterGameState().
    h.EnterGameState();

    if (!g_pSlashEntity) {
        fprintf(stderr, "[scene_slash] FAIL: g_pSlashEntity is null after GameInit\n");
        return 1;
    }
    printf("[scene_slash] g_pSlashEntity=%p OK\n", (void*)g_pSlashEntity);

    Mortar::SoundManager::GetInstance().SetSFXVolume(0.0f);

    // Reset all slash entities to a clean initial state.
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) g_pSlashEntities[i]->Reset();
    }

    SceneSlashData sceneData;
    sceneData.resetRequested = false;
    sceneData.frameCount     = 0;

    if (h.IsInteractive()) {
        // Visible interactive loop -- our own frame loop, NOT game.runFrames.
        SDL_GL_SetSwapInterval(1);
        while (h.game.running) {
            if (!SceneFrameTick(&sceneData, h.game,
                                static_cast<SDL_Window*>(h.window))) {
                h.game.running = false;
            }
        }
    } else {
        // Port specific: standalone isolated entity scene -- headless screenshot mode.
        //
        // SWIPE SPREAD ACROSS FRAMES:
        // Inject the synthetic swipe incrementally, a few FINGERMOTION events per
        // frame, so UpdatePoints builds geometry frame-by-frame (matching how the
        // binary processes one frame's events per Update tick). All 30 steps are
        // spread across 10 frames (3 steps/frame) as a diagonal-ish horizontal
        // streak from left (~10%) to right (~90%) at screen centre (y=50%).
        //
        // SCREENSHOT TIMING:
        // The screenshot is taken BEFORE FINGERUP -- while m_State=1 (actively
        // stroking, no retraction). After FINGERUP the blade retracts 2 points/
        // frame; with 30 FINGERMOTION events (~60 points), it would fully retract
        // in 30 frames. By shooting mid-swipe the trail is at peak length and
        // fully visible.
        //
        // The swipe runs x = 0.1 -> 0.9 in 30 steps, y = 0.5 (centre).
        // Each FINGERMOTION moves ~2.7% of screen width = ~13 game units.
        // In touch coords: x maps -240..+240, y maps -160..+160.
        // A swipe from nx=0.1 to nx=0.9 covers game-x -192..+192 at y=0.
        static const int   TOTAL_STEPS    = 30;
        static const int   STEPS_PER_FRAME = 3;
        static const float START_NX       = 0.1f;
        static const float END_NX         = 0.9f;
        static const float NY             = 0.5f;
        static const SDL_TouchID TID      = 1;

        float dx_step = (END_NX - START_NX) / (float)(TOTAL_STEPS - 1);

        // Frame 0: inject FINGERDOWN at the start position + first motion.
        InjectFingerDown(TID, START_NX, NY);
        SceneFrameTick(&sceneData, h.game, static_cast<SDL_Window*>(h.window));

        // Frames 1..(TOTAL_STEPS/STEPS_PER_FRAME): inject STEPS_PER_FRAME
        // FINGERMOTION events per frame to spread the swipe across multiple ticks.
        int step = 1;
        while (step < TOTAL_STEPS) {
            for (int s = 0; s < STEPS_PER_FRAME && step < TOTAL_STEPS; ++s, ++step) {
                float nx = START_NX + dx_step * (float)step;
                InjectFingerMotion(TID, nx, NY, dx_step, 0.0f);
            }
            SceneFrameTick(&sceneData, h.game, static_cast<SDL_Window*>(h.window));
        }

        // Screenshot is taken HERE -- while finger is still DOWN (m_State=1,
        // no retraction). The blade is at peak length (~60 points).
        // We need to capture before SDL_GL_SwapWindow so glReadPixels reads
        // the just-rendered back buffer. Do a final render frame without a
        // screenshot, but capture via h.ScreenshotPng() which calls glReadPixels
        // before the swap in the NEXT tick -- or just shoot now after the last
        // tick (the last SceneFrameTick already swapped; the back buffer now
        // holds the OLD front buffer which had the rendered frame).
        //
        // On double-buffered desktops after N swaps the parity determines
        // which buffer glReadPixels reads. The reliable approach is to render
        // one more frame WITHOUT calling SDL_GL_SwapWindow and shoot from that.
        // Port specific: render a final capture frame (no swap) for the screenshot.
        {
            int ww2 = 0, wh2 = 0;
            SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(h.window), &ww2, &wh2);
            glViewport(0, 0, ww2, wh2);
            Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();
            dm.BeginFrame();
            glClearColor(0.13f, 0.10f, 0.08f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (game_work.m_FruitCamera)
                game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_STANDARD, true);
            dm.SetDepthBuffer(false);
            for (int i = 0; i < 16; ++i) {
                if (g_pSlashEntities[i]) g_pSlashEntities[i]->DrawSlice();
            }
            // Do NOT call SDL_GL_SwapWindow -- leave the just-rendered content
            // in the back buffer so glReadPixels captures it.
            if (h.IsScreenshot()) h.ScreenshotPng();
            // Now swap to display it (optional for headless but keeps state clean).
            SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
        }
    }

    return h.Shutdown();
}
