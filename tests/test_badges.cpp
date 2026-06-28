// test_badges -- isolated screenshot test for IngamePopup NEW and SELECTED badges.
//
// Usage: test_badges [--screenshot|--interactive|--headless]
//
// Renders each badge centered on a dark background at its real in-game draw
// scale (NEW 0.8, SELECTED 0.5) so the border-vs-text alignment matches what
// the game actually shows. (An earlier 4x "inspection" scale distorted the
// layout -- the center-flag and baseline terms scale with it -- and made the
// text look grossly off when it is fine at game scale.)
//
// Output PNGs (--screenshot mode):
//   tmp/test/screenshots/badges/new.png      -- type 0x10, gold "NEW" badge
//   tmp/test/screenshots/badges/selected.png -- type 0x11, green "SELECTED" badge
//
// How the badges are constructed (from IngamePopup ctor @0x0016dbac):
//   type 0x10 (NEW):
//     - BakedStringBox: 16pt gangofchinese.ttf, boxW=44, boxH=14, align=0x0d
//       SetMetallicGradient(gold colours), text = LSTR_MENU_TEXTURE_09 "NEW"
//     - Texture: new_outline.tex (border), scale Vec3(0.8, 0.8, 0), pos Vec3(0, textY, 0)
//     - m_VerticalOffset = -7.0f (slight CCW rotation)
//   type 0x11 (SELECTED):
//     - BakedStringBox: 17pt gangofchinese.ttf, boxW=118, boxH=18, align=0x0d
//       SetColour(Colour(43, 176, 5)), text = LSTR_MENU_TEXTURE_53 "SELECTED"
//     - Texture: selected_outline.tex (border), scale Vec3(1.0, 1.0, 0), pos Vec3(0, 0, 0)
//     - m_VerticalOffset = 20.0f
//
// Alignment note: in IngamePopup::Draw, text finalPos uses scale*textPos while
// texture pos2 uses textPos*scale*texScale -- a potential divergence when textY
// is non-zero (Korean). Also, BakedStringBox::Draw uses m_VerticalOffset as a
// degrees rotation, while the texture uses SinIdx/CosIdx(m_VerticalOffset*182)
// for the RotZ -- both paths apply the same rotation but at different code sites.

#include "test_harness.h"
#include "hud/IngamePopup.h"
#include "engine/math/Vec3.h"
#include <cstdio>

// Render popup at (posX, posY) with the given scale for nFrames.
// Each frame: clear (BeginFrame) -> ortho setup -> badge draw -> swap.
static void RunBadgeFrames(fn::TestHarness& h, IngamePopup* popup,
                            float scale, float posX, float posY, int nFrames) {
    for (int i = 0; i < nFrames; ++i) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {}

        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(h.window, &ww, &wh);
        glViewport(0, 0, ww, wh);

        Mortar::DisplayManager::GetInstance().BeginFrame();
        MatrixManager::GetInstance().SetupOrtho(
            160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

        Vec3 pos(posX, posY, 0.0f);
        popup->Draw(scale, &pos);

        SDL_GL_SwapWindow(h.window);
    }
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "badges");
    // 120 burn-in frames: allows GameInitialise -> PreloadRings -> BuildAllPopups
    // to run so fonts (gangofchinese.ttf) and textures are loaded before we draw.
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: mHud null after boot\n");
        return 1;
    }

    // Idempotent: BuildAllPopups was called by PreloadRings during GameInitialise.
    // Explicit call here guards against any boot path that skips PreloadRings.
    BuildAllPopups();

    // ------------------------------------------------------------------
    // NEW badge (type 0x10 -- gold metallic-gradient "NEW" + new_outline.tex border)
    // In-game draw scale: 0.8f (ShopListItem::DrawFloatingText) -- render at the real
    // game scale; a 4x "inspection" scale distorts the layout (the center-flag and
    // baseline terms are scaled), which is not representative of how it draws in-game.
    // m_VerticalOffset = -7.0f -> slight CCW tilt.
    // ------------------------------------------------------------------
    {
        IngamePopup* popup = GetIngamePopup(0x10);
        if (!popup) {
            std::fprintf(stderr, "FAIL: GetIngamePopup(0x10) returned null\n");
            return 2;
        }
        std::printf("[badges/new] textBoxes=%d textures=%d m_VerticalOffset=%.1f\n",
                    (int)popup->m_TextBoxes.size(),
                    (int)popup->m_Textures.size(),
                    popup->m_VerticalOffset);

        // 3 frames: 2 warm-up (GL pipeline primed) + 1 stable for screenshot.
        RunBadgeFrames(h, popup, 0.8f, 0.0f, 0.0f, 3);

        if (h.IsScreenshot()) {
            if (!h.ScreenshotPng("badges/new")) return 3;
        }
        std::printf("PASS: badges/new rendered\n");
    }

    // ------------------------------------------------------------------
    // SELECTED badge (type 0x11 -- green "SELECTED" text + selected_outline.tex border)
    // In-game draw scale: 0.5f (ShopListItem::DrawFloatingText) -- real game scale (see NEW note).
    // m_VerticalOffset = 20.0f -> visible CW tilt.
    // ------------------------------------------------------------------
    {
        IngamePopup* popup = GetIngamePopup(0x11);
        if (!popup) {
            std::fprintf(stderr, "FAIL: GetIngamePopup(0x11) returned null\n");
            return 4;
        }
        std::printf("[badges/selected] textBoxes=%d textures=%d m_VerticalOffset=%.1f\n",
                    (int)popup->m_TextBoxes.size(),
                    (int)popup->m_Textures.size(),
                    popup->m_VerticalOffset);

        RunBadgeFrames(h, popup, 0.5f, 0.0f, 0.0f, 3);

        if (h.IsScreenshot()) {
            if (!h.ScreenshotPng("badges/selected")) return 5;
        }
        std::printf("PASS: badges/selected rendered\n");
    }

    return h.Shutdown();
}
