// test_menubutton_render.cpp -- isolated MenuButton state render test.
//
// Visually verifies the MenuButton::Draw reconciliation (v1.6.1 MenuButton::Draw
// @0x0019c2e4): the re-inlined quad path, the press-dim RGB*0.5 tint, the
// bouncing NEW badge (IngamePopup 0x10 + |sin| bob), and the 8-blade loading
// sparkle ring (blurry_backing.tex tri list).
//
// Usage: test_menubutton_render [--screenshot] [--interactive]
//
// Default (no flags): headless assertions (state/texture/popup wiring checks).
// Passes via ctest -E screenshot.
// --screenshot: renders one frame with all 4 states side by side and writes:
//   tmp/test/screenshots/menubutton/states.png
//
// Buttons rendered (left to right, world X=-180..+180, Y=+20):
//   Btn 0 (X=-180): NORMAL   -- ring tex m_RingTex[3] + curved "NEW GAME" label
//                               (baseline quad + 3-layer label render)
//   Btn 1 (X= -60): PRESS-DIM -- same tex+label, m_bAcceptsTouch=0 so the
//                               (size != m_RestScale || !m_bAcceptsTouch) &&
//                               m_FruitType<0 gate fires -> quad RGB*0.5
//   Btn 2 (X= +60): NEW BADGE -- SetNewSymbol(true), m_NewIndicatorTimer pinned
//                               to 0.25 (45 deg into the half-sine -> badge
//                               visibly bobbed ~5.7 units up)
//   Btn 3 (X=+180): SPARKLE  -- SetLoadingSymbol(true), m_SparkleTimer pinned
//                               to 3.0 (mid-animation blade-brightness frame 4)
//
// All 4 buttons are m_FruitType=-1 (toggle-style): no live fruit entity, so the
// render is fully deterministic (alpha=255, size=m_RestScale each Update, no
// quad spin, m_ShakeTimer=0 -> no rand() jitter). The anim timers are re-pinned
// AFTER HUD::Update each frame so the captured frame uses exact mid-anim values
// regardless of how many settle frames ran.
//
// C++11-host-only test (not cross-verified); mirrors test_shoplistitem_render.cpp.

#include "test_harness.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "hud/IngamePopup.h"
#include "game/GameWork.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "render/BakedStringBox.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include "engine/util/Delegate.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include <cstdio>
#include <cstdlib>
#include <SDL.h>

static const int   kNumButtons = 4;
static const float kButtonY    = 20.0f;
static const float kButtonX[4] = { -180.0f, -60.0f, 60.0f, 180.0f };
// Toggle-style hit bounds -> m_RestScale (quad size). Ring textures are square;
// 96 units at 120-unit spacing leaves clear gaps between states.
static const float kButtonSize = 96.0f;

// Deterministic mid-anim pin values (re-applied after every HUD::Update):
//   NEW badge: 0.25 * 180 * 182 = 8190 = 0x1FFE (45 deg) -> bob = sin(45)*8 ~= 5.66
//   Sparkle:   frame = 7 - ((int)3.0 % 8) = 4 (mid-rotation blade greys)
static const float kNewBadgePin = 0.25f;
static const float kSparklePin  = 3.0f;

// Verdana caption text per button (ASCII-only).
static const char* const kCaptions[4] = {
    "NORMAL + LABEL",
    "PRESS-DIM (x0.5)",
    "NEW BADGE BOB",
    "SPARKLE RING"
};

// Minimum non-black pixels for the screenshot "something drew" check.
static const int MIN_DRAWN_PIXELS = 200;

