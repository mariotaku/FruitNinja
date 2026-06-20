// PreloadRings -- binary @ 0x11c644
// Loads ring textures and colours into game_work.m_RingTex[17] / m_RingColours[0..12],
// m_Colour69C (grey), and m_TitleColour (brown Zen metallic).
// Called from GameInitialise (binary @ 0x11d22c).

#include "PreloadRings.h"
#include "game/GameWork.h"
#include "math/Colour.h"
#include "asset/TextureManager.h"

void PreloadRings() {
    // Ring texture slot map (binary @ 0x11c644, LoadLocalisedTexture order).
    game_work.m_RingTex[ 0] = Mortar::TextureManager::LoadLocalisedTexture("blob_Shadow.tex");
    game_work.m_RingTex[ 1] = Mortar::TextureManager::LoadLocalisedTexture("blue_ring.tex");
    game_work.m_RingTex[ 2] = Mortar::TextureManager::LoadLocalisedTexture("blue_stripe_ring.tex");
    game_work.m_RingTex[ 3] = Mortar::TextureManager::LoadLocalisedTexture("blue_skinny_ring.tex");
    game_work.m_RingTex[ 4] = Mortar::TextureManager::LoadLocalisedTexture("blue_stars_ring.tex");
    game_work.m_RingTex[ 5] = Mortar::TextureManager::LoadLocalisedTexture("buynow_ring.tex");
    game_work.m_RingTex[ 6] = Mortar::TextureManager::LoadLocalisedTexture("green_dot_ring.tex");
    game_work.m_RingTex[ 7] = Mortar::TextureManager::LoadLocalisedTexture("green_skinny_ring.tex");
    game_work.m_RingTex[ 8] = Mortar::TextureManager::LoadLocalisedTexture("green_ring.tex");
    game_work.m_RingTex[ 9] = Mortar::TextureManager::LoadLocalisedTexture("grey_ring.tex");
    game_work.m_RingTex[10] = Mortar::TextureManager::LoadLocalisedTexture("locked_ring.tex");
    game_work.m_RingTex[11] = Mortar::TextureManager::LoadLocalisedTexture("orange_checker_ring.tex");
    game_work.m_RingTex[12] = Mortar::TextureManager::LoadLocalisedTexture("orange_ring.tex");
    game_work.m_RingTex[13] = Mortar::TextureManager::LoadLocalisedTexture("orange_star_ring.tex");
    game_work.m_RingTex[14] = Mortar::TextureManager::LoadLocalisedTexture("purple_ring.tex");
    game_work.m_RingTex[15] = Mortar::TextureManager::LoadLocalisedTexture("red_skinny_ring.tex");
    game_work.m_RingTex[16] = Mortar::TextureManager::LoadLocalisedTexture("red_ring.tex");

    // Ring colour table (binary @ 0x11c644, RGB order with alpha=255).
    game_work.m_RingColours[ 0] = Colour(0xF9, 0x3E, 0x13, 255);
    game_work.m_RingColours[ 1] = Colour(0xC3, 0x0F, 0x00, 255);
    game_work.m_RingColours[ 2] = Colour(0xFC, 0xA0, 0x11, 255);
    game_work.m_RingColours[ 3] = Colour(0xED, 0x3C, 0x03, 255);
    game_work.m_RingColours[ 4] = Colour(0x93, 0xEE, 0xFF, 255);
    game_work.m_RingColours[ 5] = Colour(0x2D, 0x90, 0xF5, 255);
    game_work.m_RingColours[ 6] = Colour(0xE2, 0xEF, 0x28, 255);
    game_work.m_RingColours[ 7] = Colour(0x54, 0xBB, 0x01, 255);
    game_work.m_RingColours[ 8] = Colour(0xD2, 0xA6, 0xFF, 255);
    game_work.m_RingColours[ 9] = Colour(0x97, 0x36, 0xFC, 255);
    game_work.m_RingColours[10] = Colour(0xFF, 0xE4, 0x00, 255);
    game_work.m_RingColours[11] = Colour(0xEC, 0xAC, 0x05, 255);
    game_work.m_RingColours[12] = Colour(0xD9, 0xD9, 0xD9, 255);
    game_work.m_Colour69C   = Colour(0x5C, 0x5C, 0x5C, 255);   // GameWork+0x69C
    // binary PreloadRings @0x0011cd44: GameWork+0x6a0 = Colour(0x6f,0x46,0x1e) -- brown,
    // used by the "SLICE FRUIT TO BEGIN" text (MainScreen ctor @0x001982fc SetColour).
    game_work.m_TitleColour = Colour(0x6F, 0x46, 0x1E, 255);   // GameWork+0x6A0
}
