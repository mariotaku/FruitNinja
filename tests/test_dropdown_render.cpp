// test_dropdown_render.cpp -- isolated render screenshot for the dead-code
// dropdown widget stack (ComboBox -> ListBox -> VerticalScroller). All three are
// dead code in v1.6.1 (nothing instantiates a ComboBox) but carry complete,
// faithful implementations.
//
// Usage: test_dropdown_render [--screenshot|--interactive|--headless]
//
// Renders:
//   * A COLLAPSED ComboBox -- bar (blank_dialog_box) + expand arrow + the header
//     label (Yellow) + the selected-item label.
//   * An EXPANDED view -- a ListBox with 8 items (> its 6 visible rows, so its
//     ctor creates a VerticalScroller and AddControl's it to the HUD). One row is
//     the committed selection (Blue), one is the hover row (RGB 0x50,0x96,0xFF),
//     the rest White. The VerticalScroller (track + top/bottom arrows + thumb at a
//     mid position) is drawn to its right.
//
// Output PNG (--screenshot mode):
//   tmp/test/screenshots/dropdown/dropdown.png
//
// NOTE: validates widget GEOMETRY + STATE (row count/colours, scroller present on
// overflow, thumb position), NOT the final shipped art. The faithful textures
// (blank_dialog_box.tex ships, but expand_arrow.tex / vbar.tex / vslider.tex /
// arrow.tex are NOT shipped in v1.6.1 -- see the widget .cpp DIFFERS notes), so
// the test injects in-memory PROCEDURALLY-DRAWN substitute textures via each
// widget's SetTexturesForTest hook. Widget positions are test-chosen (v1.6.1 never
// places these widgets); only relative geometry + state are meaningful here.
//
// C++11 / GCC 4.4.1 clean (host-only test TU; kept lambda/auto/range-for free).

#include "test_harness.h"
#include "hud/ComboBox.h"
#include "hud/ListBox.h"
#include "hud/VerticalScroller.h"
#include "hud/HUD.h"
#include "game/GameWork.h"
#include "asset/Texture.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "render/gl_funcs.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include <cstdio>
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <SDL.h>