static int CountNonBlack(const unsigned char* pixels, int w, int h) {
    int count = 0;
    for (int i = 0; i < w * h; ++i) {
        const unsigned char* px = pixels + i * 3;
        if ((int)px[0] + (int)px[1] + (int)px[2] > 30) ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// FramePass -- one isolated-HUD frame: clear, ortho, HUD Update (then re-pin
// the anim timers so the draw uses exact mid-anim values), HUD BeginDraw+Draw,
// optional Verdana captions. Does NOT SwapWindow; caller swaps (so
// ScreenshotPng can read the back buffer between render and swap).
// ---------------------------------------------------------------------------
static void FramePass(fn::TestHarness& h, MenuButton** btns,
                      Mortar::FontCacheObjectTTF* verdanaFont)
{
    static const float kDt = 1.0f / 60.0f;

    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(h.window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager::GetInstance().BeginFrame();
    MatrixManager::GetInstance().SetupOrtho(
        160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

    if (game_work.mHud) {
        game_work.mHud->Update(kDt);

        // Re-pin anim state AFTER Update (Update advances m_NewIndicatorTimer
        // by 2*dt and m_SparkleTimer by 8*dt) so the drawn frame is exact.
        btns[2]->m_NewIndicatorTimer = kNewBadgePin;
        btns[3]->m_SparkleTimer      = kSparklePin;

        // Mirror GameDraw's scale reset so quads aren't tinted by stale
        // ScreenEffect scales from the burn-in frames.
        game_work.mHud->scales[0] = 1.0f;
        game_work.mHud->scales[1] = 1.0f;
        game_work.mHud->scales[2] = 1.0f;

        game_work.mHud->BeginDraw(kDt);
        game_work.mHud->Draw(0x7FFFFFFF);
    }

    // Verdana caption labels under each button (test scaffolding, not game UI).
    if (verdanaFont) {
        const int   boxW     = 110;
        const int   boxH     = 16;
        const float fontSize = 8.0f;
        const float captionY = -70.0f;
        for (int i = 0; i < kNumButtons; ++i) {
            // align=0x0f: H-center + V-center; flag=1 centers box on pos.
            Mortar::BakedStringBox lbl(verdanaFont, fontSize, boxW, boxH,
                                       (Mortar::ALIGNMENT_TYPE)0x0f, 1, 0);
            lbl.SetText(kCaptions[i]);
            lbl.SetColour(Colour(200, 200, 200, 255), 0);
            Vec3 captionPos(kButtonX[i], captionY, 0.0f);
            lbl.SetTranslation(captionPos, 1);
            lbl.Draw(Vec2(1.0f, 1.0f), 0.0f, 0);
        }
    }
}

int main(int argc, char* argv[]) {
    // Port specific: standalone MenuButton state render test.

    fn::TestHarness h(argc, argv, "menubutton/states");
    // 60 burn-in frames: PreloadRings (ring textures + IngamePopup badges),
    // PreloadFontsTTF (gangofchinese.ttf) and MenuButton::LoadContent
    // (blurry_backing.tex sparkle base) all complete inside game.init(); the
    // burn-in also warms the TTF glyph atlas via MainScreen before the HUD is
    // stripped by InitComponent.
    h.SetInitFrames(60);
    if (!h.ParseFlags()) return 1;
    // Component-isolation: clears the HUD so only our buttons render.
    if (!h.InitComponent()) return 1;

    int failures = 0;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud null after init\n");
        return 1;
    }

    // --- Preconditions: shared assets the 4 states depend on ---
    if (!game_work.m_RingTex[3].IsValid()) {
        std::fprintf(stderr, "FAIL: m_RingTex[3] (NEW GAME ring) not loaded\n");
        ++failures;
    }
    if (!game_work.m_RingTex[8].IsValid()) {
        std::fprintf(stderr, "FAIL: m_RingTex[8] (dojo ring) not loaded\n");
        ++failures;
    }
    if (!game_work.m_RingTex[16].IsValid()) {
        std::fprintf(stderr, "FAIL: m_RingTex[16] (plain ring) not loaded\n");
        ++failures;
    }
    if (!GetIngamePopup(0x10)) {
        std::fprintf(stderr, "FAIL: IngamePopup 0x10 (NEW badge) not built\n");
        ++failures;
    }
    if (!MenuButton::GetSparkleRingTex().IsValid()) {
        std::fprintf(stderr, "FAIL: blurry_backing.tex (sparkle ring) not loaded\n");
        ++failures;
    }
    if (failures > 0) {
        h.Shutdown();
        return 1;
    }

    // Load Verdana for caption labels (optional; test still passes if absent).
    Mortar::FontCacheObjectTTF* verdanaFont = NULL;
    {
        FT_Library ftLib = Mortar::FontTTFRegistry::GetInstance().GetFTLibrary();
        if (ftLib) {
            const char* verdanaPath = "C:\\Windows\\Fonts\\verdana.ttf";
            verdanaFont = new Mortar::FontCacheObjectTTF(ftLib, verdanaPath, 9);
            if (!verdanaFont->IsValid()) {
                delete verdanaFont;
                verdanaFont = NULL;
                std::printf("[menubutton_render] WARN: verdana.ttf absent -- captions omitted\n");
            }
        } else {
            std::printf("[menubutton_render] WARN: FT_Library not ready -- captions omitted\n");
        }
    }

    // --- Build the 4 state buttons (HUD takes ownership on AddControl) ---
    MenuButton* btns[kNumButtons];
    const Vec3 hitBounds(kButtonSize, kButtonSize, 1.0f);
    const Mortar::Delegate0<void> noCb;   // never invoked (no touch input in this test)

    for (int i = 0; i < kNumButtons; ++i) {
        btns[i] = new MenuButton();
        btns[i]->Init(Vec3(kButtonX[i], kButtonY, 0.0f), noCb, -1, hitBounds, noCb);
        // Toggle-style buttons keep whatever layer the caller assigns
        // (MainScreen uses HUD_LAYER_BUTTONS for its sound/music toggles).
        btns[i]->m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;
        game_work.mHud->AddControl(btns[i]);
    }

    // Btn 0: NORMAL -- ring quad + curved 3-layer label (same recipe as the
    // real NEW GAME button: m_RingTex[3] + gradient m_RingColours[4]/[5]).
    btns[0]->m_Texture = game_work.m_RingTex[3];
    btns[0]->SetText("NEW GAME",
                     game_work.m_RingColours[4], game_work.m_RingColours[5],
                     42.0f, 12.0f, true, true);

    // Btn 1: PRESS-DIM -- identical to btn 0 except touch disabled, which
    // satisfies the Draw press-dim gate (m_FruitType<0 && m_bAcceptsTouch==0)
    // while Update keeps size == m_RestScale. Quad renders at RGB*0.5.
    btns[1]->m_Texture = game_work.m_RingTex[3];
    btns[1]->SetText("NEW GAME",
                     game_work.m_RingColours[4], game_work.m_RingColours[5],
                     42.0f, 12.0f, true, true);
    btns[1]->SetAcceptsTouch(false);

    // Btn 2: NEW badge armed. Timer is pinned to kNewBadgePin after every
    // Update inside FramePass (mid-swing bob).
    btns[2]->m_Texture = game_work.m_RingTex[8];
    btns[2]->SetNewSymbol(true);

    // Btn 3: loading sparkle ring armed. Timer pinned to kSparklePin.
    btns[3]->m_Texture = game_work.m_RingTex[16];
    btns[3]->SetLoadingSymbol(true);

    // --- Headless assertions (state wiring) ---
    if (!btns[2]->HasNewSymbol()) {
        std::fprintf(stderr, "FAIL: HasNewSymbol() false after SetNewSymbol(true)\n");
        ++failures;
    }
    if (!btns[3]->IsLoadingSymbol()) {
        std::fprintf(stderr, "FAIL: IsLoadingSymbol() false after SetLoadingSymbol(true)\n");
        ++failures;
    }
    if (btns[1]->AcceptsTouch()) {
        std::fprintf(stderr, "FAIL: btn1 AcceptsTouch() still true -- press-dim gate won't fire\n");
        ++failures;
    }
    if (!btns[0]->m_pLabelFg) {
        std::fprintf(stderr, "FAIL: btn0 m_pLabelFg null after SetText (TTF font missing?)\n");
        ++failures;
    }

    // --- Settle: 10 frames warms GL state + glyph atlases; Update copies
    //     m_RestScale into size for the toggle-style buttons. ---
    for (int frame = 0; frame < 10; ++frame) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) break;
        }
        FramePass(h, btns, NULL);
        SDL_GL_SwapWindow(h.window);
    }

    // size == m_RestScale after Update (toggle-path copy) -- btn0 quad at rest.
    if (btns[0]->size.x != btns[0]->m_RestScale.x ||
        btns[0]->size.y != btns[0]->m_RestScale.y) {
        std::fprintf(stderr, "FAIL: btn0 size (%.1f,%.1f) != m_RestScale (%.1f,%.1f) after settle\n",
                     btns[0]->size.x, btns[0]->size.y,
                     btns[0]->m_RestScale.x, btns[0]->m_RestScale.y);
        ++failures;
    }

    // --- Screenshot: one final pass with captions, capture BEFORE swap ---
    if (h.IsScreenshot()) {
        FramePass(h, btns, verdanaFont);

        int fw = 0, fh = 0;
        unsigned char* pixels = h.ReadPixels(&fw, &fh);
        int drawn = pixels ? CountNonBlack(pixels, fw, fh) : 0;
        std::free(pixels);
        if (drawn < MIN_DRAWN_PIXELS) {
            std::fprintf(stderr, "FAIL: only %d non-black pixels (< %d) -- nothing drew\n",
                         drawn, MIN_DRAWN_PIXELS);
            ++failures;
        } else {
            std::printf("[menubutton_render] drawnPixels=%d\n", drawn);
        }

        if (!h.ScreenshotPng("menubutton/states")) {
            std::fprintf(stderr, "FAIL: ScreenshotPng('menubutton/states') failed\n");
            ++failures;
        }
        SDL_GL_SwapWindow(h.window);
    }

    // --- Interactive mode: render loop until ESC or window-close ---
    if (h.IsInteractive()) {
        std::printf("[menubutton_render] entering interactive mode -- ESC to exit\n");
        bool running = true;
        while (running) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) { running = false; break; }
                if (ev.type == SDL_KEYDOWN &&
                    ev.key.keysym.sym == SDLK_ESCAPE) { running = false; break; }
            }
            if (!running) break;
            FramePass(h, btns, verdanaFont);
            SDL_GL_SwapWindow(h.window);
            SDL_Delay(16);
        }
        std::printf("[menubutton_render] interactive exit\n");
    }

    delete verdanaFont;
    verdanaFont = NULL;

    if (failures > 0) {
        std::fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    std::printf("PASS: menubutton_render OK\n");
    return h.Shutdown();
}
