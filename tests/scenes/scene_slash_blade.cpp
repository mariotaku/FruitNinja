// scene_slash_blade.cpp -- blade skin colour regression / diagnostic test.
//
// Tests that the flame / shiny_red / default blade skins actually render
// non-black pixels on the native GL host.  Isolates the "flame blade renders
// black on web" bug by capturing the readback colour on host so we know what
// the correct colour looks like (the web trace can then differ from it).
//
// Per skin it reports:
//   g_ModTexture IsValid + texId + dims
//   m_BaseColour (per-vertex lerp result from UpdateModColour)
//   first vertex buffer colour (BGRA word after UpdatePoints)
//   rendered readback colour (dominant non-background R,G,B avg over trail region)
//
// Asserts:
//   For "flame":  readback dominant colour is NOT near-black (r<16 && g<16 && b<16).
//                 If it IS near-black on native GL, the bug exists on native too.
//   For "default": readback has non-background pixels (basic sanity).
//
// Build and run:
//   cmake --build build/host -j
//   build/host/tests/scenes/scene_slash_blade_scene.exe
//
// Port specific: standalone blade-skin diagnostic / regression scene.
//
// Screenshot: tmp/test/screenshots/scene_slash_blade_<skin>.png
//
// Controls (interactive mode, --interactive flag):
//   ESC -- quit
//   1   -- switch to "default" skin
//   2   -- switch to "flame" skin
//   3   -- switch to "shiny_red" skin
//   R   -- reset blade trail

#include "../test_harness.h"
#include "entities/SlashEntity.h"
#include "game/GameWork.h"
#include "game/GameTaskState.h"
#include "game/FruitCamera.h"
#include "render/DisplayManager.h"
#include "core/SystemManager.h"
#include "platform/InputTranslatorSDL.h"
#include "render/gl_funcs.h"
#include "asset/TextureManager.h"
#include "Game.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------------------
// Skin definitions
// ---------------------------------------------------------------------------

struct SkinDef {
    const char* name;
    // SetModColours params
    // palette: single entry; for "default" we call InitModColours instead.
    Colour palette[1];
    const char* particlePath;
    const char* textureName2;
    bool isDefault;  // true = call InitModColours (no mod tex), not SetModColours
};

static SkinDef s_Skins[] = {
    // default: uses blade.tex (g_ModTexture = null), white palette.
    { "default",
      { Colour(255,255,255,255) },
      "", "",
      true },
    // flame: loads flame_blade.tex, white palette, count=1, type=0.
    // Matches FLAME_BLADE item's SlashModInfo::SetEquipped call.
    { "flame",
      { Colour(255,255,255,255) },
      "flame_blade", "flame_blade",
      false },
    // shiny_red: loads shiney_red_blade.tex, white palette.
    // Known-working skin for comparison.
    { "shiny_red",
      { Colour(255,255,255,255) },
      "shiney_red_blade", "shiney_red_blade",
      false },
};

static const int NUM_SKINS = 3;

// ---------------------------------------------------------------------------
// Background colour: pure blue so a black blade trail is distinguishable.
// Any pixel with r < 40 AND g < 40 AND b > 150 is considered background.
// ---------------------------------------------------------------------------
static const unsigned char BG_R = 0;
static const unsigned char BG_G = 0;
static const unsigned char BG_B = 255;

static bool IsBackground(unsigned char r, unsigned char g, unsigned char b) {
    return (r < 40) && (g < 40) && (b > 150);
}

// ---------------------------------------------------------------------------
// Swipe injection helpers (same convention as scene_slash.cpp)
// ---------------------------------------------------------------------------
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