// ---------------------------------------------------------------------------
// Solid-colour GL texture wrapped in a Texture2D_Bada (valid texId + dims;
// VerticalScroller::Draw reads GetHeight() to size its arrows/thumb).
// ---------------------------------------------------------------------------
static Mortar::SmartPtr<Mortar::Texture> MakeSolidTex(
    uint8_t r, uint8_t g, uint8_t b, uint8_t a, int w, int h)
{
    std::vector<uint8_t> px((size_t)w * (size_t)h * 4);
    for (int i = 0; i < w * h; ++i) {
        px[i * 4 + 0] = r;
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = b;
        px[i * 4 + 3] = a;
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
    glBindTexture(GL_TEXTURE_2D, 0);

    Mortar::Bada::Texture2D_Bada* t = new Mortar::Bada::Texture2D_Bada();
    t->m_TexId = id;
    t->SetDimensions(w, h);
    t->m_HasAlpha = (a != 255);
    return Mortar::SmartPtr<Mortar::Texture>(t);
}

// ---------------------------------------------------------------------------
// Procedural up-arrow (opaque triangle pointing up, transparent elsewhere). The
// bottom scroller arrow reuses this via the binary's U+V-flipped DrawQuadUnCached.
// ---------------------------------------------------------------------------
static Mortar::SmartPtr<Mortar::Texture> MakeArrowTex(
    uint8_t r, uint8_t g, uint8_t b, int w, int h)
{
    std::vector<uint8_t> px((size_t)w * (size_t)h * 4, 0);
    for (int y = 0; y < h; ++y) {
        // Row 0 = top (narrow tip); increasing y widens the triangle.
        float t = (float)y / (float)(h - 1);
        int half = (int)(t * (float)w * 0.5f);
        int cx = w / 2;
        for (int x = cx - half; x <= cx + half; ++x) {
            if (x < 0 || x >= w) continue;
            uint8_t* out = &px[((size_t)y * (size_t)w + (size_t)x) * 4];
            out[0] = r; out[1] = g; out[2] = b; out[3] = 255;
        }
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
    glBindTexture(GL_TEXTURE_2D, 0);

    Mortar::Bada::Texture2D_Bada* t = new Mortar::Bada::Texture2D_Bada();
    t->m_TexId = id;
    t->SetDimensions(w, h);
    t->m_HasAlpha = true;
    return Mortar::SmartPtr<Mortar::Texture>(t);
}

// ---------------------------------------------------------------------------
// Render pass: clear + ortho + draw all widgets. Caller swaps.
// ---------------------------------------------------------------------------
static void DrawPass(fn::TestHarness& h, ComboBox* combo, ListBox* list,
                     VerticalScroller* scroller)
{
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(h.window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager::GetInstance().BeginFrame();
    MatrixManager::GetInstance().SetupOrtho(
        160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

    float hudScale[3] = { 1.0f, 1.0f, 1.0f };

    combo->Draw(hudScale);
    list->Draw(hudScale);
    if (scroller) scroller->Draw(hudScale);
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "dropdown/dropdown");
    // Burn-in frames: let GameInitialise load fonts (pFontMain) before we draw labels.
    h.SetInitFrames(90);
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud null after boot\n");
        return 1;
    }

    // -----------------------------------------------------------------------
    // Substitute textures (real dropdown art is not shipped -- see header note).
    // Inject BEFORE drawing (widget ctors do not read textures, but the Draw
    // paths do). Distinct colours so each element reads clearly.
    // -----------------------------------------------------------------------
    Mortar::SmartPtr<Mortar::Texture> texBar   = MakeSolidTex(40, 40, 60, 255, 8, 8);   // bar bg (dark)
    Mortar::SmartPtr<Mortar::Texture> texArrow = MakeArrowTex(255, 210, 40, 32, 32);     // expand arrow (gold)
    Mortar::SmartPtr<Mortar::Texture> texRow   = MakeSolidTex(255, 255, 255, 255, 8, 8); // row bg (white -> row colour tints)
    Mortar::SmartPtr<Mortar::Texture> texTrack = MakeSolidTex(70, 70, 90, 255, 8, 8);    // scroller track
    Mortar::SmartPtr<Mortar::Texture> texThumb = MakeSolidTex(200, 200, 210, 255, 8, 8); // scroller thumb
    Mortar::SmartPtr<Mortar::Texture> texVArrow = MakeArrowTex(180, 180, 200, 24, 24);   // scroller arrows

    ComboBox::SetTexturesForTest(texBar, texArrow);
    ListBox::SetTexturesForTest(texRow);
    VerticalScroller::SetTexturesForTest(texTrack, texThumb, texVArrow);

    int failures = 0;

    {
        // ---- Collapsed ComboBox ----
        // DrawWidth = textScaleX*size.x = 120; DrawHeight = textScaleY*size.y = 36.
        std::vector<std::string> comboItems;
        comboItems.push_back(std::string("Easy"));
        comboItems.push_back(std::string("Normal"));
        comboItems.push_back(std::string("Hard"));
        ComboBox combo(Vec3(-40.0f, -150.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                       comboItems, /*defaultIdx*/ 1, "DIFFICULTY",
                       /*textFlag*/ 6, /*width*/ 20, /*scaleX*/ 120, /*scaleY*/ 36);

        if (combo.DrawWidth() != 120.0f || combo.DrawHeight() != 36.0f) {
            std::fprintf(stderr, "FAIL: ComboBox draw extents %.1f x %.1f (expected 120 x 36)\n",
                         (double)combo.DrawWidth(), (double)combo.DrawHeight());
            ++failures;
        }
        if (combo.SelectedIter() != &comboItems[1]) {
            std::fprintf(stderr, "FAIL: ComboBox selection not begin()+defaultIdx\n");
            ++failures;
        }
        // Default m_TextColour is black (Colour()); set white so the selected
        // label reads against the dark bar (test-only visual choice).
        combo.SetTextColour(Colour(255, 255, 255, 255));

        // ---- Expanded ListBox (8 items, 6 visible -> scroller exists) ----
        std::vector<std::string> listItems;
        listItems.push_back(std::string("Apple"));
        listItems.push_back(std::string("Banana"));
        listItems.push_back(std::string("Cherry"));
        listItems.push_back(std::string("Date"));
        listItems.push_back(std::string("Elder"));
        listItems.push_back(std::string("Fig"));
        listItems.push_back(std::string("Grape"));
        listItems.push_back(std::string("Kiwi"));

        // cellHeightParam=14, cellWidthParam=120, fontScaleParam=30 ->
        //   m_CellWidth=120, m_CellHeight=30, font size = 14 (cellHeightParam*size.x).
        ListBox list(Vec3(-40.0f, 30.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), listItems,
                     /*selIter*/ &listItems[2], /*visibleRows*/ 6,
                     /*cellHeightParam*/ 14, /*cellWidthParam*/ 120, /*fontScaleParam*/ 30);

        if (list.Scroller() == NULL) {
            std::fprintf(stderr, "FAIL: ListBox overflow (8>6) but no VerticalScroller created\n");
            ++failures;
        }
        if (list.CellWidth() != 120.0f || list.CellHeight() != 30.0f) {
            std::fprintf(stderr, "FAIL: ListBox cell %.1f x %.1f (expected 120 x 30)\n",
                         (double)list.CellWidth(), (double)list.CellHeight());
            ++failures;
        }

        VerticalScroller* scroller = list.Scroller();
        if (scroller) {
            // Scroller max = items - visibleRows = 2. Confirm the composition wiring.
            if (scroller->MaxValue() != 2) {
                std::fprintf(stderr, "FAIL: scroller maxValue %d (expected 2 = 8-6)\n",
                             (int)scroller->MaxValue());
                ++failures;
            }
            if (scroller->TotalRows() != 6) {
                std::fprintf(stderr, "FAIL: scroller totalRows %d (expected 6)\n",
                             (int)scroller->TotalRows());
                ++failures;
            }
            // Visual: mid scroll + hover/selection so all three row colours render.
            scroller->m_CurrentValue = 1;               // thumb mid-track (genuinely public -- see class header)
        }
        // Selection (Blue) and hover (light blue) rows within the visible window
        // (top row = begin() + m_CurrentValue = index 1).
        list.SetTopVisibleForTest(&listItems[2]);           // committed -> Blue
        list.SetHoverForTest(&listItems[4]);                // hovered   -> RGB(0x50,0x96,0xFF)
        list.SetTextColourForTest(Colour(20, 20, 30, 255)); // dark text on white/tinted rows

        // ListBox::GetSelected returns the committed row.
        if (list.GetSelected() != &listItems[2]) {
            std::fprintf(stderr, "FAIL: GetSelected() != committed row\n");
            ++failures;
        }

        // ---- Settle + screenshot ----
        for (int frame = 0; frame < 8; ++frame) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {}
            DrawPass(h, &combo, &list, scroller);
            SDL_GL_SwapWindow(h.window);
        }

        if (h.IsScreenshot()) {
            DrawPass(h, &combo, &list, scroller);
            if (!h.ScreenshotPng("dropdown/dropdown")) {
                std::fprintf(stderr, "FAIL: ScreenshotPng failed\n");
                ++failures;
            } else {
                std::printf("[dropdown] screenshot written\n");
            }
            SDL_GL_SwapWindow(h.window);
        }

        if (h.IsInteractive()) {
            bool running = true;
            while (running) {
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) {
                    if (ev.type == SDL_QUIT) { running = false; break; }
                    if (ev.type == SDL_KEYDOWN &&
                        ev.key.keysym.sym == SDLK_ESCAPE) { running = false; break; }
                }
                if (!running) break;
                DrawPass(h, &combo, &list, scroller);
                SDL_GL_SwapWindow(h.window);
                SDL_Delay(16);
            }
        }

        // The scroller is owned by game_work.mHud (ListBox ctor AddControl'd it);
        // the stack ListBox dtor does NOT delete it. Do not touch `scroller`
        // after this block.
    } // widgets destroyed while GL context still alive

    // Release the substitute textures (static-slot refs + local refs) while the GL
    // context is alive -- glDeleteTextures runs in the Texture2D_Bada dtor.
    ComboBox::UnloadContent();
    ListBox::UnloadContent();
    VerticalScroller::UnloadContent();
    texBar.SetNull();
    texArrow.SetNull();
    texRow.SetNull();
    texTrack.SetNull();
    texThumb.SetNull();
    texVArrow.SetNull();

    if (failures > 0) {
        std::fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    std::printf("PASS: dropdown_render OK\n");
    return h.Shutdown();
}
