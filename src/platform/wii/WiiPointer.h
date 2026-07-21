#ifndef FN_PLATFORM_WII_WII_POINTER_H
#define FN_PLATFORM_WII_WII_POINTER_H

// Port specific: on-screen Wiimote IR hand-pointer overlay. No binary
// equivalent (the original has no cursor -- direct touch input). Draws the
// game's own ninja-hand texture (swipe_fruit_begin.tex, the same 2-frame
// sheet TutorialControl uses for its tutorial arrow) at each connected
// remote's IR aim point, on every screen, so IR aiming is visible without a
// physical touchscreen.
//
// Per remote (see InputTranslatorWii::GetPointer): hidden while IR is
// invalid (remote not pointed at the screen) or while that remote's smoothed
// pointer speed exceeds kHideSpeed (a fast slice shouldn't have a hand icon
// sitting on top of the blade). Shows the "tapped" UV frame while A is held,
// else "pointing".
//
// Only compiled when FRUIT_PLATFORM_WII is set (see
// src/platform/wii/CMakeLists.txt).
#ifdef FRUIT_PLATFORM_WII

class InputTranslatorWii;

namespace FN {
namespace wii {

// Loads swipe_fruit_begin.tex once (lazy, on first Draw call). Safe to call
// repeatedly; no-op after the first successful load.
void WiiPointer_Init();

// Draw the hand pointer for every connected+valid remote in `in`. Call once
// per display frame, after the game's own draw and any HUD/OSD overlays, so
// the cursor is topmost -- see Game::renderFrame (src/GameWii.cpp).
// Establishes its own 2D ortho + world-stack reset (same idiom as
// TutorialControl::Draw / OSD_Draw), so it can be called from any screen
// state without the caller preparing render state first.
void WiiPointer_Draw(const InputTranslatorWii& in);

} // namespace wii
} // namespace FN

#endif // FRUIT_PLATFORM_WII

#endif // FN_PLATFORM_WII_WII_POINTER_H