static void InjectFingerMotion(SDL_TouchID tid, float nx, float ny,
                                float dx, float dy) {
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

// ---------------------------------------------------------------------------
// Apply a skin via SetModColours / InitModColours.
// ---------------------------------------------------------------------------
static void ApplySkin(const SkinDef& skin) {
    if (skin.isDefault) {
        SlashEntity::InitModColours();
    } else {
        SlashEntity::SetModColours(
            skin.palette,       // const Colour*
            1,                  // colourCount
            0,                  // colourType (0 = static)
            1.0f,               // lifeScale
            skin.particlePath,  // particlePath  (trail emitter name)
            skin.textureName2,  // textureName2  (texture file stem)
            false,              // directional
            "",                 // contactParticle
            ""                  // particle2
        );
    }
}

// ---------------------------------------------------------------------------
// Render one blade frame.
// Clear to blue background, draw the slash trail, leave in back-buffer (no swap).
// ---------------------------------------------------------------------------
static void RenderBladeFrame(SDL_Window* window) {
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();
    dm.BeginFrame();

    // Blue background so a black blade trail is easily spotted.
    glClearColor((float)BG_R / 255.f, (float)BG_G / 255.f, (float)BG_B / 255.f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (game_work.m_FruitCamera) {
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_STANDARD, true);
    }

    dm.SetDepthBuffer(false);
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) g_pSlashEntities[i]->DrawSlice();
    }
}

// ---------------------------------------------------------------------------
// Tick the slash entity: drain SDL events, dispatch, then Update.
// Port specific: drain events (no dispatch), then DispatchForSimTick
// to match the #173 drain/dispatch split invariant.
// ---------------------------------------------------------------------------
static void TickSlash(Game& game, SDL_Window* window) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (game.inputTranslator) {
            game.inputTranslator->DrainSDLEvent(ev,
                static_cast<SDL_Window*>(window));
        }
    }
    if (game.inputTranslator) game.inputTranslator->DispatchForSimTick();
    float dt = 0.0f;
    SystemManager::GetInstance().Update(&dt);

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
}

// ---------------------------------------------------------------------------
// Inject a synthetic horizontal swipe and tick until trail is built.
// Returns when trail is rendered (m_PointCount > 3 on at least one entity).
// ---------------------------------------------------------------------------
static void InjectSwipe(Game& game, SDL_Window* window) {
    static const int   TOTAL_STEPS    = 30;
    static const int   STEPS_PER_FRAME = 3;
    static const float START_NX       = 0.1f;
    static const float END_NX         = 0.9f;
    static const float NY             = 0.5f;
    static const SDL_TouchID TID      = 1;

    float dx_step = (END_NX - START_NX) / (float)(TOTAL_STEPS - 1);

    InjectFingerDown(TID, START_NX, NY);
    TickSlash(game, window);

    int step = 1;
    while (step < TOTAL_STEPS) {
        for (int s = 0; s < STEPS_PER_FRAME && step < TOTAL_STEPS; ++s, ++step) {
            float nx = START_NX + dx_step * (float)step;
            InjectFingerMotion(TID, nx, NY, dx_step, 0.0f);
        }
        TickSlash(game, window);
    }
    // Finger stays down (no FINGERUP) so trail remains at peak length.
    // Run a few more ticks so UpdatePoints has time to propagate m_BaseColour.
    for (int i = 0; i < 3; ++i) {
        TickSlash(game, window);
    }
}

// ---------------------------------------------------------------------------
// Compute dominant non-background colour from the pixel buffer.
// Returns false if no non-background pixels found.
// ---------------------------------------------------------------------------
static bool DominantColour(const unsigned char* pixels, int w, int h,
                            unsigned char* outR, unsigned char* outG,
                            unsigned char* outB, int* outCount) {
    long sumR = 0, sumG = 0, sumB = 0;
    int count = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char* px = pixels + (y * w + x) * 3;
            if (!IsBackground(px[0], px[1], px[2])) {
                sumR += px[0]; sumG += px[1]; sumB += px[2];
                ++count;
            }
        }
    }
    if (outCount) *outCount = count;
    if (count == 0) { *outR = 0; *outG = 0; *outB = 0; return false; }
    *outR = (unsigned char)(sumR / count);
    *outG = (unsigned char)(sumG / count);
    *outB = (unsigned char)(sumB / count);
    return true;
}

