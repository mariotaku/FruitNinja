#ifndef FN_HUD_LAYER_H
#define FN_HUD_LAYER_H

#include <cstdint>

//
// HUDControl::m_LayerFlags bit semantics.
//
// Catalogued from binary RE (re-analyst), re-verified against v1.6.1 GameDraw
// @0x001cd720 for task #35 (game-over text popping through the bomb-hit flash
// on quit). HUD::Draw(layerMask) iterates active HUDControls and tests
// `(layerMask & m_LayerFlags) != 0`. GameDraw invokes HUD::Draw eight times in
// this chronological order, interleaved with fixed (non-layer-dispatched) draws:
//
//   1.  HUD::BeginDraw(dt)
//   2.  HUD::Draw(0x40)   — menu fruit backdrop
//   3.  SplatEntity::DrawActiveSplats()         (fixed)
//   4.  Fruit::DrawShadows()                    (fixed)
//   5.  SlashEntity::PreDraw()                  (fixed)
//   6.  BombBlast::DrawActiveBlasts()           (fixed)
//   7.  BombFlash::DrawActiveFlashes()          (fixed)
//   8.  HUD::Draw(0x80)   — post-actor HUD (cards / buttons / keyboard)
//   8a. FruitRay::DrawRays()                    (fixed)
//   8b. DrawSlices(dt, pass=1)                  — slash trails, modelIdx==3 only
//                                                 (the super-fruit slice model)
//   9.  pm.Draw(-1)                             — particles, background tier
//   10. ActorList draw loop (16 actors, vtable +0x34 SecondaryDraw)
//   11. pm.Draw(0)                              — particles, mid tier
//   12. DrawSlices(dt, pass=0)                  — slash trails, modelIdx!=3
//   13. HUD::Draw(0x01)   — score / miss / game-over / mainscreen logo
//   14. pm.Draw(1)                              — particles, foreground tier
//   15. WaveManager::Draw(0)
//   16. HUD::Draw(0x08)   — pause / notifications / tutorial
//   17. HUD::Draw(0x400)  — screen fade / modal scrollers / keyboard popup.
//       Drawn HERE, BEFORE the bomb-hit white flash -- NOT last. This is why
//       the game-over fact-board text (layer 0x400) despawns under the flash
//       on quit in the binary, instead of popping on top of it.
//   18. MainScreen::DrawPostEffects() (conditional)
//   19. DrawCritHit() (conditional)
//   20. HUD::Draw(0x100)  — multiplayer P1 score
//   21. DrawBombHit() (conditional)              — bomb-hit white flash
//   22. HUD::Draw(0x200)  — slider / freeze flash
//   23. NetworkManager::DrawNews() (conditional, defunct in port)
//   24. DrawStartFade() (conditional)
//   25. HUD::Draw(0x800)  — top-most overlay, fires unconditionally at the very
//       end of GameDraw, OUTSIDE the active-guard (see item below).
//
// HUDControl::HUDControl ctor (binary @ 0x00144104) defaults m_LayerFlags=1.
// Subclass ctors / Init may overwrite. Bits 0x02, 0x04, 0x10, 0x20 are
// unused / reserved (no writers and no HUD::Draw caller observed).
//
namespace Mortar {

enum HUDLayer : uint32_t {
    // 0 — never matches any HUD::Draw(layerMask) test; control is filtered
    // out for the frame. Used by ShopScreen / GameOverScreen / TimeControl
    // to gate visibility (e.g. ShopScreen hides during splat-active fade-in,
    // TimeControl hides outside arcade mode).
    HUD_LAYER_NONE        = 0x0000,

    // 0x40 — menu fruit backdrop (first pass, BEFORE splats).
    // Writers: MenuButton::Init (when fruitType >= 0), SpeedControl ctor.
    // MenuButton self-demotes to HUD_LAYER_POST_ACTOR (0x80) inside its
    // DrawOrder once the slide-in settles, so the same control renders at
    // 0x40 during fade-in then at 0x80 during steady state.
    HUD_LAYER_MENU_BG     = 0x0040,

    // 0x80 — post-actor HUD overlay (AFTER splats / blasts / flashes).
    // Writers: ShopScreen, DojoScreen, AboutScreen, BonusScreen,
    //   LeaderboardScreen, UpsellScreen, ComboBox, CheckBox,
    //   KeyboardControl::Init, PowerUpShop::Init, FruitFactControl runtime,
    //   MenuButton self-demote.
    HUD_LAYER_POST_ACTOR  = 0x0080,

    // 0x01 — default HUDControl layer ("background HUD" / score plate).
    // Drawn AFTER particles+slices but BEFORE foreground particles.
    // Writers: every HUDControl by default; MissControl::Init explicit;
    //   ScoreControl uses (1 << m_PlayerIdx) so player 0 = 0x01;
    //   MainScreen logo, GameOverScreen, ScoreMultiplyerBoard,
    //   ProgressionTimerControl, CoinCounter, ComboControl, TimeControl,
    //   FPSCounter, ScrollingMenu, ScrollingMenuItem.
    HUD_LAYER_DEFAULT     = 0x0001,

    // 0x08 — buttons / pause / tutorial.
    // Writers: PauseScreen, TutorialControl.
    HUD_LAYER_BUTTONS     = 0x0008,

    // 0x100 — multiplayer P1 (right-side) score.
    // ScoreControl computes (1 << m_PlayerIdx); m_PlayerIdx == 8 hits 0x100.
    HUD_LAYER_P2_SCORE    = 0x0100,

    // 0x200 — slider widgets / volume sensitivity / freeze-flash bucket.
    // Writers: SliderControl ctor.
    HUD_LAYER_SLIDER      = 0x0200,

    // 0x400 — screen fade + modal scrollers. Drawn right after 0x08 (buttons),
    // BEFORE DrawPostEffects/DrawCritHit/DrawBombHit -- NOT last (v1.6.1
    // GameDraw @0x001cd720; task #35). Writers: ScreenFadeControl, ListBox,
    // VerticalScroller, NotificationControl.
    HUD_LAYER_FADE_MODAL  = 0x0400,

    // 0x800 — top-most overlay; maps to drawOrder="top_most" in effect XML
    // (EffectImage::Parse table @0x2d8bf0, v1.6.1). Fires unconditionally at
    // the very end of GameDraw @0x001cd720, outside the active-guard.
    HUD_LAYER_TOP_MOST    = 0x0800,
};

} // namespace Mortar

#endif // FN_HUD_LAYER_H
