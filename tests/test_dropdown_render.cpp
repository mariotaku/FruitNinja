// test_dropdown_render.cpp -- isolated render screenshot for the dead-code
// dropdown widget stack (ComboBox -> ListBox -> VerticalScroller). All three are
// dead code in v1.6.1 (nothing instantiates a ComboBox) but carry complete,
// faithful implementations.
//
// Usage: test_dropdown_render [--screenshot|--interactive|--headless]
//
// Renders the SAME widget in BOTH states, side by side:
//   * LEFT  -- a COLLAPSED ComboBox: header label (Yellow) + blank_dialog_box bar
//     + expand arrow + the currently-selected item's text. This is what the combo
//     looks like before it is tapped.
//   * RIGHT -- an EXPANDED ComboBox: the same collapsed bar, with the dropdown
//     ListBox attached directly below it (exactly where ComboBox::Update spawns it:
//     pos.y - m_DrawHeight - 1). The ListBox holds 8 items (> its 6 visible rows,
//     so its ctor creates a VerticalScroller and AddControl's it to the HUD). One
//     row is the committed selection (Blue), one is the hover row (RGB
//     0x50,0x96,0xFF), the rest White; the scroller (track + top/bottom arrows +
//     thumb at a mid position) sits to its right.
//
// The expanded ListBox is built with the SAME ctor args ComboBox::Update uses when
// it opens (selIter=combo selection, visibleRows=textFlag, cellHeightParam=14,
// cellWidthParam=128, fontScaleParam=16), so its rows are the ORIGINAL 16px item
// height -- identical to a real combo open, no enlargement.
//
// Output PNG (--screenshot mode):
//   tmp/test/screenshots/dropdown/dropdown.png
//
// NOTE: validates widget GEOMETRY + STATE (collapsed vs expanded, row
// count/colours, scroller present on overflow, thumb position), NOT the final
// shipped art. The faithful textures (blank_dialog_box.tex ships, but
// expand_arrow.tex / vbar.tex / vslider.tex / arrow.tex are NOT shipped in v1.6.1
// -- see the widget .cpp DIFFERS notes), so the test injects in-memory
// PROCEDURALLY-DRAWN substitute textures via each widget's SetTexturesForTest hook.
// Widget positions are test-chosen (v1.6.1 never places these widgets); only
// relative geometry + state are meaningful here. Item/label strings are UPPERCASE
// because font_fruit_ninja.fnt carries no lowercase glyphs.
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

// Placeholder-art texture makers (MakeSolidTex / MakeArrowTex + SDF helpers)
// are shared with test_settings_widgets_render.cpp and
// test_settings_interactive.cpp -- see the header for details.
#include "widget_placeholder_art.h"
using namespace fn_widget_art;

