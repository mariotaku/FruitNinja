// scene_fruit.cpp -- standalone isolated fruit scene.
// Boots the engine (GameInitialise), then renders ONE watermelon on a plain
// cleared background. The menu, HUD, WaveManager, and MainScreen are never
// drawn -- the scene runs its own minimal per-frame loop.
//
// Port specific: standalone isolated entity scene
//
// Controls:
//   ESC          -- quit
//   SPACE        -- reset / respawn the current fruit type
//   click / tap  -- cycle through all fruit types then BOMB then back to fruit 0
//   F12          -- screenshot (BMP via GameSDL.cpp handler)
//   --screenshot  -- headless one-shot: run hidden, dump PPM to
//                    tmp/test/screenshots/scene_fruit.png, exit
//   --headless    -- hidden window, no screenshot
//   --chuck       -- ballistic arc mode (old behaviour: launches upward with gravity)
//   (bare launch) -- visible window, fruit centered and spinning

#include "../test_harness.h"
#include "entities/Fruit.h"
#include "entities/Bomb.h"
#include "entities/ActorManager.h"
#include "entities/Entity.h"
#include "game/GameWork.h"
#include "game/GameTaskState.h"
#include "game/FruitCamera.h"
#include "render/DisplayManager.h"
#include "core/SystemManager.h"
#include "render/gl_funcs.h"
#include "Game.h"
#include <cstdio>
#include <cstring>

// Port specific: standalone isolated entity scene -- coordinate space is the
// centered ortho: X in [-240,+240] (horizontal), Y in [-160,+160] (vertical).
// Screen center = (0, 0).
static const float SCENE_CENTER_X    = 0.0f;
static const float SCENE_CENTER_Y    = 0.0f;

// Chuck (ballistic) mode launch parameters -- old behaviour, activated with --chuck.
static const float CHUCK_START_Y     = -150.0f;
static const float CHUCK_VEL_Y       =  12.0f;
static const float CHUCK_GRAVITY_Y   =  -3.5f;

// Slow steady spin for centered mode: Y-axis spin so the fruit rotates
// showing all faces. Magnitude matched to binary's range (2..4) but at the
// low end so it's readable.
static const float SPIN_MAGNITUDE    =  2.5f;

struct SceneFruitData {
    Fruit*  fruit;
    Bomb*   bomb;
    // Cycle step: 0 .. g_FruitInfoCount-1 = fruit types; step == count = bomb.
    int     step;
    int     fruitType;   // mirrored from step when step < count; -1 when bomb slot
    int     respawnCountdown;
    bool    requestRespawn;
    bool    chuckMode;
};

// Kill any currently live entity (fruit or bomb) before spawning a new one.
static void KillCurrentEntity(SceneFruitData* d) {
    if (d->fruit) {
        d->fruit->KillFruit(false);
        d->fruit = NULL;
    }
    if (d->bomb) {
        d->bomb->KillBomb();
        d->bomb = NULL;
    }
}