// ---------------------------------------------------------------------------
// Run one skin through the full render + readback pipeline.
// Returns true if the test assertion passes for that skin.
// ---------------------------------------------------------------------------
static bool RunSkin(fn::TestHarness& h, const SkinDef& skin) {
    printf("\n[blade_skin] --- skin: %s ---\n", skin.name);

    // --- Reset all slash entities to clean state ---
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) g_pSlashEntities[i]->Reset();
    }

    // Drain any residual SDL events from previous skin.
    {
        SDL_Event drain;
        while (SDL_PollEvent(&drain)) {}
    }

    // --- Apply blade skin ---
    ApplySkin(skin);

    // --- Report texture state immediately after equip ---
    {
        const Mortar::SmartPtr<Mortar::Texture>& modTex =
            SlashEntity::GetModTexture();
        bool valid = modTex.IsValid();
        unsigned int texId = 0;
        int texW = 0, texH = 0;
        if (valid) {
            texId = modTex->GetTexId();
            texW  = modTex->GetWidth();
            texH  = modTex->GetHeight();
        }
        printf("[blade_skin]   g_ModTexture: valid=%s texId=%u dims=%dx%d\n",
               valid ? "yes" : "no",
               texId, texW, texH);
        if (!skin.isDefault && !valid) {
            fprintf(stderr,
                "[blade_skin]   WARN: skin '%s' has textureName2='%s' but "
                "g_ModTexture is not valid (texture file missing?)\n",
                skin.name, skin.textureName2);
        }
    }

    // --- Inject swipe and build trail ---
    InjectSwipe(h.game, static_cast<SDL_Window*>(h.window));

    // --- Report per-vertex data ---
    if (g_pSlashEntity) {
        const Colour& bc = g_pSlashEntity->GetBaseColour();
        int pc = g_pSlashEntity->GetPointCount();
        uint32_t firstVert = g_pSlashEntity->GetFirstVertexColour();
        printf("[blade_skin]   m_BaseColour: R=%u G=%u B=%u A=%u\n",
               bc.r, bc.g, bc.b, bc.a);
        printf("[blade_skin]   m_PointCount: %d\n", pc);
        // First vertex colour is in BGRA order in the buffer (Colour struct
        // packs as b,g,r,a; PlatformColour() = (a<<24)|(b<<16)|(g<<8)|r).
        // Print BGRA word as-is, then unpack to R/G/B for readability.
        unsigned char bv_b = (unsigned char)((firstVert >> 16) & 0xff);
        unsigned char bv_g = (unsigned char)((firstVert >>  8) & 0xff);
        unsigned char bv_r = (unsigned char)((firstVert      ) & 0xff);
        unsigned char bv_a = (unsigned char)((firstVert >> 24) & 0xff);
        printf("[blade_skin]   firstVertex.colour: BGRA=0x%08X -> R=%u G=%u B=%u A=%u\n",
               firstVert, bv_r, bv_g, bv_b, bv_a);

        // Report UV spread across the trail -- key diagnostic for UV normalization bug.
        // Expected: UVs spread from ~0.0 to 0.98 across trail positions.
        // Bug symptom: UV[0] very negative (clamped to 0 by GL) if normFactor=arcTotal
        // instead of 1.0, causing the whole trail to sample from UV=0 (dark edge).
        if (pc > 0) {
            float u0 = g_pSlashEntity->GetVertexU(0);
            float u_mid = (pc >= 2) ? g_pSlashEntity->GetVertexU(pc / 2) : -99.f;
            float u_last = (pc >= 2) ? g_pSlashEntity->GetVertexU(pc - 2) : -99.f;
            printf("[blade_skin]   vertex UV.x: [0]=%.4f [mid]=%.4f [last]=%.4f"
                   " (expected 0.0..0.98 spread; near-0 everywhere = UV remap bug)\n",
                   u0, u_mid, u_last);
        }
    }

    // --- Render a capture frame (no swap -> back buffer holds rendered result) ---
    RenderBladeFrame(static_cast<SDL_Window*>(h.window));

    // --- Read pixels ---
    int fw = 0, fh = 0;
    unsigned char* pixels = h.ReadPixels(&fw, &fh);
    if (!pixels) {
        fprintf(stderr, "[blade_skin]   FAIL: ReadPixels returned null\n");
        return false;
    }

    // --- Save screenshot (re-reads the intact back buffer) ---
    {
        char name[256];
        snprintf(name, sizeof(name), "scene_slash_blade_%s", skin.name);
        h.ScreenshotPng(name);
    }

    // --- Compute dominant readback colour ---
    unsigned char domR = 0, domG = 0, domB = 0;
    int nonBgCount = 0;
    bool hasFg = DominantColour(pixels, fw, fh,
                                &domR, &domG, &domB, &nonBgCount);
    free(pixels);

    printf("[blade_skin]   readback: non-background pixels=%d  "
           "dominant R=%u G=%u B=%u\n",
           nonBgCount, domR, domG, domB);

    bool pass = true;

    if (!hasFg) {
        fprintf(stderr,
            "[blade_skin]   FAIL (%s): zero non-background pixels rendered. "
            "Blade trail was not drawn (m_PointCount <= 3, or texture invalid, "
            "or clear-colour confusion).\n", skin.name);
        pass = false;
    } else {
        // Check for the "renders black" bug specifically on the flame skin.
        // The root cause is a UV normalization bug in UpdatePoints:
        //   normFactor = arcTotal / g_Scale4  (where arcTotal = trail arc in game units ~384)
        //   uRemap = 0.98 - (1 - arc_i/arcTotal * 0.98) * normFactor
        //          = arc_i * 0.98 + (0.98 - arcTotal)    <- 0.98 - arcTotal is very negative
        // With GL_REPEAT, frac(negative_u) wraps to the right edge of the texture.
        // For flame_blade.tex: bright right, dark left -> wrapping to right is bright,
        //   but the distribution of frac(-383..)...frac(-6.7) hits mostly dark pixels.
        // For blade.tex: uniform grey -> any UV distribution gives avg grey (appears OK).
        // FIX NEEDED: RE binary UpdatePoints UV remap formula and port correctly.
        // (Do not apply an empirical fix here -- RE first per CLAUDE.md policy.)
        bool isBlack = (domR < 16 && domG < 16 && domB < 16);
        if (isBlack) {
            fprintf(stderr,
                "[blade_skin]   FAIL (%s): trail readback is near-black "
                "(R=%u G=%u B=%u).\n"
                "  DIAGNOSIS: UV normalization bug in UpdatePoints: UVs are massively\n"
                "  negative (~-383 to -6.7) instead of 0..0.98. GL_REPEAT wraps them\n"
                "  to frac values that happen to sample flame_blade.tex's dark left edge.\n"
                "  Same UV bug affects default/shiny_red but their texture content\n"
                "  averages to non-black across the wrapped UV distribution.\n"
                "  ROOT CAUSE: normFactor = arcTotal / g_Scale4 should be 1.0 or\n"
                "  the remap formula needs RE from binary UpdatePoints.\n",
                skin.name, domR, domG, domB);
            pass = false;
        } else {
            printf("[blade_skin]   PASS (%s): trail rendered non-black "
                   "(R=%u G=%u B=%u)\n", skin.name, domR, domG, domB);
            if (domR < 30 || domG < 10) {
                // Warn if default blade looks unexpectedly tinted -- also a symptom
                // of the UV remap (wrapping to a biased region of the texture).
                printf("[blade_skin]   NOTE (%s): colour is biased -- UV wrap may be"
                       " sampling a non-uniform texture region (same UV remap issue).\n",
                       skin.name);
            }
        }
    }

    // Swap to display the result (keeps the window coherent in interactive mode).
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));

    return pass;
}