// ---------------------------------------------------------------------------
// Render pass: clear + ortho + draw both combos (collapsed + expanded) and the
// expanded combo's dropped ListBox + scroller. Caller swaps.
// ---------------------------------------------------------------------------
static void DrawPass(fn::TestHarness& h, ComboBox* comboCollapsed,
                     ComboBox* comboExpanded, ListBox* list,
                     VerticalScroller* scroller)
{
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(h.window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager::GetInstance().BeginFrame();
    MatrixManager::GetInstance().SetupOrtho(
        160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

    float hudScale[3] = { 1.0f, 1.0f, 1.0f };

    comboCollapsed->Draw(hudScale);
    comboExpanded->Draw(hudScale);
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
    Mortar::SmartPtr<Mortar::Texture> texArrow = MakeArrowTex(255, 210, 40, 32, 32, /*pointDown*/ true); // expand arrow (gold, points down)
    Mortar::SmartPtr<Mortar::Texture> texRow   = MakeSolidTex(255, 255, 255, 255, 8, 8); // row bg (white -> row colour tints)
    Mortar::SmartPtr<Mortar::Texture> texTrack = MakeSolidTex(70, 70, 90, 255, 8, 8);    // scroller track
    Mortar::SmartPtr<Mortar::Texture> texThumb = MakeSolidTex(200, 200, 210, 255, 8, 8); // scroller thumb
    Mortar::SmartPtr<Mortar::Texture> texVArrow = MakeArrowTex(180, 180, 200, 24, 24);   // scroller arrows

    ComboBox::SetTexturesForTest(texBar, texArrow);
    ListBox::SetTexturesForTest(texRow);
    VerticalScroller::SetTexturesForTest(texTrack, texThumb, texVArrow);

    int failures = 0;

    {
        // Shared item model for both combos (8 items -> the expanded list overflows
        // its 6 visible rows and grows a scroller). UPPERCASE: the game font has no
        // lowercase glyphs.
        std::vector<std::string> items;
        items.push_back(std::string("APPLE"));
        items.push_back(std::string("BANANA"));
        items.push_back(std::string("CHERRY"));
        items.push_back(std::string("DATE"));
        items.push_back(std::string("ELDER"));
        items.push_back(std::string("FIG"));
        items.push_back(std::string("GRAPE"));
        items.push_back(std::string("KIWI"));

        // Bar geometry shared by both combos: DrawWidth = scaleX*size.x = 120,
        // DrawHeight = scaleY*size.y = 55 (unified to the checkbox box height).
        const uint16_t kScaleX = 120, kScaleY = 55;
        const uint8_t  kTextFlag = 6;   // -> the spawned ListBox's visibleRows

        // ---- LEFT: collapsed ComboBox (defaultIdx 1 -> "BANANA") ----
        ComboBox comboCollapsed(Vec3(-115.0f, 110.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                                items, /*defaultIdx*/ 1, "COLLAPSED",
                                kTextFlag, /*width*/ 20, kScaleX, kScaleY);
        comboCollapsed.SetTextColour(Colour(255, 255, 255, 255));

        // ---- RIGHT: expanded ComboBox (defaultIdx 2 -> "CHERRY") ----
        ComboBox comboExpanded(Vec3(70.0f, 110.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                               items, /*defaultIdx*/ 2, "EXPANDED",
                               kTextFlag, /*width*/ 20, kScaleX, kScaleY);
        comboExpanded.SetTextColour(Colour(255, 255, 255, 255));

        if (comboExpanded.DrawWidth() != 120.0f || comboExpanded.DrawHeight() != 55.0f) {
            std::fprintf(stderr, "FAIL: ComboBox draw extents %.1f x %.1f (expected 120 x 55)\n",
                         (double)comboExpanded.DrawWidth(), (double)comboExpanded.DrawHeight());
            ++failures;
        }

        // ---- The dropdown ListBox the expanded combo drops below its bar ----
        // Reproduces the EXACT ListBox ComboBox::Update spawns on tap: position
        // (pos.x, pos.y - m_DrawHeight - 1, -1) and ctor args
        // (selIter, visibleRows=textFlag, cellHeightParam=14, cellWidthParam=128,
        // fontScaleParam=16) -> m_CellWidth=128, m_CellHeight=16, font size 14.
        // These are the original item dimensions (no enlargement).
        Vec3 lbPos(comboExpanded.pos.x,
                   (comboExpanded.pos.y - comboExpanded.DrawHeight()) - 1.0f, -1.0f);
        ListBox list(lbPos, Vec3(1.0f, 1.0f, 1.0f), items,
                     /*selIter*/ comboExpanded.SelectedIter(), /*visibleRows*/ kTextFlag,
                     /*cellHeightParam*/ 14, /*cellWidthParam*/ 128, /*fontScaleParam*/ 16);

        if (list.Scroller() == NULL) {
            std::fprintf(stderr, "FAIL: ListBox overflow (8>6) but no VerticalScroller created\n");
            ++failures;
        }
        if (list.CellWidth() != 128.0f || list.CellHeight() != 16.0f) {
            std::fprintf(stderr, "FAIL: ListBox cell %.1f x %.1f (expected 128 x 16)\n",
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
        list.SetTopVisibleForTest(&items[2]);           // committed -> Blue (matches combo selection "CHERRY")
        list.SetHoverForTest(&items[4]);                // hovered   -> RGB(0x50,0x96,0xFF)
        list.SetTextColourForTest(Colour(20, 20, 30, 255)); // dark text on white/tinted rows

        // ListBox::GetSelected returns the committed row.
        if (list.GetSelected() != &items[2]) {
            std::fprintf(stderr, "FAIL: GetSelected() != committed row\n");
            ++failures;
        }

        // ---- Settle + screenshot ----
        for (int frame = 0; frame < 8; ++frame) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {}
            DrawPass(h, &comboCollapsed, &comboExpanded, &list, scroller);
            SDL_GL_SwapWindow(h.window);
        }

        if (h.IsScreenshot()) {
            DrawPass(h, &comboCollapsed, &comboExpanded, &list, scroller);
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
                DrawPass(h, &comboCollapsed, &comboExpanded, &list, scroller);
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