// Spawn (or re-spawn) the entity for the current step.
// step < g_FruitInfoCount => fruit; step == count => bomb.
static void SpawnSceneFruit(SceneFruitData* d, Game& game) {
    KillCurrentEntity(d);

    Mortar::ActorManager* am = game.actorManager;
    if (!am) return;

    const int count = g_FruitInfoCount;
    const bool isBombSlot = (d->step == count);

    if (isBombSlot) {
        // --- Bomb path ---
        Mortar::Entity* e = am->Add(1, true);
        if (!e) {
            fprintf(stderr, "[scene_fruit] actorManager->Add(1) returned null\n");
            return;
        }
        d->bomb = static_cast<Bomb*>(e);
        d->bomb->Init(NULL, 0, NULL);

        // Centered spinning: suppress gravity + ballistic.
        d->bomb->pos         = _Vector3<float>(SCENE_CENTER_X, SCENE_CENTER_Y, 0.0f);
        d->bomb->vel         = _Vector3<float>(0.0f, 0.0f, 0.0f);
        d->bomb->m_AccelForce = _Vector3<float>(0.0f, 0.0f, 0.0f);
        // Bomb::Init sets random m_RotVelX/Y (1..8); override to a
        // controlled slow spin so it reads well in the viewer.
        d->bomb->m_RotVelX   = (int16_t)2;
        d->bomb->m_RotVelY   = (int16_t)1;
        // Activate (not killed, not inactive).
        d->bomb->flags &= ~(uint32_t)(0x01 | 0x10);

        printf("[scene_fruit] spawned BOMB at (%.1f,%.1f) chuck=%s\n",
               d->bomb->pos.x, d->bomb->pos.y,
               d->chuckMode ? "yes" : "no");
    } else {
        // --- Fruit path ---
        d->fruitType = d->step;

        Mortar::Entity* e = am->Add(0, true);
        if (!e) {
            fprintf(stderr, "[scene_fruit] actorManager->Add(0) returned null\n");
            return;
        }

        d->fruit = static_cast<Fruit*>(e);
        d->fruit->Init(NULL, d->fruitType, NULL);

        if (d->chuckMode) {
            // Ballistic arc: launch from near bottom, fly upward.
            d->fruit->pos           = _Vector3<float>(SCENE_CENTER_X, CHUCK_START_Y, 0.0f);
            d->fruit->vel           = _Vector3<float>(0.0f, CHUCK_VEL_Y, 0.0f);
            d->fruit->m_Gravity     = _Vector3<float>(0.0f, CHUCK_GRAVITY_Y, 0.0f);
            d->fruit->m_bBallisticEnable = 1;
            d->fruit->Chuck(0.0f);
            _Vector3<float> spinAxis(1.0f, 1.0f, 1.0f);
            d->fruit->RotateFacingUp(true, spinAxis);
        } else {
            // Centered spinning: pinned at screen center, no ballistic.
            d->fruit->pos              = _Vector3<float>(SCENE_CENTER_X, SCENE_CENTER_Y, 0.0f);
            d->fruit->vel              = _Vector3<float>(0.0f, 0.0f, 0.0f);
            d->fruit->m_Gravity        = _Vector3<float>(0.0f, 0.0f, 0.0f);
            d->fruit->m_bBallisticEnable = 0;
            // RotateFacingUp sets m_RotVel1/2 = spinVelAxis * random magnitude.
            // We override the velocity afterward to a deterministic slow Y-spin.
            _Vector3<float> spinAxis(0.0f, 1.0f, 0.0f);
            d->fruit->RotateFacingUp(false, spinAxis);
            // Override spin to a controlled constant.
            d->fruit->m_RotVel1 = _Vector3<float>(0.0f, SPIN_MAGNITUDE, 0.0f);
            d->fruit->m_RotVel2 = _Vector3<float>(0.0f, SPIN_MAGNITUDE, 0.0f);
            // Activate (not killed, not inactive) so ActorManager::Draw picks it up.
            d->fruit->flags &= ~(uint32_t)(0x01 | 0x10);  // clear ENT_INACTIVE | ENT_KILLED
        }

        const char* fname = Fruit::FruitTypeName(d->fruitType);
        printf("[scene_fruit] spawned fruitType=%d (%s) at (%.1f,%.1f) chuck=%s\n",
               d->fruitType, (fname ? fname : "?"),
               d->fruit->pos.x, d->fruit->pos.y,
               d->chuckMode ? "yes" : "no");
    }
}

