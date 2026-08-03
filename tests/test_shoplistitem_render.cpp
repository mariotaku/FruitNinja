// test_shoplistitem_render.cpp -- isolated render screenshot for ShopListItem rows.
//
// Usage: test_shoplistitem_render [--screenshot] [--interactive] [--lang=<name|flag>]
//
// Default (no flags): headless assertions (box-build check). Passes via ctest -E screenshot.
// --screenshot: renders a 5-row column and writes:
//   tmp/test/screenshots/shoplistitem/<lang>.png
//
// Rows rendered (top to bottom on screen, world Y=+120 down to -120):
//   Row 0 (Y=+120): Short CJK "raw_begin_blade"     BLADE, unlocked, seen -- baseline
//   Row 1 (Y= +60): Long CJK "shiny_red_blade"      BLADE, unlocked, seen -- #276 shrink-to-fit
//   Row 2 (Y=   0): English "Locked Blade"           BLADE, LOCKED (m_Cost=10), seen
//   Row 3 (Y= -60): "New Blade"                      BLADE, unlocked, m_bSeen=0 -> NEW badge
//   Row 4 (Y=-120): "Equipped Blade"                 BLADE, unlocked, m_SelectedAlpha=1 -> SELECTED badge
//
// DrawFloatingText live:
//   Row 3: IngamePopup type 0x10 (NEW badge) when m_NewItemAlpha > 0.
//   Row 4: IngamePopup type 0x11 (SELECTED badge) when m_SelectedAlpha > 0.
// DrawDescription skipped: m_pShopScreen=null -- DrawDescription early-returns safely.
// DrawDividers partially: ShopScreen::s_TexScratch not loaded (ShopScreen::Init not called),
//   so the divider quads are not drawn (guarded by .IsValid() inside DrawDividers).
//
// C++11 / GCC 4.4.1 clean: no lambdas, no auto, no range-for, no enum class.

#include "test_harness.h"
#include "hud/ShopListItem.h"
#include "game/ItemInfo.h"
#include "game/GameWork.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "render/BakedStringBox.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include "math/_Vector2.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include <cstdio>
#include <cstring>
#include <SDL.h>

// ---------------------------------------------------------------------------
// Language suffix table + shot-label builder (mirrors test_shopscreen.cpp)
// ---------------------------------------------------------------------------

static int g_LangFlag = -1;

static const char* const kLangShort[] = {
    "en", "en_uk", "fr", "es", "de", "it", "nl", "sv", "da", "nb",
    "fi", "ko", "ja", "zh", "zh_hant", "es_419", "pl", "pt", "pt_br", "ru",
    "ar", "dbg"
};
static const int kLangShortCount = 22;

static void BuildShotLabel(char* out, size_t outSize, int langFlag) {
    if (langFlag >= 0 && langFlag < kLangShortCount) {
        snprintf(out, outSize, "shoplistitem/%s", kLangShort[langFlag]);
    } else {
        snprintf(out, outSize, "shoplistitem/default");
    }
}

// ---------------------------------------------------------------------------
// UTF-8 title strings used for CJK rows.
// Hardcoded byte literals ensure correct encoding regardless of source charset.
// ---------------------------------------------------------------------------

// "原始刀刃" -- 4 CJK characters (short-name baseline)
static const char kTitleShortCJK[] = {
    '\xe5', '\x8e', '\x9f',  // 原 U+539F
    '\xe5', '\xa7', '\x8b',  // 始 U+59CB
    '\xe5', '\x88', '\x80',  // 刀 U+5200
    '\xe5', '\x88', '\x83',  // 刃 U+5203
    '\0'
};

// "闪闪发光的红色刀刃" -- 9 CJK characters (long-name; exercises #276 auto-shrink-to-fit)
static const char kTitleLongCJK[] = {
    '\xe9', '\x97', '\xaa',  // 闪 U+95EA
    '\xe9', '\x97', '\xaa',  // 闪 U+95EA
    '\xe5', '\x8f', '\x91',  // 发 U+53D1
    '\xe5', '\x85', '\x89',  // 光 U+5149
    '\xe7', '\x9a', '\x84',  // 的 U+7684
    '\xe7', '\xba', '\xa2',  // 红 U+7EA2
    '\xe8', '\x89', '\xb2',  // 色 U+8272
    '\xe5', '\x88', '\x80',  // 刀 U+5200
    '\xe5', '\x88', '\x83',  // 刃 U+5203
    '\0'
};