// ---------------------------------------------------------------------------
// Interactive mode: allow switching skins via keyboard
// ---------------------------------------------------------------------------
struct InteractiveData {
    int  activeSkin;       // index into s_Skins[]
    bool skinDirty;        // true = need to re-apply skin next tick
    bool resetRequested;
    int  frameCount;
};

static bool InteractiveTick(fn::TestHarness& h, InteractiveData* d) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return false;
        if (ev.type == SDL_KEYDOWN) {
            if (ev.key.keysym.sym == SDLK_ESCAPE) return false;
            if (ev.key.keysym.sym == SDLK_r) { d->resetRequested = true; }
            if (ev.key.keysym.sym == SDLK_1) { d->activeSkin = 0; d->skinDirty = true; }
            if (ev.key.keysym.sym == SDLK_2) { d->activeSkin = 1; d->skinDirty = true; }
            if (ev.key.keysym.sym == SDLK_3) { d->activeSkin = 2; d->skinDirty = true; }
        } else {
            if (h.game.inputTranslator) {
                h.game.inputTranslator->DrainSDLEvent(
                    ev, static_cast<SDL_Window*>(h.window));
            }
        }
    }
    if (h.game.inputTranslator) h.game.inputTranslator->DispatchForSimTick();

    float dt = 0.0f;
    SystemManager::GetInstance().Update(&dt);

    if (d->skinDirty) {
        d->skinDirty = false;
        ApplySkin(s_Skins[d->activeSkin]);
        printf("[blade_skin] switched to skin: %s\n",
               s_Skins[d->activeSkin].name);
    }

    if (d->resetRequested) {
        d->resetRequested = false;
        for (int i = 0; i < 16; ++i) {
            if (g_pSlashEntities[i]) g_pSlashEntities[i]->Reset();
        }
        printf("[blade_skin] Reset()\n");
    }

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

    if (d->frameCount % 60 == 0 && g_pSlashEntity) {
        const Colour& bc = g_pSlashEntity->GetBaseColour();
        printf("[blade_skin] f=%d skin=%s baseColour R=%u G=%u B=%u A=%u  pts=%d\n",
               d->frameCount,
               s_Skins[d->activeSkin].name,
               bc.r, bc.g, bc.b, bc.a,
               g_pSlashEntity->GetPointCount());
    }
    ++d->frameCount;

    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(h.window), &ww, &wh);
    glViewport(0, 0, ww, wh);
    RenderBladeFrame(static_cast<SDL_Window*>(h.window));
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // Port specific: standalone blade-skin diagnostic / regression scene.

    fn::TestHarness h(argc, argv, "scene_slash_blade");
    h.SetInteractiveDefault(false);
    h.SetInitFrames(0);

    if (!h.ParseFlags()) return 1;
    if (!h.Init())       return 1;

    // Entered through the task dispatcher (NOT a bare GameInit(0)) so
    // GameTaskExit dispatches GameExit at teardown -- see EnterGameState().
    h.EnterGameState();
    if (!g_pSlashEntity) {
        fprintf(stderr, "[blade_skin] FAIL: g_pSlashEntity null after GameInit\n");
        return 1;
    }
    printf("[blade_skin] g_pSlashEntity=%p OK\n", (void*)g_pSlashEntity);

    Mortar::SoundManager::GetInstance().SetSFXVolume(0.0f);

    // Reset all slash entities.
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) g_pSlashEntities[i]->Reset();
    }

    // -----------------------------------------------------------------------
    // Interactive mode: 1/2/3 keys switch skins, mouse drag draws trail.
    // -----------------------------------------------------------------------
    if (h.IsInteractive()) {
        SDL_GL_SetSwapInterval(1);
        // Start with default skin.
        ApplySkin(s_Skins[0]);
        printf("[blade_skin] interactive: 1=default 2=flame 3=shiny_red R=reset ESC=quit\n");

        InteractiveData d;
        d.activeSkin    = 0;
        d.skinDirty     = false;
        d.resetRequested = false;
        d.frameCount    = 0;

        while (h.game.running) {
            if (!InteractiveTick(h, &d)) break;
        }
        return h.Shutdown();
    }

    // -----------------------------------------------------------------------
    // Headless: run each skin, collect results.
    // -----------------------------------------------------------------------
    SDL_GL_SetSwapInterval(0);

    // Warm-up frame with the clear colour so the driver allocates the framebuffer.
    {
        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(h.window), &ww, &wh);
        glViewport(0, 0, ww, wh);
        glClearColor((float)BG_R/255.f, (float)BG_G/255.f, (float)BG_B/255.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
    }

    bool allPass = true;
    for (int s = 0; s < NUM_SKINS; ++s) {
        bool ok = RunSkin(h, s_Skins[s]);
        if (!ok) allPass = false;
    }

    printf("\n[blade_skin] SUMMARY: %s\n", allPass ? "ALL PASS" : "ONE OR MORE FAIL");

    h.Shutdown();
    return allPass ? 0 : 1;
}