// Per-frame entity-only update (replaces game.runFrames which also runs GameTaskUpdate/Draw).
// Updates ONLY: dt from SystemManager, the fruit entity.
// Returns false to quit.
static bool SceneFrameTick(SceneFruitData* d, Game& game, SDL_Window* window) {
    // Poll SDL events.
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return false;
        if (ev.type == SDL_KEYDOWN) {
            if (ev.key.keysym.sym == SDLK_ESCAPE) return false;
            if (ev.key.keysym.sym == SDLK_SPACE)  d->requestRespawn = true;
        }
        // Tap / click -> cycle through all fruit types (0..count-1) then bomb
        // (step == count), then wrap back to fruit 0. Total steps = count+1.
        // Handle MOUSEBUTTONDOWN only: with SDL_HINT_MOUSE_TOUCH_EVENTS a click
        // also fires a synthesized FINGERDOWN, which would double-advance the cycle.
        if (ev.type == SDL_MOUSEBUTTONDOWN) {
            int count = g_FruitInfoCount;
            if (count > 0) d->step = (d->step + 1) % (count + 1);
            d->requestRespawn = true;
        }
    }

    // Fixed dt from SystemManager (matches the game's frame-tick path).
    float dt = 0.0f;
    SystemManager::GetInstance().Update(&dt);

    // Update the active entity manually (matches the ActorManager::Update call path):
    // Entity::Update -> Entity::PostUpdate are the two vtable calls ActorManager makes.
    if (d->fruit && !(d->fruit->flags & (uint32_t)(0x01 | 0x10))) {
        d->fruit->Update(dt);
        d->fruit->PostUpdate(dt);

        // Centered mode: clamp pos back to center each frame so any residual
        // ballistic drift (e.g. from flags not fully suppressing it) doesn't
        // send the fruit off screen.
        if (!d->chuckMode) {
            d->fruit->pos.x = SCENE_CENTER_X;
            d->fruit->pos.y = SCENE_CENTER_Y;
        }
    }
    if (d->bomb && !(d->bomb->flags & (uint32_t)(0x01 | 0x10))) {
        d->bomb->Update(dt);
        d->bomb->PostUpdate(dt);

        // Centered mode: pin the bomb to screen center.
        d->bomb->pos.x = SCENE_CENTER_X;
        d->bomb->pos.y = SCENE_CENTER_Y;
    }

    // Auto-respawn in chuck mode when fruit goes offscreen or dies.
    if (d->chuckMode) {
        bool gone = (!d->fruit) ||
                    (d->fruit->flags & (uint32_t)0x10) ||
                    d->fruit->CheckHasGoneOffscreen();
        if (gone) {
            if (d->respawnCountdown <= 0) d->respawnCountdown = 30;
            else {
                --d->respawnCountdown;
                if (d->respawnCountdown == 0) d->requestRespawn = true;
            }
        } else {
            d->respawnCountdown = 0;
        }
    }

    if (d->requestRespawn) {
        d->requestRespawn = false;
        SpawnSceneFruit(d, game);
    }

    // === Minimal isolated draw ===
    // SetupPerspective uploads the centered ortho projection matrix.
    // BeginFrame sets blend state + clears. We override the clear colour
    // to a neutral mid-grey so the watermelon is clearly readable.
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();
    dm.BeginFrame();
    // Override the black clear with a neutral dark-wood grey.
    // BeginFrame already called glClear; we set a new colour and re-clear.
    glClearColor(0.13f, 0.10f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (game_work.m_FruitCamera)
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_STANDARD, true);

    // 3D entity draw: depth write ON, depth test ON.
    dm.SetDepthBufferWrite(true);
    dm.SetDepthBuffer(true);
    if (game.actorManager)
        game.actorManager->Draw(game.renderer);

    dm.SetDepthBuffer(false);

    SDL_GL_SwapWindow(window);
    return true;
}

int main(int argc, char* argv[]) {
    // Port specific: standalone isolated entity scene

    bool chuckMode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--chuck") == 0) chuckMode = true;
    }

    fn::TestHarness h(argc, argv, "scene_fruit");
    // Default to visible window; --screenshot or --headless suppress it.
    h.SetInteractiveDefault(true);
    // No burn-in: GameInitialise (called inside game.init) is all we need.
    // GameInit (which sets up MainScreen/PauseScreen/SlashEntities/WaveManager)
    // is NOT called -- the scene drives ActorManager directly.
    h.SetInitFrames(0);

    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    // GameInit (state-2 task handler) sets up MainScreen, HUD, SlashEntities.
    // For the fruit scene we need: actorManager (GameInitialise), FruitCamera
    // (GameInitialise), and a Fruit factory (GameInitialise step 28 RegisterFactory).
    // Enter state 2 through the task dispatcher so prespawn pools and the factory
    // delegate are wired AND GameTaskExit dispatches GameExit at teardown (a bare
    // GameInit(0) leaves the task state unregistered) -- see EnterGameState().
    // The initComplete guard inside GameInit prevents double-initialisation.
    h.EnterGameState();

    Mortar::SoundManager::GetInstance().SetSFXVolume(0.0f);

    int watermelonType = Fruit::FruitType("watermelon", false);
    if (watermelonType < 0) {
        fprintf(stderr, "[scene_fruit] WARN: 'watermelon' not found, using type 0\n");
        watermelonType = 0;
    }
    printf("[scene_fruit] watermelon fruitType=%d  chuck=%s\n",
           watermelonType, chuckMode ? "yes" : "no");

    SceneFruitData sceneData;
    sceneData.fruit            = NULL;
    sceneData.bomb             = NULL;
    sceneData.step             = watermelonType;
    sceneData.fruitType        = watermelonType;
    sceneData.respawnCountdown = 0;
    sceneData.requestRespawn   = true;
    sceneData.chuckMode        = chuckMode;

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
        // Headless screenshot mode: spawn, tick 60 frames, screenshot.
        SceneFrameTick(&sceneData, h.game, static_cast<SDL_Window*>(h.window));
        for (int i = 0; i < 60; ++i) {
            SceneFrameTick(&sceneData, h.game, static_cast<SDL_Window*>(h.window));
        }
        if (h.IsScreenshot()) h.ScreenshotPng();
    }

    return h.Shutdown();
}