// ---------------------------------------------------------------------------
// Row layout constants
// ---------------------------------------------------------------------------

static const int kNumRows = 5;

// Item X position: 0 = horizontal center of screen (world X range: -240..+240).
static const float kItemX = 0.0f;

// Item Y positions: +120=near top, -120=near bottom (world Y range: -160..+160).
// 60-unit vertical spacing; items are 80 units tall so adjacent items slightly overlap
// at their divider edges. Text content (title at Y+16, category at Y-10) does not overlap.
static const float kRowY[5] = { 120.0f, 60.0f, 0.0f, -60.0f, -120.0f };

// Verdana caption text per row (ASCII-only -- no Unicode in printf/log strings).
static const char* const kRowCaptions[5] = {
    "SHORT CJK (baseline)",
    "LONG CJK (#276 shrink)",
    "ENGLISH / LOCKED",
    "NEW badge (bSeen=0)",
    "SELECTED badge"
};

// ---------------------------------------------------------------------------
// DrawPass -- render all 5 items + optional Verdana captions into the back
// buffer. Does NOT call SwapWindow; caller swaps after this function returns
// (so ScreenshotPng can be called between render and swap).
// ---------------------------------------------------------------------------
static void DrawPass(
    fn::TestHarness& h,
    ShopListItem* items,
    Mortar::FontCacheObjectTTF* verdanaFont)
{
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(h.window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    // Clear + set up the same ortho the HUD uses.
    Mortar::DisplayManager::GetInstance().BeginFrame();
    MatrixManager::GetInstance().SetupOrtho(
        160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

    // Draw all item rows by calling NewDraw() directly, bypassing Draw()'s
    // dispatch. The rows were marked onscreen at setup so NewDraw's own
    // m_bOnscreen head gate passes.
    for (int i = 0; i < kNumRows; ++i) {
        items[i].NewDraw();
    }

    // Draw Verdana caption labels (right side: visual center at X=+190, same Y as item).
    // Box: 90 world-units wide x 16 tall. align=0x0f (H-center + V-center), flag=1
    // (SetTranslation centers on pos). caption spans X=+145..+235, within +240 right edge.
    if (verdanaFont) {
        const int boxW = 90;
        const int boxH = 16;
        const float fontSize = 8.0f;
        // Visual center X for captions: icon reaches X=+127, caption starts X=+145 -> center +190.
        const float captionCX = 145.0f + (float)boxW * 0.5f;  // = 190.0f

        for (int i = 0; i < kNumRows; ++i) {
            // align=0x0f: H-center (bits 0-1=3) + V-center (bits 2-3=3); flag=1 centers on pos.
            Mortar::BakedStringBox lbl(verdanaFont, fontSize, boxW, boxH, (Mortar::ALIGNMENT_TYPE)0x0f, 1, 0);
            lbl.SetText(kRowCaptions[i]);
            lbl.SetColour(Colour(200, 200, 200, 255), 0);
            _Vector3<float> captionPos(captionCX, kRowY[i], 0.0f);
            lbl.SetTranslation(captionPos, 1);
            lbl.Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    // Parse --lang= here for label composition; ParseFlags() also parses it
    // for the locale-apply path inside Init() (two passes, harmless).
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--lang=", 7) == 0) {
            g_LangFlag = ParseLanguageArg(argv[i] + 7);
        }
    }

    char shotLabel[256];
    BuildShotLabel(shotLabel, sizeof(shotLabel), g_LangFlag);

    fn::TestHarness h(argc, argv, shotLabel);
    // 60 burn-in frames: gangofchinese.ttf loads lazily on first MenuButton/BSButton
    // creation, which happens during MainScreen init inside game.runFrames().
    h.SetInitFrames(60);
    if (!h.ParseFlags()) return 1;
    // Component-isolation: clears the HUD so only our items render on the background.
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud null after init\n");
        return 1;
    }

    // Load Verdana for caption labels (optional; test still passes if absent).
    Mortar::FontCacheObjectTTF* verdanaFont = NULL;
    {
        const char* verdanaPath = "C:\\Windows\\Fonts\\verdana.ttf";
        verdanaFont = new Mortar::FontCacheObjectTTF(verdanaPath, 9);
        if (!verdanaFont->IsValid()) {
            delete verdanaFont;
            verdanaFont = NULL;
            printf("[shoplistitem_render] WARN: verdana.ttf absent -- captions omitted\n");
        } else {
            printf("[shoplistitem_render] Verdana caption font: OK\n");
        }
    }

    int failures = 0;

    // Scope items + infos so BakedStringBox GL resources are destroyed while
    // the GL context is still alive (before h.Shutdown() tears it down).
    {
        // -----------------------------------------------------------------------
        // Row 0: Short CJK name, BLADE, unlocked, seen -- baseline.
        // title = "原始刀刃", icon = item_originalblade.tex
        // -----------------------------------------------------------------------
        ItemInfo info0;
        info0.m_Type            = (int8_t)ITEM_TYPE_BLADE;
        info0.m_Cost            = 0;
        info0.m_pTitle          = (char*)kTitleShortCJK;
        info0.m_pDescText       = (char*)"Short CJK blade.";
        info0.m_pLockedText     = NULL;
        info0.m_pProgressFmt    = NULL;
        info0.m_pTotalStatKey   = NULL;
        info0.m_CountDownFrom   = 0;
        info0.m_pTextureName    = (char*)"item_originalblade";
        info0.m_bSeen           = 1;
        info0.m_RequirementType = (int8_t)0;

        // -----------------------------------------------------------------------
        // Row 1: Long CJK name, BLADE, unlocked, seen -- #276 auto-shrink-to-fit.
        // title = "闪闪发光的红色刀刃", icon = item_shiney_red_blade.tex
        // BakedStringBox must fit the 9-char name in one line without left-clipping.
        // -----------------------------------------------------------------------
        ItemInfo info1;
        info1.m_Type            = (int8_t)ITEM_TYPE_BLADE;
        info1.m_Cost            = 0;
        info1.m_pTitle          = (char*)kTitleLongCJK;
        info1.m_pDescText       = (char*)"Long CJK; #276 auto-shrink-to-fit test.";
        info1.m_pLockedText     = NULL;
        info1.m_pProgressFmt    = NULL;
        info1.m_pTotalStatKey   = NULL;
        info1.m_CountDownFrom   = 0;
        info1.m_pTextureName    = (char*)"item_shiney_red_blade";
        info1.m_bSeen           = 1;
        info1.m_RequirementType = (int8_t)0;

        // -----------------------------------------------------------------------
        // Row 2: English name, BLADE, LOCKED (m_Cost=10), seen.
        // IsLocked() = (m_Cost > 0) -> NewDraw uses grey itemColour (200,200,200).
        // No icon texture (locked items show locked_stroke.tex only if ShopScreen
        // static textures are loaded; not the case in this isolated test).
        // -----------------------------------------------------------------------
        ItemInfo info2;
        info2.m_Type            = (int8_t)ITEM_TYPE_BLADE;
        info2.m_Cost            = 10;
        info2.m_pTitle          = (char*)"Locked Blade";
        info2.m_pDescText       = (char*)"This blade is locked.";
        info2.m_pLockedText     = (char*)"Buy for 10 coins.";
        info2.m_pProgressFmt    = NULL;
        info2.m_pTotalStatKey   = NULL;
        info2.m_CountDownFrom   = 0;
        info2.m_pTextureName    = NULL;
        info2.m_bSeen           = 1;
        info2.m_RequirementType = (int8_t)0;

        // -----------------------------------------------------------------------
        // Row 3: "New Blade", BLADE, unlocked, m_bSeen=0.
        // Create() detects m_bSeen==0 and sets m_NewItemAlpha=1.0f immediately.
        // DrawFloatingText draws IngamePopup 0x10 (NEW badge) when m_NewItemAlpha>0.
        // icon = item_discoblade.tex
        // -----------------------------------------------------------------------
        ItemInfo info3;
        info3.m_Type            = (int8_t)ITEM_TYPE_BLADE;
        info3.m_Cost            = 0;
        info3.m_pTitle          = (char*)"New Blade";
        info3.m_pDescText       = (char*)"Newly available.";
        info3.m_pLockedText     = NULL;
        info3.m_pProgressFmt    = NULL;
        info3.m_pTotalStatKey   = NULL;
        info3.m_CountDownFrom   = 0;
        info3.m_pTextureName    = (char*)"item_discoblade";
        info3.m_bSeen           = 0;
        info3.m_RequirementType = (int8_t)0;

        // -----------------------------------------------------------------------
        // Row 4: "Equipped Blade", BLADE, unlocked, seen.
        // m_SelectedAlpha is forced to 1.0f manually (item is not registered in
        // ItemManager, so Move() would ramp it toward 0; we reset after each Move()).
        // DrawFloatingText draws IngamePopup 0x11 (SELECTED badge) when alpha>0.
        // icon = item_american_blade.tex
        // -----------------------------------------------------------------------
        ItemInfo info4;
        info4.m_Type            = (int8_t)ITEM_TYPE_BLADE;
        info4.m_Cost            = 0;
        info4.m_pTitle          = (char*)"Equipped Blade";
        info4.m_pDescText       = (char*)"Currently equipped.";
        info4.m_pLockedText     = NULL;
        info4.m_pProgressFmt    = NULL;
        info4.m_pTotalStatKey   = NULL;
        info4.m_CountDownFrom   = 0;
        info4.m_pTextureName    = (char*)"item_american_blade";
        info4.m_bSeen           = 1;
        info4.m_RequirementType = (int8_t)0;

        // Construct + Create all items. pShopScreen=NULL: DrawDescription early-returns.
        ShopListItem items[kNumRows];
        items[0].Create(&info0, NULL);
        items[1].Create(&info1, NULL);
        items[2].Create(&info2, NULL);
        items[3].Create(&info3, NULL);
        items[4].Create(&info4, NULL);

        // Set positions via Move() -- writes pos.x/y/z and m_IconPos (icon offset).
        // game_work.dt is ~1/60 from the last burn-in frame so alpha ramps apply one step:
        //   items[3].m_NewItemAlpha was set to 1.0f by Create() (bSeen=0); stays at 1.0f.
        //   items[4].m_SelectedAlpha starts at 0 (not in ItemManager); decrements slightly.
        for (int i = 0; i < kNumRows; ++i) {
            items[i].Move(_Vector3<float>(kItemX, kRowY[i], 0.0f));
            // NewDraw's head gate is m_bOnscreen (+0x2D), matching the binary
            // @0x001b5910. Normally ScrollingMenu::Update sets it; this fixture
            // drives NewDraw directly, so mark the rows onscreen by hand.
            items[i].SetOnscreen(true);
        }

        // Force SELECTED alpha on row 4 after Move() decremented it.
        // Reset is also applied each frame of the settle loop.
        items[4].m_SelectedAlpha = 1.0f;

        // -----------------------------------------------------------------------
        // Headless assertions: verify box build. Skip in screenshot mode (avoids
        // drawing stub items into the pre-screenshot framebuffer -- mirrors the
        // guard in test_shopscreen.cpp for the same reason).
        // -----------------------------------------------------------------------
        if (!h.IsScreenshot()) {
            fprintf(stdout, "[shoplistitem_render] headless: box-creation checks\n");
            if (!game_work.m_pTTFFontMain) {
                fprintf(stderr,
                    "WARN: m_pTTFFontMain null -- box assertions skipped (no TTF font)\n");
            } else {
                // Row 0: short CJK baseline.
                items[0].NewDraw();
                if (!items[0].m_pBox0) {
                    fprintf(stderr, "FAIL: m_pBox0 null after NewDraw (row 0, short CJK)\n");
                    ++failures;
                } else {
                    printf("PASS: m_pBox0 (title, row 0) built\n");
                }
                if (!items[0].m_pBox1) {
                    fprintf(stderr, "FAIL: m_pBox1 null after NewDraw (row 0)\n");
                    ++failures;
                } else {
                    printf("PASS: m_pBox1 (category, row 0) built\n");
                }

                // Row 1: long CJK -- box0 must build without crash (#276 path).
                items[1].NewDraw();
                if (!items[1].m_pBox0) {
                    fprintf(stderr,
                        "FAIL: m_pBox0 null after NewDraw (row 1, long CJK -- #276 path)\n");
                    ++failures;
                } else {
                    printf("PASS: m_pBox0 (title, row 1, long CJK) built\n");
                }
            }
        }

        // -----------------------------------------------------------------------
        // Settle loop (10 frames): warms up GL state (shaders, vertex buffers) and
        // builds glyph atlas textures for all 5 items. Uses DrawPass() without
        // Verdana captions (captions add per-frame allocation overhead; not needed
        // for warm-up). SELECTED alpha is reset each frame to prevent decay.
        // -----------------------------------------------------------------------
        for (int frame = 0; frame < 10; ++frame) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) break;
            }
            items[4].m_SelectedAlpha = 1.0f;
            DrawPass(h, items, NULL);
            SDL_GL_SwapWindow(h.window);
        }

        // -----------------------------------------------------------------------
        // Screenshot: one final render pass with Verdana captions. Capture BEFORE
        // SwapWindow so glReadPixels reads the just-rendered back buffer.
        // -----------------------------------------------------------------------
        if (h.IsScreenshot()) {
            items[4].m_SelectedAlpha = 1.0f;
            DrawPass(h, items, verdanaFont);
            if (!h.ScreenshotPng(shotLabel)) {
                fprintf(stderr, "FAIL: ScreenshotPng('%s') failed\n", shotLabel);
                ++failures;
            } else {
                printf("[%s] screenshot written\n", shotLabel);
            }
            SDL_GL_SwapWindow(h.window);
        }

        // -----------------------------------------------------------------------
        // Interactive mode: render loop until ESC or window-close.
        // -----------------------------------------------------------------------
        if (h.IsInteractive()) {
            printf("[shoplistitem_render] entering interactive mode -- ESC to exit\n");
            bool running = true;
            while (running) {
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) {
                    if (ev.type == SDL_QUIT) { running = false; break; }
                    if (ev.type == SDL_KEYDOWN &&
                        ev.key.keysym.sym == SDLK_ESCAPE) { running = false; break; }
                }
                if (!running) break;
                items[4].m_SelectedAlpha = 1.0f;
                DrawPass(h, items, verdanaFont);
                SDL_GL_SwapWindow(h.window);
                SDL_Delay(16);
            }
            printf("[shoplistitem_render] interactive exit\n");
        }

        // -----------------------------------------------------------------------
        // Cleanup: null out string literals before ItemInfo dtors run.
        // ItemInfo::~ItemInfo() calls free() on all m_p* string fields.
        // Literals are NOT heap-allocated; free(non-heap) corrupts the allocator.
        // The BakedStringBox already copied the text via SetText(), so nulling is safe.
        // -----------------------------------------------------------------------
        info0.m_pName = NULL; info0.m_pTitle = NULL; info0.m_pDescText = NULL;
        info0.m_pLockedText = NULL; info0.m_pProgressFmt = NULL;
        info0.m_pTotalStatKey = NULL; info0.m_pTextureName = NULL;

        info1.m_pName = NULL; info1.m_pTitle = NULL; info1.m_pDescText = NULL;
        info1.m_pLockedText = NULL; info1.m_pProgressFmt = NULL;
        info1.m_pTotalStatKey = NULL; info1.m_pTextureName = NULL;

        info2.m_pName = NULL; info2.m_pTitle = NULL; info2.m_pDescText = NULL;
        info2.m_pLockedText = NULL; info2.m_pProgressFmt = NULL;
        info2.m_pTotalStatKey = NULL; info2.m_pTextureName = NULL;

        info3.m_pName = NULL; info3.m_pTitle = NULL; info3.m_pDescText = NULL;
        info3.m_pLockedText = NULL; info3.m_pProgressFmt = NULL;
        info3.m_pTotalStatKey = NULL; info3.m_pTextureName = NULL;

        info4.m_pName = NULL; info4.m_pTitle = NULL; info4.m_pDescText = NULL;
        info4.m_pLockedText = NULL; info4.m_pProgressFmt = NULL;
        info4.m_pTotalStatKey = NULL; info4.m_pTextureName = NULL;

    } // items[5] and info* destroyed here; GL context still live.

    delete verdanaFont;
    verdanaFont = NULL;

    if (failures > 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    printf("PASS: shoplistitem_render OK\n");
    return h.Shutdown();
}
